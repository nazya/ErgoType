#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "pio_usb.h"

#include "linux/include/linux/hid.h"
#include "hid_async.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"

/*
 * Interrupt-IN executor. A bounded port slot owns each RX buffer from arm
 * through task-side parsing. The exact TinyUSB endpoint callback publishes
 * only a pointer to that stable slot; the endpoint is not rearmed until the
 * event has been consumed.
 *
 * There is exactly one owner per HID interface:
 *
 *   STOPPED -> ARMED -> QUEUED -> ACTIVE -> STOPPED
 *
 * A host-defer reservation is tracked separately because it also retains the
 * hid pointer. close() reconciles an already armed transfer on the TinyUSB
 * host task; a concurrent reopen either keeps that transfer or rearms it after
 * the abort completes.
 */
/* One stable interrupt slot can publish at most one queued event per HID. */
#define USBHID_INPUT_REPORT_QUEUE_LEN CFG_TUH_HID
#define USBHID_CONTROL_REPORT_SLOTS (HID_ASYNC_REQUEST_QUEUE_LEN + 1)
#define USBHID_CONTROL_REPORT_QUEUE_LEN USBHID_CONTROL_REPORT_SLOTS
#define USBHID_REPORT_NOTIFY_INDEX 1u
_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES >
		USBHID_REPORT_NOTIFY_INDEX,
	       "HID report lanes require a dedicated task notification index");
#define USBHID_INTERRUPT_REPORT_MAX 64u
/* PIO-USB may publish a raced completion at the end of a later SOF. */
#define USBHID_REPORT_ABORT_DRAIN_TICKS ((TickType_t)2)
/* A busy async FIFO must either drain or park this endpoint within 8 seconds. */
#define USBHID_CLEAR_HALT_QUEUE_RETRY_MS 32u
#define USBHID_CLEAR_HALT_QUEUE_RETRY_MAX 250u

enum usbhid_report_owner {
	USBHID_REPORT_STOPPED,
	USBHID_REPORT_ARMED,
	USBHID_REPORT_QUEUED,
	USBHID_REPORT_ACTIVE,
};

enum usbhid_report_host_action {
	USBHID_REPORT_HOST_NONE,
	USBHID_REPORT_HOST_ARM,
	USBHID_REPORT_HOST_ABORT,
	USBHID_REPORT_HOST_RESET_DATA_TOGGLE,
};

enum usbhid_report_recovery {
	USBHID_REPORT_RECOVERY_NONE,
	USBHID_REPORT_RECOVERY_IO_RETRY,
	USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT,
	USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE,
	USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE,
};

struct usbhid_input_report_event {
	struct hid_device *hid;
	u8 *data;
	u32 generation;
	u16 len;
	u16 bufsize;
	u8 xfer_result;
	bool parse;
};

struct usbhid_control_report_event {
	struct hid_device *hid;
	u8 *data;
	void *context;
	usbhid_control_report_done_t done;
	TaskHandle_t parser_owner;
	u32 generation;
	u16 len;
	u16 bufsize;
	u8 report_type;
	bool parse;
};

struct usbhid_report_reconcile {
	struct hid_device *hid;
	u32 generation;
};

struct usbhid_report_rx_slot {
	struct hid_device *owner;
	u32 generation;
	u32 serial;
	u16 bufsize;
	u8 dev_addr;
	u8 instance;
	u8 ep_addr;
	/* Port glue: bounded backpressure while queuing async CLEAR_HALT. */
	u8 clear_halt_queue_retries;
};

struct usbhid_report_retry {
	struct timer_list io_retry;
	unsigned long stop_retry;
	unsigned int retry_delay;
};

static QueueHandle_t usbhid_input_report_queue;
static QueueHandle_t usbhid_control_report_queue;
static TaskHandle_t usbhid_report_task_handle;
static struct usbhid_control_report_event usbhid_control_report_handoff;
static bool usbhid_control_report_handoff_ready;
CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN
static struct usbhid_report_rx_slot usbhid_report_rx_slots[CFG_TUH_HID];
CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN
static u8 usbhid_report_rx_buffers[CFG_TUH_HID]
	[USBHID_INTERRUPT_REPORT_MAX];
/* Keep Linux retry state out of the constrained TinyUSB scratch bank. */
static struct usbhid_report_retry usbhid_report_retries[CFG_TUH_HID];
static u8 usbhid_report_recovery[CFG_TUH_HID];
static u32 usbhid_report_serial;
/* report_host_pending retains each hid until the fenced host pass. */
static struct usbhid_report_reconcile
	usbhid_report_reconcile_pending[CFG_TUH_HID];
static u8 usbhid_report_reconcile_drain_mask;

static void usbhid_report_reconcile_on_host(void *data);
static void usbhid_report_xfer_complete(tuh_xfer_t *xfer);
static void hid_retry_timeout(struct timer_list *t);
static void hid_io_error(struct hid_device *hid);
static void usbhid_report_try_clear_halt(struct hid_device *hid);
static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status);

static void usbhid_report_notify_task(void)
{
	TaskHandle_t task;

	taskENTER_CRITICAL();
	task = usbhid_report_task_handle;
	taskEXIT_CRITICAL();
	if (task)
		(void)xTaskNotifyGiveIndexed(task, USBHID_REPORT_NOTIFY_INDEX);
}

void usbhid_control_report_owner_ready(void)
{
	/* Port glue: driver_input_lock readiness is not a FreeRTOS queue event. */
	usbhid_report_notify_task();
}

static u32 usbhid_report_next_serial_locked(void)
{
	if (!++usbhid_report_serial)
		++usbhid_report_serial;
	return usbhid_report_serial;
}

/*
 * Traverse the supplied list of reports and find the longest
 */
static void hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = ((report->size - 1) >> 3) + 1 + hid->report_enum[type].numbered;
		if (*max < size)
			*max = size;
	}
}

static int usbhid_report_prepare(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned int insize = 0;
	int slot = -1;

	if (!usbhid->usb_altsetting.has_interrupt_in)
		return -ENODEV;

	/* Match upstream usbhid's interrupt URB length, including report ID. */
	hid_find_max_report(hid, HID_INPUT_REPORT, &insize);
	if (!insize)
		return -ENODEV;
	// if (insize > HID_MAX_BUFFER_SIZE)
	// 	insize = HID_MAX_BUFFER_SIZE;
	// PIO/TinyUSB owns fixed 64-byte RX slots, so reject instead of allocating
	// and clamping to Linux's 16 KiB maximum.
	if (insize > USBHID_INTERRUPT_REPORT_MAX)
		return -EMSGSIZE;

	taskENTER_CRITICAL();
	if (usbhid->transport_stopping) {
		taskEXIT_CRITICAL();
		return -ENODEV;
	}
	if (usbhid->report_slot) {
		slot = usbhid->report_slot - 1;
		if (slot >= CFG_TUH_HID ||
		    usbhid_report_rx_slots[slot].owner != hid)
			slot = -1;
	} else {
		for (int i = 0; i < CFG_TUH_HID; i++) {
			if (!usbhid_report_rx_slots[i].owner) {
				slot = i;
				break;
			}
		}
		if (slot >= 0) {
			usbhid_report_rx_slots[slot].owner = hid;
			usbhid->report_slot = slot + 1;
		}
	}
	if (slot >= 0) {
		usbhid->report_bufsize = (u16)insize;
		usbhid_report_rx_slots[slot].bufsize = (u16)insize;
	}
	taskEXIT_CRITICAL();

	return slot >= 0 ? 0 : -ENOMEM;
}

static bool usbhid_report_arm_on_host(struct hid_device *hid, u32 generation)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_rx_slot *slot;
	tuh_xfer_t xfer = { 0 };
	u32 serial;
	u16 bufsize;
	u8 ep_addr;
	int index;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index < 0 || index >= CFG_TUH_HID ||
	    usbhid_report_rx_slots[index].owner != hid ||
	    usbhid_report_rx_slots[index].serial ||
	    !usbhid->report_bufsize ||
	    !usbhid->usb_altsetting.has_interrupt_in) {
		taskEXIT_CRITICAL();
		return false;
	}

	slot = &usbhid_report_rx_slots[index];
	serial = usbhid_report_next_serial_locked();
	bufsize = usbhid->report_bufsize;
	ep_addr = usbhid->usb_altsetting.interrupt_in_endpoint;
	slot->generation = generation;
	slot->serial = serial;
	slot->bufsize = bufsize;
	slot->dev_addr = usbhid->dev_addr;
	slot->instance = usbhid->instance;
	slot->ep_addr = ep_addr;
	taskEXIT_CRITICAL();

	xfer.daddr = usbhid->dev_addr;
	xfer.ep_addr = ep_addr;
	// usb_fill_int_urb(usbhid->urbin, dev, pipe, usbhid->inbuf, insize,
	// 		 hid_irq_in, hid, interval);
	// TinyUSB has no URB object here: the opened endpoint retains its interval,
	// while this stable slot supplies Linux's buffer, length, callback, and owner.
	xfer.buffer = usbhid_report_rx_buffers[index];
	xfer.buflen = bufsize;
	xfer.complete_cb = usbhid_report_xfer_complete;
	xfer.user_data = serial;
	if (tuh_edpt_xfer(&xfer))
		return true;

	taskENTER_CRITICAL();
	if (slot->owner == hid && slot->serial == serial)
		slot->serial = 0;
	taskEXIT_CRITICAL();
	return false;
}

static void usbhid_report_abort_on_host(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_rx_slot *slot;
	u8 ep_addr = 0;
	int index;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID) {
		slot = &usbhid_report_rx_slots[index];
		if (slot->owner == hid && slot->serial) {
			ep_addr = slot->ep_addr;
			slot->serial = 0;
		}
	}
	taskEXIT_CRITICAL();

	if (ep_addr && tuh_hid_mounted(usbhid->dev_addr, usbhid->instance) &&
	    usbh_edpt_busy(usbhid->dev_addr, ep_addr))
		(void)tuh_edpt_abort_xfer(usbhid->dev_addr, ep_addr);
}

// static void hid_irq_in(struct urb *urb)
// TinyUSB direct endpoint completion supplies tuh_xfer_t instead of Linux URB.
static void usbhid_report_xfer_complete(tuh_xfer_t *xfer)
{
	struct usbhid_report_rx_slot completed = { 0 };
	u8 *report = NULL;
	u32 serial;

	if (!xfer || !(serial = (u32)xfer->user_data))
		return;

	taskENTER_CRITICAL();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_report_rx_slot *slot =
			&usbhid_report_rx_slots[i];

		if (slot->serial != serial || slot->dev_addr != xfer->daddr ||
		    slot->ep_addr != xfer->ep_addr)
			continue;
		completed = *slot;
		report = usbhid_report_rx_buffers[i];
		slot->serial = 0;
		break;
	}
	taskEXIT_CRITICAL();

	// case -ECONNRESET:	/* unlink */
	// case -ENOENT:
	// case -ESHUTDOWN:	/* unplug */
	// 	clear_bit(HID_IN_RUNNING, &usbhid->iofl);
	// 	return;
	// TinyUSB abort/unplug clears the slot serial. A late PIO completion then
	// dies here before publication; generation fencing covers the later task.
	if (!report)
		return;
	usbhid_backend_report_completed(completed.dev_addr,
		completed.instance, completed.generation, report,
		completed.bufsize, xfer->actual_len, (u8)xfer->result);
}

static bool usbhid_report_queue_reconcile(struct hid_device *hid,
					   u32 generation, bool drain_sof)
{
	int slot = -1;

	taskENTER_CRITICAL();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_reconcile_pending[i].hid == hid &&
		    usbhid_report_reconcile_pending[i].generation == generation) {
			slot = i;
			break;
		}
		if (slot < 0 && !usbhid_report_reconcile_pending[i].hid)
			slot = i;
	}
	if (slot >= 0) {
		usbhid_report_reconcile_pending[slot].hid = hid;
		usbhid_report_reconcile_pending[slot].generation = generation;
		if (drain_sof)
			usbhid_report_reconcile_drain_mask |= (u8)BIT(slot);
	}
	taskEXIT_CRITICAL();

	if (slot < 0)
		return false;

	/* The pending table is the durable predicate; publish it before waking. */
	usbhid_report_notify_task();
	return true;
}

static void usbhid_report_schedule_recovery_timer(
		struct hid_device *hid, int index,
		enum usbhid_report_recovery recovery, unsigned long expires)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool still_current;

	mod_timer(&usbhid_report_retries[index].io_retry, expires);

	/* Close/unplug may race the interval between publishing and arming. */
	taskENTER_CRITICAL();
	still_current = usbhid_report_rx_slots[index].owner == hid &&
		  usbhid_report_rx_slots[index].generation == usbhid->generation &&
		  usbhid_report_recovery[index] == recovery &&
		  usbhid->report_wanted && !usbhid->transport_stopping &&
		  usbhid->report_host_pending;
	taskEXIT_CRITICAL();
	if (still_current ||
	    !timer_delete(&usbhid_report_retries[index].io_retry))
		return;

	taskENTER_CRITICAL();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] == recovery) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT)
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED)
			usbhid->report_host_pending = false;
	}
	taskEXIT_CRITICAL();
}

// static void hid_cancel_delayed_stuff(struct usbhid_device *usbhid)
// {
// 	timer_delete_sync(&usbhid->io_retry);
// 	cancel_work_sync(&usbhid->reset_work);
// }
// Firmware has one static retry timer per report slot. Async CLEAR_HALT lives
// in hid_async_task and is drained separately by the lifecycle barrier.
static bool usbhid_report_cancel_recovery_timer(struct hid_device *hid,
						 bool sync)
{
	struct usbhid_device *usbhid = hid->driver_data;
	enum usbhid_report_recovery recovery;
	struct timer_list *timer = NULL;
	int index;
	int was_pending;
	bool reconcile = false;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid) {
		recovery = usbhid_report_recovery[index];
		if (recovery == USBHID_REPORT_RECOVERY_IO_RETRY ||
		    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT) {
			timer = &usbhid_report_retries[index].io_retry;
			/* Keep the static slot from being reused across timer deletion. */
			usbhid->io_pending++;
		}
	}
	taskEXIT_CRITICAL();
	if (!timer)
		return false;

	was_pending = sync ? timer_delete_sync(timer) : timer_delete(timer);
	taskENTER_CRITICAL();
	if (was_pending && usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] == recovery) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT)
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED) {
			if (usbhid->report_wanted &&
			    !usbhid->transport_stopping) {
				usbhid->report_host_pending = true;
				reconcile = true;
			} else {
				usbhid->report_host_pending = false;
			}
		}
	}
	configASSERT(usbhid->io_pending);
	usbhid->io_pending--;
	taskEXIT_CRITICAL();
	return reconcile;
}

/* I/O retry timer routine */
// static void hid_retry_timeout(struct timer_list *t)
// {
// 	struct usbhid_device *usbhid = timer_container_of(usbhid, t, io_retry);
// 	struct hid_device *hid = usbhid->hid;
//
// 	dev_dbg(&usbhid->intf->dev, "retrying intr urb\n");
// 	if (hid_start_in(hid))
// 		hid_io_error(hid);
// }
// The firmware timer cannot submit through TinyUSB. It publishes a bounded
// report-task wake, which then hands the retry to the TinyUSB host owner.
static void hid_retry_timeout(struct timer_list *t)
{
	struct usbhid_report_retry *retry =
		timer_container_of(retry, t, io_retry);
	struct usbhid_device *usbhid;
	int index = retry - usbhid_report_retries;
	struct hid_device *hid = NULL;
	enum usbhid_report_recovery recovery;
	u32 generation = 0;
	bool queue_retry = false;
	bool clear_halt = false;

	taskENTER_CRITICAL();
	recovery = usbhid_report_recovery[index];
	hid = usbhid_report_rx_slots[index].owner;
	usbhid = hid ? hid->driver_data : NULL;
	if (hid && usbhid_report_rx_slots[index].generation == usbhid->generation &&
	    usbhid->report_owner == USBHID_REPORT_STOPPED &&
	    usbhid->report_host_pending && usbhid->report_wanted &&
	    !usbhid->transport_stopping) {
		generation = usbhid->generation;
		if (recovery == USBHID_REPORT_RECOVERY_IO_RETRY) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			queue_retry = true;
		} else if (recovery ==
			   USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT) {
			clear_halt = true;
		}
	} else if (hid &&
		   (recovery == USBHID_REPORT_RECOVERY_IO_RETRY ||
		    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT)) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT)
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED)
			usbhid->report_host_pending = false;
	}
	taskEXIT_CRITICAL();

	if (clear_halt) {
		usbhid_report_try_clear_halt(hid);
		return;
	}
	if (!queue_retry)
		return;
	if (usbhid_report_queue_reconcile(hid, generation, false))
		return;

	usbhid_backend_rx_rearm_failed();
	taskENTER_CRITICAL();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid_report_recovery[index] == USBHID_REPORT_RECOVERY_NONE &&
	    usbhid->report_host_pending) {
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
	}
	taskEXIT_CRITICAL();
}

/* Workqueue routine to reset the device or clear a halt */
// static void hid_reset(struct work_struct *work)
// {
// 	struct usbhid_device *usbhid =
// 		container_of(work, struct usbhid_device, reset_work);
// 	struct hid_device *hid = usbhid->hid;
// 	int rc;
//
// 	if (test_bit(HID_CLEAR_HALT, &usbhid->iofl)) {
// 		dev_dbg(&usbhid->intf->dev, "clear halt\n");
// 		rc = usb_clear_halt(hid_to_usb_dev(hid), usbhid->urbin->pipe);
// 		clear_bit(HID_CLEAR_HALT, &usbhid->iofl);
// 		if (rc == 0) {
// 			hid_start_in(hid);
// 		} else {
// 			dev_dbg(&usbhid->intf->dev,
// 					"clear-halt failed: %d\n", rc);
// 			set_bit(HID_RESET_PENDING, &usbhid->iofl);
// 		}
// 	}
//
// 	if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
// 		dev_dbg(&usbhid->intf->dev, "resetting device\n");
// 		usb_queue_reset_device(usbhid->intf);
// 	}
// }
// Linux can block reset_work in usb_clear_halt() and hand a reset back to USB
// core. This port splits that work across report, async-EP0, and host-owner
// continuations so no TinyUSB callback waits on progress from TinyUSB itself.

/* Main I/O error handler */
// static void hid_io_error(struct hid_device *hid)
// {
// 	unsigned long flags;
// 	struct usbhid_device *usbhid = hid->driver_data;
//
// 	spin_lock_irqsave(&usbhid->lock, flags);
//
// 	/* Stop when disconnected */
// 	if (test_bit(HID_DISCONNECTED, &usbhid->iofl))
// 		goto done;
//
// 	/* If it has been a while since the last error, we'll assume
// 	 * this a brand new error and reset the retry timeout. */
// 	if (time_after(jiffies, usbhid->stop_retry + HZ/2))
// 		usbhid->retry_delay = 0;
//
// 	/* When an error occurs, retry at increasing intervals */
// 	if (usbhid->retry_delay == 0) {
// 		usbhid->retry_delay = 13;	/* Then 26, 52, 104, 104, ... */
// 		usbhid->stop_retry = jiffies + msecs_to_jiffies(1000);
// 	} else if (usbhid->retry_delay < 100)
// 		usbhid->retry_delay *= 2;
//
// 	if (time_after(jiffies, usbhid->stop_retry)) {
//
// 		/* Retries failed, so do a port reset unless we lack bandwidth*/
// 		if (!test_bit(HID_NO_BANDWIDTH, &usbhid->iofl)
// 		     && !test_and_set_bit(HID_RESET_PENDING, &usbhid->iofl)) {
//
// 			schedule_work(&usbhid->reset_work);
// 			goto done;
// 		}
// 	}
//
// 	mod_timer(&usbhid->io_retry,
// 			jiffies + msecs_to_jiffies(usbhid->retry_delay));
// done:
// 	spin_unlock_irqrestore(&usbhid->lock, flags);
// }
static void hid_io_error(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_retry *retry;
	unsigned long expires = 0;
	unsigned long now = jiffies;
	bool park = false;
	int index;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index < 0 || index >= CFG_TUH_HID ||
	    usbhid_report_rx_slots[index].owner != hid ||
	    usbhid_report_rx_slots[index].generation != usbhid->generation ||
	    usbhid->report_owner != USBHID_REPORT_STOPPED ||
	    !usbhid->report_host_pending || !usbhid->report_wanted ||
	    usbhid->transport_stopping) {
		if (index >= 0 && index < CFG_TUH_HID &&
		    usbhid_report_rx_slots[index].owner == hid &&
		    usbhid->report_owner == USBHID_REPORT_STOPPED &&
		    usbhid_report_recovery[index] == USBHID_REPORT_RECOVERY_NONE)
			usbhid->report_host_pending = false;
		taskEXIT_CRITICAL();
		return;
	}

	retry = &usbhid_report_retries[index];
	if (time_after(now, retry->stop_retry + HZ / 2))
		retry->retry_delay = 0;
	if (retry->retry_delay == 0) {
		retry->retry_delay = 13;	/* Then 26, 52, 104, 104, ... */
		retry->stop_retry = now + msecs_to_jiffies(1000);
	} else if (retry->retry_delay < 100) {
		retry->retry_delay *= 2;
	}

	if (time_after(now, retry->stop_retry)) {
		// schedule_work(&usbhid->reset_work);
		// TinyUSB has no coordinated USB-core reset/re-enumeration path. Park
		// this polling epoch instead of resetting hardware behind TinyUSB's
		// configured-device state; a later successful transfer or physical
		// replug resets the retry epoch.
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
		park = true;
	} else {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_IO_RETRY;
		expires = now + msecs_to_jiffies(retry->retry_delay);
	}
	taskEXIT_CRITICAL();

	if (park) {
		usbhid_backend_rx_rearm_failed();
		return;
	}
	usbhid_report_schedule_recovery_timer(hid, index,
		USBHID_REPORT_RECOVERY_IO_RETRY, expires);
}

// Async replacement for the HID_CLEAR_HALT half of hid_reset() above. The
// broker sends remote CLEAR_FEATURE; only its completion may request the
// host-owner PIO DATA0 reset and rearm.
static void usbhid_report_try_clear_halt(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	enum usbhid_report_recovery recovery;
	u32 generation;
	u8 ep_addr;
	bool retry_queue = false;
	bool fault = false;
	int index;
	int ret;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index < 0 || index >= CFG_TUH_HID ||
	    usbhid_report_rx_slots[index].owner != hid ||
	    usbhid_report_rx_slots[index].generation != usbhid->generation ||
	    usbhid_report_recovery[index] !=
		USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT ||
	    usbhid->report_owner != USBHID_REPORT_STOPPED ||
	    !usbhid->report_host_pending || !usbhid->report_wanted ||
	    usbhid->transport_stopping) {
		if (index >= 0 && index < CFG_TUH_HID &&
		    usbhid_report_rx_slots[index].owner == hid &&
		    usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
			if (usbhid->report_owner == USBHID_REPORT_STOPPED)
				usbhid->report_host_pending = false;
		}
		taskEXIT_CRITICAL();
		return;
	}

	usbhid_report_recovery[index] =
		USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE;
	generation = usbhid->generation;
	ep_addr = usbhid_report_rx_slots[index].ep_addr;
	taskEXIT_CRITICAL();

	ret = hid_async_queue_clear_halt(hid, ep_addr,
		usbhid_report_clear_halt_complete, NULL);
	if (!ret)
		return;

	taskENTER_CRITICAL();
	recovery = usbhid_report_recovery[index];
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE) {
		if (ret == -EBUSY && usbhid->report_wanted &&
		    !usbhid->transport_stopping &&
		    ++usbhid_report_rx_slots[index].clear_halt_queue_retries <
			USBHID_CLEAR_HALT_QUEUE_RETRY_MAX) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT;
			retry_queue = true;
		} else {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
			fault = ret != -ENODEV && usbhid->report_wanted &&
				!usbhid->transport_stopping;
			if (fault)
				usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
	}
	taskEXIT_CRITICAL();

	if (retry_queue) {
		usbhid_report_schedule_recovery_timer(hid, index,
			USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT,
			jiffies +
			msecs_to_jiffies(USBHID_CLEAR_HALT_QUEUE_RETRY_MS));
	} else if (fault) {
		// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
		// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
		// 	usb_queue_reset_device(usbhid->intf);
		// }
		// There is no coordinated TinyUSB reset/re-enumeration path yet;
		// park the endpoint after failed or starved remote clear-halt.
		usbhid_backend_rx_rearm_failed();
	}
}

static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status)
{
	struct hid_device *hid = req->hid;
	struct usbhid_device *usbhid = hid->driver_data;
	u32 generation = 0;
	bool queue_reconcile = false;
	bool fault = false;
	int index;

	taskENTER_CRITICAL();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE) {
		usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		generation = usbhid_report_rx_slots[index].generation;
		if (!status && generation == usbhid->generation &&
		    !usbhid->transport_stopping) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE;
			queue_reconcile = true;
		} else {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			fault = status != -ENODEV && usbhid->report_wanted &&
				!usbhid->transport_stopping;
			if (fault)
				usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
	}
	taskEXIT_CRITICAL();

	if (!queue_reconcile) {
		if (fault) {
			// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
			// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
			// 	usb_queue_reset_device(usbhid->intf);
			// }
			// An asynchronous EP0 failure parks for the same missing-reset reason.
			usbhid_backend_rx_rearm_failed();
		}
		return;
	}
	if (usbhid_report_queue_reconcile(hid, generation, false))
		return;

	usbhid_backend_rx_rearm_failed();
	taskENTER_CRITICAL();
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
	}
	taskEXIT_CRITICAL();
}

int usbhid_report_init(void)
{
	memset(usbhid_report_rx_slots, 0, sizeof(usbhid_report_rx_slots));
	memset(usbhid_report_retries, 0, sizeof(usbhid_report_retries));
	memset(usbhid_report_recovery, 0, sizeof(usbhid_report_recovery));
	// INIT_WORK(&usbhid->reset_work, hid_reset);
	// timer_setup(&usbhid->io_retry, hid_retry_timeout, 0);
	// Fixed report slots own retry timers; hid_async/report tasks replace the
	// Linux reset work item without allocating one object per interface.
	for (int i = 0; i < CFG_TUH_HID; i++)
		timer_setup(&usbhid_report_retries[i].io_retry,
			    hid_retry_timeout, 0);
	usbhid_input_report_queue =
		xQueueCreate(USBHID_INPUT_REPORT_QUEUE_LEN,
			     sizeof(struct usbhid_input_report_event));
	if (!usbhid_input_report_queue)
		return -ENOMEM;
	usbhid_control_report_queue =
		xQueueCreate(USBHID_CONTROL_REPORT_QUEUE_LEN,
			     sizeof(struct usbhid_control_report_event));
	if (!usbhid_control_report_queue) {
		vQueueDelete(usbhid_input_report_queue);
		usbhid_input_report_queue = NULL;
		return -ENOMEM;
	}
	return 0;
}

int usbhid_report_start(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool defer;
	int slot;
	int ret;

	if (!hid || !usbhid_input_report_queue)
		return -ENODEV;
	usbhid = hid->driver_data;
	ret = usbhid_report_prepare(hid);
	if (ret)
		return ret;

	taskENTER_CRITICAL();
	if (usbhid->transport_stopping) {
		taskEXIT_CRITICAL();
		return -ENODEV;
	}
	usbhid->report_wanted = true;
	usbhid->report_revision++;
	slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	defer = usbhid->report_owner == USBHID_REPORT_STOPPED &&
		!usbhid->report_host_pending && slot >= 0 &&
		usbhid_report_recovery[slot] == USBHID_REPORT_RECOVERY_NONE;
	if (defer)
		usbhid->report_host_pending = true;
	taskEXIT_CRITICAL();

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
	return 0;
}

void usbhid_report_close(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool defer = false;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	taskEXIT_CRITICAL();
	defer |= usbhid_report_cancel_recovery_timer(hid, true);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
}

int usbhid_report_submit(struct hid_device *hid, const uint8_t *report,
			 uint16_t bufsize, uint32_t len, uint8_t xfer_result,
			 bool parse)
{
	struct usbhid_device *usbhid;
	struct usbhid_input_report_event event = {
		.hid = hid,
		.data = (u8 *)report,
		.len = len <= UINT16_MAX ? (u16)len : 0,
		.parse = parse && xfer_result == XFER_RESULT_SUCCESS,
		.bufsize = bufsize,
		.xfer_result = xfer_result,
	};
	int status = 0;

	if (!usbhid_input_report_queue)
		return -ENODEV;
	if (!hid)
		return -ENODEV;
	usbhid = hid->driver_data;
	/* A completed transfer must advance ownership even if its payload is bad. */
	if (!report || !bufsize || bufsize > USBHID_INTERRUPT_REPORT_MAX ||
	    len > bufsize || len > UINT16_MAX) {
		event.len = 0;
		event.parse = false;
		status = -EMSGSIZE;
	}

	taskENTER_CRITICAL();
	if (usbhid->report_owner != USBHID_REPORT_ARMED) {
		/* An aborted PIO transfer may publish its old completion one SOF late. */
		if (usbhid->report_owner == USBHID_REPORT_STOPPED &&
		    usbhid->report_host_pending) {
			taskEXIT_CRITICAL();
			return 0;
		}
		taskEXIT_CRITICAL();
		return -EBUSY;
	}
	if (usbhid->transport_stopping || !usbhid->report_wanted) {
		usbhid->report_owner = USBHID_REPORT_STOPPED;
		taskEXIT_CRITICAL();
		return 0;
	}
	event.generation = usbhid->generation;
	usbhid->report_owner = USBHID_REPORT_QUEUED;
	taskEXIT_CRITICAL();

	if (xQueueSendToBack(usbhid_input_report_queue, &event, 0) != pdPASS) {
		taskENTER_CRITICAL();
		if (usbhid->report_owner == USBHID_REPORT_QUEUED) {
			usbhid->report_owner = USBHID_REPORT_STOPPED;
			usbhid->report_wanted = false;
		}
		taskEXIT_CRITICAL();
		return -EBUSY;
	}
	/* The durable queue predicate is published before its task wakeup. */
	usbhid_report_notify_task();

	return status;
}

int usbhid_control_report_submit(struct hid_device *hid, uint8_t report_type,
				 uint8_t *report, uint16_t bufsize,
				 uint16_t len, TaskHandle_t parser_owner,
				 usbhid_control_report_done_t done,
				 void *context)
{
	struct usbhid_device *usbhid;
	struct usbhid_control_report_event event = {
		.hid = hid,
		.data = report,
		.context = context,
		.done = done,
		.parser_owner = parser_owner,
		.len = len,
		.parse = true,
		.bufsize = bufsize,
		.report_type = report_type,
	};

	if (!usbhid_control_report_queue || !hid || !report || !done)
		return -ENODEV;
	usbhid = hid->driver_data;
	if (len > bufsize)
		return -EMSGSIZE;

	taskENTER_CRITICAL();
	if (usbhid->transport_stopping) {
		taskEXIT_CRITICAL();
		return -ENODEV;
	}
	event.generation = usbhid->generation;
	taskEXIT_CRITICAL();

	/*
	 * Each bounded async request has a reserved control-lane slot. Waiting for
	 * parser ownership therefore cannot block interrupt-IN or its host fence.
	 */
	if (xQueueSendToBack(usbhid_control_report_queue, &event, 0) != pdPASS)
		return -EBUSY;
	/* The durable queue predicate is published before its task wakeup. */
	usbhid_report_notify_task();

	return 0;
}

void usbhid_report_stop(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool defer = false;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	usbhid->transport_stopping = true;
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	taskEXIT_CRITICAL();
	usbhid_report_notify_task();
	(void)usbhid_report_cancel_recovery_timer(hid, true);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
}

void usbhid_report_unplug(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	int slot;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	usbhid->transport_stopping = true;
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	/* TinyUSB closes the physical endpoint around its unmount callback. */
	if (usbhid->report_owner == USBHID_REPORT_ARMED)
		usbhid->report_owner = USBHID_REPORT_STOPPED;
	slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid)
		usbhid_report_rx_slots[slot].serial = 0;
	taskEXIT_CRITICAL();
	usbhid_report_notify_task();
	/* TinyUSB unmount callback cannot wait for a running timer callback. */
	(void)usbhid_report_cancel_recovery_timer(hid, false);
}

void usbhid_report_release(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	int slot;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	taskEXIT_CRITICAL();
	if (slot >= 0 && slot < CFG_TUH_HID)
		timer_delete_sync(&usbhid_report_retries[slot].io_retry);

	taskENTER_CRITICAL();
	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid) {
		memset(&usbhid_report_rx_slots[slot], 0,
		       sizeof(usbhid_report_rx_slots[slot]));
		usbhid_report_retries[slot].stop_retry = 0;
		usbhid_report_retries[slot].retry_delay = 0;
		usbhid_report_recovery[slot] = USBHID_REPORT_RECOVERY_NONE;
	}
	usbhid->report_slot = 0;
	usbhid->report_bufsize = 0;
	taskEXIT_CRITICAL();
}

bool usbhid_report_is_stopping(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool stopping;

	if (!hid)
		return true;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	stopping = usbhid->transport_stopping;
	taskEXIT_CRITICAL();
	return stopping;
}

static bool usbhid_report_idle(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool idle;

	taskENTER_CRITICAL();
	idle = usbhid->report_owner == USBHID_REPORT_STOPPED &&
	       !usbhid->report_host_pending;
	taskEXIT_CRITICAL();
	return idle;
}

void usbhid_report_wait_idle(struct hid_device *hid)
{
	while (!usbhid_report_idle(hid))
		vTaskDelay(1);
}

/* Start up the input URB */
// static int hid_start_in(struct hid_device *hid)
// {
// 	unsigned long flags;
// 	int rc = 0;
// 	struct usbhid_device *usbhid = hid->driver_data;
//
// 	spin_lock_irqsave(&usbhid->lock, flags);
// 	if (test_bit(HID_IN_POLLING, &usbhid->iofl) &&
// 	    !test_bit(HID_DISCONNECTED, &usbhid->iofl) &&
// 	    !test_bit(HID_SUSPENDED, &usbhid->iofl) &&
// 	    !test_and_set_bit(HID_IN_RUNNING, &usbhid->iofl)) {
// 		rc = usb_submit_urb(usbhid->urbin, GFP_ATOMIC);
// 		if (rc != 0) {
// 			clear_bit(HID_IN_RUNNING, &usbhid->iofl);
// 			if (rc == -ENOSPC)
// 				set_bit(HID_NO_BANDWIDTH, &usbhid->iofl);
// 		} else {
// 			clear_bit(HID_NO_BANDWIDTH, &usbhid->iofl);
// 		}
// 	}
// 	spin_unlock_irqrestore(&usbhid->lock, flags);
// 	return rc;
// }
// wanted/stopping/owner are this port's POLLING/DISCONNECTED/RUNNING gates.
// TinyUSB submits only from its host owner and returns bool, so it cannot
// preserve Linux's -ENOSPC/HID_NO_BANDWIDTH distinction.
static void usbhid_report_reconcile_on_host(void *data)
{
	struct hid_device *hid = data;
	struct usbhid_device *usbhid = hid->driver_data;
	enum usbhid_report_host_action action;
	enum usbhid_report_recovery recovery;
	u32 generation;
	u32 revision;
	u8 ep_addr;
	int index;
	bool fault;
	bool ok;

	for (;;) {
		taskENTER_CRITICAL();
		index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
		recovery = index >= 0 && index < CFG_TUH_HID &&
			usbhid_report_rx_slots[index].owner == hid ?
			(enum usbhid_report_recovery)
				usbhid_report_recovery[index] :
			USBHID_REPORT_RECOVERY_NONE;
		ep_addr = index >= 0 && index < CFG_TUH_HID ?
			usbhid_report_rx_slots[index].ep_addr : 0;
		if ((usbhid->transport_stopping || !usbhid->report_wanted) &&
		    usbhid->report_owner == USBHID_REPORT_ARMED) {
			usbhid->report_owner = USBHID_REPORT_STOPPED;
			action = USBHID_REPORT_HOST_ABORT;
		} else if (!usbhid->transport_stopping &&
			   usbhid->report_owner == USBHID_REPORT_STOPPED &&
			   recovery ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
			action = USBHID_REPORT_HOST_RESET_DATA_TOGGLE;
		} else if (!usbhid->transport_stopping &&
			   usbhid->report_wanted &&
			   usbhid->report_owner == USBHID_REPORT_STOPPED &&
			   recovery == USBHID_REPORT_RECOVERY_NONE) {
			usbhid->report_owner = USBHID_REPORT_ARMED;
			action = USBHID_REPORT_HOST_ARM;
		} else {
			action = USBHID_REPORT_HOST_NONE;
		}
		if (action == USBHID_REPORT_HOST_NONE) {
			if (usbhid->transport_stopping && index >= 0 &&
			    index < CFG_TUH_HID &&
			    usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE)
				usbhid_report_recovery[index] =
					USBHID_REPORT_RECOVERY_NONE;
			usbhid->report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}
		generation = usbhid->generation;
		revision = usbhid->report_revision;
		taskEXIT_CRITICAL();

		if (action == USBHID_REPORT_HOST_ABORT) {
			usbhid_report_abort_on_host(hid);
			/*
			 * Fence re-arm through the report task. If PIO completed while
			 * abort raced its SOF, that old HCD event is already ahead of the
			 * next host defer and cannot consume the newly armed owner.
			 */
			if (usbhid_report_queue_reconcile(hid, generation, true))
				return;

			/* The pending table and its notification-only wake are bounded. */
			usbhid_backend_rx_rearm_failed();
			taskENTER_CRITICAL();
			usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}

		if (action == USBHID_REPORT_HOST_RESET_DATA_TOGGLE) {
			/*
			 * The pinned PIO HCD maps TinyUSB rhport N to PIO root N-1,
			 * but its hcd_edpt_clear_stall() is a no-op. Remote clear-halt
			 * has completed; reset the idle local endpoint to DATA0 here.
			 */
			ok = tuh_hid_mounted(usbhid->dev_addr, usbhid->instance) &&
			     pio_usb_host_endpoint_reset_data_toggle(
				BOARD_TUH_RHPORT - 1u, usbhid->dev_addr, ep_addr);

			taskENTER_CRITICAL();
			fault = false;
			if (index >= 0 && index < CFG_TUH_HID &&
			    usbhid_report_rx_slots[index].owner == hid &&
			    usbhid_report_rx_slots[index].generation == generation &&
			    usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
				usbhid_report_recovery[index] =
					USBHID_REPORT_RECOVERY_NONE;
				if (!ok && !usbhid->transport_stopping) {
					usbhid->report_wanted = false;
					fault = true;
				}
			}
			if (ok && !usbhid->transport_stopping &&
			    usbhid->report_wanted) {
				taskEXIT_CRITICAL();
				continue;
			}
			usbhid->report_host_pending = false;
			taskEXIT_CRITICAL();
			// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
			// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
			// 	usb_queue_reset_device(usbhid->intf);
			// }
			// TinyUSB has no coordinated USB-core reset/re-enumeration path;
			// park after a local DATA0 failure instead of desynchronizing HCD state.
			if (fault)
				usbhid_backend_rx_rearm_failed();
			return;
		}

		// status = usb_submit_urb(urb, GFP_ATOMIC);
		// if (status) {
		// 	clear_bit(HID_IN_RUNNING, &usbhid->iofl);
		// 	if (status != -EPERM) {
		// 		hid_err(hid, "can't resubmit intr, %s-%s/input%d, status %d\n",
		// 			hid_to_usb_dev(hid)->bus->bus_name,
		// 			hid_to_usb_dev(hid)->devpath,
		// 			usbhid->ifnum, status);
		// 		hid_io_error(hid);
		// 	}
		// }
		// TinyUSB submission runs only in this host-owner pass. A failed submit
		// enters the same delayed I/O recovery instead of spinning here.
		ok = tuh_hid_mounted(usbhid->dev_addr, usbhid->instance) &&
		     usbhid_report_arm_on_host(hid, generation);
		if (ok) {
			taskENTER_CRITICAL();
			if (!usbhid->transport_stopping &&
			    usbhid->report_wanted &&
			    usbhid->report_revision == revision) {
				usbhid->report_host_pending = false;
				taskEXIT_CRITICAL();
				return;
			}
			taskEXIT_CRITICAL();
			continue;
		}

		usbhid_backend_rx_rearm_failed();
		taskENTER_CRITICAL();
		if (usbhid->report_owner == USBHID_REPORT_ARMED)
			usbhid->report_owner = USBHID_REPORT_STOPPED;
		if (usbhid->transport_stopping || !usbhid->report_wanted) {
			usbhid->report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}
		taskEXIT_CRITICAL();
		hid_io_error(hid);
		return;
	}
}

static void usbhid_control_report_finish(
		struct usbhid_control_report_event *event, int status)
{
	/* Parsing is part of upstream control-I/O completion. */
	event->done(event->hid, event->context, status);
}

static bool usbhid_report_process_reconcile(void)
{
	struct usbhid_report_reconcile pending[CFG_TUH_HID];
	bool have_pending = false;
	u8 drain_mask = 0;

	taskENTER_CRITICAL();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_reconcile_pending[i].hid) {
			have_pending = true;
			break;
		}
	}
	if (have_pending) {
		memcpy(pending, usbhid_report_reconcile_pending, sizeof(pending));
		memset(usbhid_report_reconcile_pending, 0,
		       sizeof(usbhid_report_reconcile_pending));
		drain_mask = usbhid_report_reconcile_drain_mask;
		usbhid_report_reconcile_drain_mask = 0;
	}
	taskEXIT_CRITICAL();

	if (!have_pending)
		return false;

	/* Only an abort needs two SOFs before the host-owner fence. */
	if (drain_mask)
		vTaskDelay(USBHID_REPORT_ABORT_DRAIN_TICKS);
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_device *pending_usbhid;
		bool reconcile;

		if (!pending[i].hid)
			continue;
		pending_usbhid = pending[i].hid->driver_data;
		taskENTER_CRITICAL();
		reconcile = pending[i].generation == pending_usbhid->generation &&
			    pending_usbhid->report_host_pending;
		taskEXIT_CRITICAL();
		if (reconcile)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					pending[i].hid, false);
	}
	return true;
}

enum usbhid_control_report_result {
	USBHID_CONTROL_REPORT_EMPTY,
	USBHID_CONTROL_REPORT_BLOCKED,
	USBHID_CONTROL_REPORT_PROCESSED,
};

static enum usbhid_control_report_result
usbhid_control_report_process_one(void)
{
	struct usbhid_control_report_event event;
	struct usbhid_device *usbhid;
	TaskHandle_t parser_owner;
	bool process;

	if (xQueuePeek(usbhid_control_report_queue, &event, 0) != pdPASS)
		return USBHID_CONTROL_REPORT_EMPTY;

	usbhid = event.hid->driver_data;
	parser_owner = event.parser_owner;
	taskENTER_CRITICAL();
	process = event.generation == usbhid->generation &&
		  !usbhid->transport_stopping;
	taskEXIT_CRITICAL();

	/*
	 * A probe-time GET belongs to the lifecycle task already holding
	 * driver_input_lock. Keep the event at the control-lane head until that
	 * task enters hid_hw_wait(), then hand off only this control report.
	 */
	if (process && event.parse && parser_owner &&
	    sema_owned_by_task(&event.hid->driver_input_lock, parser_owner)) {
		bool waiter_ready;

		taskENTER_CRITICAL();
		waiter_ready = usbhid->control_waiter == parser_owner &&
			       !usbhid_control_report_handoff_ready;
		taskEXIT_CRITICAL();
		if (!waiter_ready)
			return USBHID_CONTROL_REPORT_BLOCKED;

		if (xQueueReceive(usbhid_control_report_queue, &event, 0) !=
		    pdPASS)
			return USBHID_CONTROL_REPORT_EMPTY;
		taskENTER_CRITICAL();
		configASSERT(!usbhid_control_report_handoff_ready);
		usbhid_control_report_handoff = event;
		usbhid_control_report_handoff_ready = true;
		taskEXIT_CRITICAL();
		/* The owner runs one priority below this task. */
		vTaskDelay(1);
		return USBHID_CONTROL_REPORT_PROCESSED;
	}

	if (xQueueReceive(usbhid_control_report_queue, &event, 0) != pdPASS)
		return USBHID_CONTROL_REPORT_EMPTY;
	usbhid = event.hid->driver_data;
	taskENTER_CRITICAL();
	process = event.generation == usbhid->generation &&
		  !usbhid->transport_stopping;
	taskEXIT_CRITICAL();

	/*
	 * Like upstream hid_ctrl(), a completed control report is delivered once.
	 * hid_safe_input_report() owns the nonblocking parser lock; its return value
	 * is not a USB completion status and must not cause this event to be replayed.
	 */
	if (process && event.parse)
		(void)hid_safe_input_report(
			event.hid,
			(enum hid_report_type)event.report_type,
			event.data, event.bufsize, event.len, 0);
	usbhid_control_report_finish(&event, process ? 0 : -ENODEV);
	return USBHID_CONTROL_REPORT_PROCESSED;
}

bool usbhid_control_report_process_owned(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	struct usbhid_control_report_event event;
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	bool process;

	if (!hid || !sema_owned_by_current(&hid->driver_input_lock))
		return false;
	usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	if (!usbhid_control_report_handoff_ready ||
	    usbhid_control_report_handoff.hid != hid ||
	    usbhid_control_report_handoff.parser_owner != task ||
	    usbhid->control_waiter != task) {
		taskEXIT_CRITICAL();
		return false;
	}
	event = usbhid_control_report_handoff;
	usbhid_control_report_handoff_ready = false;
	process = event.generation == usbhid->generation &&
		  !usbhid->transport_stopping;
	taskEXIT_CRITICAL();

	usbhid_control_report_finish(&event,
		process && event.parse ?
			hid_safe_input_report_locked(
				hid,
				(enum hid_report_type)event.report_type,
				event.data, event.bufsize, event.len, 0) :
			-ENODEV);
	/* The handoff predicate changed after its event was copied locally. */
	usbhid_report_notify_task();
	return true;
}

void usbhid_report_task(void *pvParameters)
{
	bool prefer_control = false;

	(void)pvParameters;
	taskENTER_CRITICAL();
	usbhid_report_task_handle = xTaskGetCurrentTaskHandle();
	taskEXIT_CRITICAL();

	for (;;) {
		struct usbhid_device *usbhid;
		struct usbhid_input_report_event event;
		enum usbhid_control_report_result control_result =
			USBHID_CONTROL_REPORT_EMPTY;
		bool clear_halt = false;
		bool defer = false;
		bool io_retry = false;
		bool polling;
		bool transfer_failed;
		bool process;
		int index;

		/* Host abort/rearm fences remain globally prior to both report lanes. */
		if (usbhid_report_process_reconcile())
			continue;

		if (prefer_control) {
			control_result = usbhid_control_report_process_one();
			if (control_result == USBHID_CONTROL_REPORT_PROCESSED) {
				prefer_control = false;
				continue;
			}
		}

		if (xQueueReceive(usbhid_input_report_queue, &event, 0) != pdPASS) {
			if (!prefer_control) {
				control_result =
					usbhid_control_report_process_one();
				if (control_result ==
				    USBHID_CONTROL_REPORT_PROCESSED) {
					prefer_control = false;
					continue;
				}
			}
			if (usbhid_report_process_reconcile())
				continue;
			/*
			 * Queues and the reconcile table are durable predicates. Index 1 is
			 * only their wake edge, so coalescing notifications cannot lose work.
			 */
			(void)ulTaskNotifyTakeIndexed(
				USBHID_REPORT_NOTIFY_INDEX, pdTRUE,
				control_result == USBHID_CONTROL_REPORT_BLOCKED ?
					1 : portMAX_DELAY);
			continue;
		}
		prefer_control = true;
		usbhid = event.hid->driver_data;

		taskENTER_CRITICAL();
		index = usbhid->report_slot ?
			usbhid->report_slot - 1 : -1;
		process = usbhid->report_owner == USBHID_REPORT_QUEUED &&
			  event.generation == usbhid->generation &&
			  usbhid->report_wanted &&
			  !usbhid->transport_stopping;
		polling = process;
		transfer_failed = polling &&
				 event.xfer_result != XFER_RESULT_SUCCESS;
		if (usbhid->report_owner == USBHID_REPORT_QUEUED)
			usbhid->report_owner = USBHID_REPORT_ACTIVE;
		// switch (urb->status) {
		// case 0:			/* success */
		// 	usbhid->retry_delay = 0;
		// Each fixed report slot owns Linux's retry epoch. Reset it before
		// parsing exactly when its interrupt transfer succeeds.
		if (polling && event.xfer_result == XFER_RESULT_SUCCESS &&
		    index >= 0 && index < CFG_TUH_HID &&
		    usbhid_report_rx_slots[index].owner == event.hid)
			usbhid_report_retries[index].retry_delay = 0;
		// 	if (!test_bit(HID_OPENED, &usbhid->iofl))
		// 		break;
		// Generic HID core's ll_open_count is the port's HID_OPENED state.
		// ALWAYS_POLL keeps the transfer alive while closed, but drops payload.
		if (!event.hid->ll_open_count)
			process = false;
		// 	usbhid_mark_busy(usbhid);
		// Runtime-PM busy tracking is absent at this firmware boundary.
		taskEXIT_CRITICAL();

		// 	if (!test_bit(HID_RESUME_RUNNING, &usbhid->iofl)) {
		// 		hid_safe_input_report(urb->context, HID_INPUT_REPORT,
		// 			      urb->transfer_buffer, urb->transfer_buffer_length,
		// 			      urb->actual_length, 1);
		// TinyUSB supplies the stable slot and exact actual length. Resume-time
		// suppression is not wired yet; normal open/ALWAYS_POLL parsing is here.
		if (process && event.parse)
			(void)hid_safe_input_report(event.hid,
						HID_INPUT_REPORT,
						event.data, event.bufsize,
						event.len, 1);
		// 		/*
		// 		 * autosuspend refused while keys are pressed
		// 		 * because most keyboards don't wake up when
		// 		 * a key is released
		// 		 */
		// 		if (hid_check_keys_pressed(hid))
		// 			set_bit(HID_KEYS_PRESSED, &usbhid->iofl);
		// 		else
		// 			clear_bit(HID_KEYS_PRESSED, &usbhid->iofl);
		// 	}
		// 	break;
		// Firmware has no Linux autosuspend/key-wakeup policy to update here.
		if (transfer_failed)
			usbhid_backend_rx_transfer_failed(event.xfer_result);

		taskENTER_CRITICAL();
		if (usbhid->report_owner == USBHID_REPORT_ACTIVE)
			usbhid->report_owner = USBHID_REPORT_STOPPED;
		if (polling && event.generation == usbhid->generation &&
		    usbhid->report_owner == USBHID_REPORT_STOPPED &&
		    !usbhid->transport_stopping &&
		    usbhid->report_wanted &&
		    !usbhid->report_host_pending) {
			switch (event.xfer_result) {
			case XFER_RESULT_SUCCESS:
				usbhid->report_host_pending = true;
				defer = true;
				break;
			case XFER_RESULT_STALLED:
				// case -EPIPE:		/* stall */
				// 	usbhid_mark_busy(usbhid);
				// 	clear_bit(HID_IN_RUNNING, &usbhid->iofl);
				// 	set_bit(HID_CLEAR_HALT, &usbhid->iofl);
				// 	schedule_work(&usbhid->reset_work);
				// 	return;
				// The report task publishes reset_work state; EP0 remains owned
				// by hid_async_task and no TinyUSB callback blocks.
				if (index >= 0 && index < CFG_TUH_HID &&
				    usbhid_report_rx_slots[index].owner == event.hid) {
					usbhid_report_recovery[index] =
						USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT;
					usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
					usbhid->report_host_pending = true;
					clear_halt = true;
				}
				break;
			case XFER_RESULT_FAILED:
			case XFER_RESULT_TIMEOUT:
				// case -EILSEQ:	/* protocol error or unplug */
				// case -EPROTO:	/* protocol error or unplug */
				// case -ETIME:		/* protocol error or unplug */
				// case -ETIMEDOUT:	/* Should never happen, but... */
				// 	usbhid_mark_busy(usbhid);
				// 	clear_bit(HID_IN_RUNNING, &usbhid->iofl);
				// 	hid_io_error(hid);
				// 	return;
				// TinyUSB FAILED is the PIO protocol-error bucket; TIMEOUT
				// maps directly to Linux -ETIMEDOUT.
				usbhid->report_host_pending = true;
				io_retry = true;
				break;
			default:
				// default:		/* error */
				// 	hid_warn(urb->dev, "input irq status %d received\n",
				// 		 urb->status);
				// Linux immediately resubmits unclassified completion errors.
				usbhid->report_host_pending = true;
				defer = true;
				break;
			}
		}
		taskEXIT_CRITICAL();

		if (clear_halt)
			usbhid_report_try_clear_halt(event.hid);
		else if (io_retry)
			hid_io_error(event.hid);
		else if (defer)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					event.hid, false);
	}
}
