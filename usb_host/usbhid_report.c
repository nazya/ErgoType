#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "pio_usb.h"

#include "linux/include/linux/hid.h"
#include "hid_async.h"
#include "hid_transport_sync.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"

/*
 * Interrupt-IN executor. Upstream's per-interface inbuf remains stable from
 * arm through task-side parsing. The exact TinyUSB endpoint callback publishes
 * its raw result and length into the fixed slot which already retains that
 * buffer; the endpoint is not rearmed until the report task consumes the slot.
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
#define USBHID_CONTROL_REPORT_SLOTS (HID_ASYNC_REQUEST_QUEUE_LEN + 1)
#define USBHID_CONTROL_REPORT_QUEUE_LEN USBHID_CONTROL_REPORT_SLOTS
#define USBHID_REPORT_NOTIFY_INDEX 1u
_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES >
		USBHID_REPORT_NOTIFY_INDEX,
	       "HID report lanes require a dedicated task notification index");
/* PIO-USB may publish a raced completion at the end of a later SOF. */
#define USBHID_REPORT_ABORT_DRAIN_FRAMES 2u
/* A busy async FIFO must either drain or park this endpoint within 8 seconds. */
#define USBHID_CLEAR_HALT_QUEUE_RETRY_MS 32u
#define USBHID_CLEAR_HALT_QUEUE_RETRY_MAX 250u

enum usbhid_report_owner {
	USBHID_REPORT_STOPPED,
	USBHID_REPORT_ARMED,
	USBHID_REPORT_QUEUED,
	USBHID_REPORT_ACTIVE,
};

static void usbhid_report_drain_abort_frames(void)
{
	u32 start = pio_usb_host_get_frame_number();

	/* FreeRTOS ticks and the independent PIO SOF timer need not share phase. */
	while (pio_usb_host_get_frame_number() - start <
	       USBHID_REPORT_ABORT_DRAIN_FRAMES)
		vTaskDelay(1);
}

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
	USBHID_REPORT_RECOVERY_DEVICE_RESET,
};

enum usbhid_report_detach_state {
	USBHID_REPORT_DETACH_NONE,
	USBHID_REPORT_DETACH_PENDING,
	USBHID_REPORT_DETACH_QUEUED,
	USBHID_REPORT_DETACH_DONE,
};

struct usbhid_input_report_event {
	struct hid_device *hid;
	u8 *data;
	u32 generation;
	u32 revision; /* Arm/open epoch, not the device generation. */
	u32 len;
	u16 bufsize;
	u8 xfer_result;
};

struct usbhid_control_report_event {
	struct hid_device *hid;
	u8 *data;
	void *context;
	usbhid_control_report_done_t done;
	u32 generation;
	u16 len;
	u16 bufsize;
	u8 report_type;
};

struct usbhid_report_reconcile {
	struct hid_device *hid;
	u32 generation;
};

struct usbhid_report_rx_slot {
	struct hid_device *owner;
	u8 *buffer;
	u32 generation;
	u32 revision; /* Arm/open epoch, not the device generation. */
	u32 serial;
	u32 actual_len;
	u16 bufsize;
	u8 dev_addr;
	u8 ep_addr;
	u8 xfer_result;
	/* Port glue: bounded backpressure while queuing async CLEAR_HALT. */
	u8 clear_halt_queue_retries;
	/* TinyUSB closes the HCD endpoint after its application unmount hooks. */
	u8 detach_state;
};

struct usbhid_report_retry {
	struct timer_list io_retry;
	unsigned long stop_retry;
	unsigned int retry_delay;
};

static QueueHandle_t usbhid_control_report_queue;
static TaskHandle_t usbhid_report_task_handle;
CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN
static struct usbhid_report_rx_slot usbhid_report_rx_slots[CFG_TUH_HID];
/* Keep Linux retry state out of the constrained TinyUSB scratch bank. */
static struct usbhid_report_retry usbhid_report_retries[CFG_TUH_HID];
static u8 usbhid_report_recovery[CFG_TUH_HID];
static u32 usbhid_report_serial;
/* report_host_pending retains each hid until the fenced host pass. */
static struct usbhid_report_reconcile
	usbhid_report_reconcile_pending[CFG_TUH_HID];
static u8 usbhid_report_reconcile_drain_mask;
/* Task-owned round-robin cursor over durable per-interface completions. */
static u8 usbhid_input_report_cursor;

static void usbhid_report_reconcile_on_host(void *data);
static void usbhid_report_detach_fence_on_host(void *data);
static void usbhid_report_xfer_complete(tuh_xfer_t *xfer);
static void hid_retry_timeout(struct timer_list *t);
static void hid_io_error(struct hid_device *hid);
static void usbhid_report_try_clear_halt(struct hid_device *hid);
static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status);

static void usbhid_report_notify_task(void)
{
	TaskHandle_t task;

	hid_transport_lock();
	task = usbhid_report_task_handle;
	hid_transport_unlock();
	if (task)
		(void)xTaskNotifyGiveIndexed(task, USBHID_REPORT_NOTIFY_INDEX);
}

/*
 * Upstream usb_kill_urb(usbhid->urbin) fences the interrupt-IN URB. TinyUSB
 * has no exposed URB, so its owner, deferred host pass, and physical-detach
 * fence form the equivalent durable predicate. Caller holds the transport
 * mutex while testing it.
 */
static bool usbhid_report_idle_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool detach_pending = false;
	int slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;

	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid) {
		u8 detach_state = usbhid_report_rx_slots[slot].detach_state;

		detach_pending =
			detach_state == USBHID_REPORT_DETACH_PENDING ||
			detach_state == USBHID_REPORT_DETACH_QUEUED;
	}
	return usbhid->report_owner == USBHID_REPORT_STOPPED &&
	       !usbhid->report_host_pending && !detach_pending;
}

/* Snapshot the waiter in the same publication scope which reaches idle. */
static TaskHandle_t usbhid_report_idle_waiter_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	return usbhid_report_idle_locked(hid) ? usbhid->wait.task : NULL;
}

bool usbhid_report_idle(struct hid_device *hid)
{
	bool idle;

	hid_transport_lock();
	idle = usbhid_report_idle_locked(hid);
	hid_transport_unlock();
	return idle;
}

static u32 usbhid_report_next_serial_locked(void)
{
	if (!++usbhid_report_serial)
		++usbhid_report_serial;
	return usbhid_report_serial;
}

static int usbhid_report_prepare(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned int insize = usbhid->report_bufsize;
	int slot = -1;

	if (!usbhid->usb_altsetting.has_interrupt_in)
		return -ENODEV;

	/* usbhid_start() computed upstream's interrupt URB length, including ID. */
	if (!insize)
		return -ENODEV;
	// if (insize > HID_MAX_BUFFER_SIZE)
	// 	insize = HID_MAX_BUFFER_SIZE;
	// The exact upstream cap is applied in usbhid_start() before this transport
	// adapter reserves its TinyUSB receive metadata slot. usbhid->inbuf owns the
	// task-context allocation for the full interface lifetime.

	hid_transport_lock();
	if (usbhid->transport_stopping) {
		hid_transport_unlock();
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
		configASSERT(usbhid->inbuf);
		usbhid->report_bufsize = (u16)insize;
		usbhid_report_rx_slots[slot].bufsize = (u16)insize;
	}
	hid_transport_unlock();

	return slot >= 0 ? 0 : -ENOMEM;
}

static bool usbhid_report_arm_on_host(struct hid_device *hid, u32 generation)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_rx_slot *slot;
	tuh_xfer_t xfer = { 0 };
	u32 serial;
	u8 *buffer;
	u16 bufsize;
	u8 ep_addr;
	int index;

	hid_transport_lock();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index < 0 || index >= CFG_TUH_HID ||
	    usbhid_report_rx_slots[index].owner != hid ||
	    usbhid_report_rx_slots[index].serial ||
	    !usbhid->report_bufsize ||
	    !usbhid->usb_altsetting.has_interrupt_in) {
		hid_transport_unlock();
		return false;
	}

	slot = &usbhid_report_rx_slots[index];
	configASSERT(usbhid->inbuf);
	serial = usbhid_report_next_serial_locked();
	buffer = (u8 *)usbhid->inbuf;
	bufsize = usbhid->report_bufsize;
	ep_addr = usbhid->usb_altsetting.interrupt_in_endpoint;
	slot->generation = generation;
	slot->revision = usbhid->report_revision;
	slot->serial = serial;
	slot->buffer = buffer;
	slot->actual_len = 0;
	slot->bufsize = bufsize;
	slot->dev_addr = usbhid->dev_addr;
	slot->ep_addr = ep_addr;
	slot->xfer_result = XFER_RESULT_INVALID;
	hid_transport_unlock();

	xfer.daddr = usbhid->dev_addr;
	xfer.ep_addr = ep_addr;
	// usb_fill_int_urb(usbhid->urbin, dev, pipe, usbhid->inbuf, insize,
	// 		 hid_irq_in, hid, interval);
	// TinyUSB has no URB object here: the opened endpoint retains its interval,
	// upstream's stable inbuf supplies the payload, and this metadata slot
	// supplies the length, callback identity, and owner.
	xfer.buffer = buffer;
	xfer.buflen = bufsize;
	xfer.complete_cb = usbhid_report_xfer_complete;
	xfer.user_data = serial;
	if (tuh_edpt_xfer(&xfer))
		return true;

	hid_transport_lock();
	if (slot->owner == hid && slot->serial == serial)
		slot->serial = 0;
	hid_transport_unlock();
	return false;
}

static void usbhid_report_abort_on_host(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_rx_slot *slot;
	u8 ep_addr = 0;
	int index;

	hid_transport_lock();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID) {
		slot = &usbhid_report_rx_slots[index];
		if (slot->owner == hid && slot->serial) {
			ep_addr = slot->ep_addr;
			slot->serial = 0;
		}
	}
	hid_transport_unlock();

	if (ep_addr && tuh_hid_mounted(usbhid->dev_addr, usbhid->instance) &&
	    usbh_edpt_busy(usbhid->dev_addr, ep_addr))
		(void)tuh_edpt_abort_xfer(usbhid->dev_addr, ep_addr);
}

/*
 * TinyUSB/FreeRTOS callback boundary; upstream hid_irq_in() policy remains in
 * the report task below. The pinned TinyUSB endpoint callback cannot recover
 * transfer_buffer, so the exact arm slot retains upstream's stable inbuf.
 */
static void usbhid_report_xfer_complete(tuh_xfer_t *xfer)
{
	bool published = false;
	u32 serial;

	if (!xfer || !(serial = (u32)xfer->user_data))
		return;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_report_rx_slot *slot =
			&usbhid_report_rx_slots[i];
		struct usbhid_device *usbhid;

		if (slot->serial != serial || slot->dev_addr != xfer->daddr ||
		    slot->ep_addr != xfer->ep_addr)
			continue;
		configASSERT(slot->owner);
		usbhid = slot->owner->driver_data;
		configASSERT(usbhid);
		/*
		 * Pinned TinyUSB gives endpoint completions back only from its USBH
		 * owner task, serialized with reconcile_on_host(). If that boundary
		 * ever becomes ISR/direct-reentrant, this invariant must be replaced
		 * by an atomic owner transition instead of weakening the assertion.
		 */
		configASSERT(usbhid->report_owner == USBHID_REPORT_ARMED);
		configASSERT(slot->generation == usbhid->generation);
		configASSERT(slot->buffer == (u8 *)usbhid->inbuf);
		configASSERT(slot->bufsize == usbhid->report_bufsize);
		slot->actual_len = xfer->actual_len;
		slot->xfer_result = (u8)xfer->result;
		slot->serial = 0;
		usbhid->report_owner = USBHID_REPORT_QUEUED;
		published = true;
		break;
	}
	hid_transport_unlock();

	// case -ECONNRESET:	/* unlink */
	// case -ENOENT:
	// case -ESHUTDOWN:	/* unplug */
	// 	clear_bit(HID_IN_RUNNING, &usbhid->iofl);
	// 	return;
	// TinyUSB abort/unplug clears the slot serial. A late PIO completion then
	// dies here before publication; generation fencing covers the later task.
	if (!published)
		return;
	/* The durable QUEUED predicate is published before its task wakeup. */
	usbhid_report_notify_task();
}

/*
 * TinyUSB 0.18 runs application/class unmount hooks inside its DEVICE_REMOVE
 * event, before class close and hcd_device_close(). A deferred host event is
 * therefore the physical-buffer fence: it cannot run until that remove pass
 * has returned and the HCD can no longer write through usbhid->inbuf.
 */
static void usbhid_report_detach_fence_on_host(void *data)
{
	TaskHandle_t waiters[CFG_TUH_HID] = { 0 };

	(void)data;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_rx_slots[i].detach_state ==
		    USBHID_REPORT_DETACH_QUEUED) {
			usbhid_report_rx_slots[i].detach_state =
				USBHID_REPORT_DETACH_DONE;
			waiters[i] = usbhid_report_idle_waiter_locked(
				usbhid_report_rx_slots[i].owner);
		}
	}
	hid_transport_unlock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (waiters[i])
			xTaskNotifyGive(waiters[i]);
	}
}

static bool usbhid_report_queue_reconcile(struct hid_device *hid,
					   u32 generation, bool drain_sof)
{
	int slot = -1;

	hid_transport_lock();
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
	hid_transport_unlock();

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
	TaskHandle_t waiter = NULL;
	bool still_current;

	mod_timer(&usbhid_report_retries[index].io_retry, expires);

	/* Close/unplug may race the interval between publishing and arming. */
	hid_transport_lock();
	still_current = usbhid_report_rx_slots[index].owner == hid &&
		  usbhid_report_rx_slots[index].generation == usbhid->generation &&
		  usbhid_report_recovery[index] == recovery &&
		  usbhid->report_wanted && !usbhid->transport_stopping &&
		  usbhid->report_host_pending;
	hid_transport_unlock();
	if (still_current ||
	    !timer_delete(&usbhid_report_retries[index].io_retry))
		return;

	hid_transport_lock();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] == recovery) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT)
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED)
			usbhid->report_host_pending = false;
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);
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

	hid_transport_lock();
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
	hid_transport_unlock();
	if (!timer)
		return false;

	was_pending = sync ? timer_delete_sync(timer) : timer_delete(timer);
	hid_transport_lock();
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
	hid_transport_unlock();
	/* Release through the common wait-head wake edge after timer state settles. */
	usbhid_io_put(hid);
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
	TaskHandle_t waiter = NULL;
	bool queue_retry = false;
	bool clear_halt = false;

	hid_transport_lock();
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
	if (hid)
		waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (clear_halt) {
		usbhid_report_try_clear_halt(hid);
		return;
	}
	if (!queue_retry)
		return;
	if (usbhid_report_queue_reconcile(hid, generation, false))
		return;

	usbhid_backend_rx_rearm_failed();
	hid_transport_lock();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid_report_recovery[index] == USBHID_REPORT_RECOVERY_NONE &&
	    usbhid->report_host_pending) {
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);
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

/*
 * Upstream Linux: no equivalent; usb_queue_reset_device() retains the USB
 * interface while usbcore owns reset serialization. This firmware state marks
 * the report epoch instead. The backend snapshots physical topology and queues
 * full TinyUSB teardown/re-enumeration without retaining this hid pointer.
 */
static void usbhid_report_request_device_reset(struct hid_device *hid,
						int index, u32 generation)
{
	struct usbhid_device *usbhid = hid->driver_data;
	u32 revision;
	TaskHandle_t waiter = NULL;
	bool reconcile = false;
	bool park = false;
	int ret;

	/*
	 * report_host_pending is the firmware replacement for reset_work's hid
	 * lifetime. Keep that existing fence through publication: a timer callback
	 * may otherwise be preempted by detach between dropping it and this call.
	 */
	hid_transport_lock();
	revision = usbhid->report_revision;
	hid_transport_unlock();
	ret = usbhid_backend_queue_device_reset(hid, revision);
	hid_transport_lock();
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid->generation == generation &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_DEVICE_RESET) {
		if (ret) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			if (ret == -ECANCELED) {
				/*
				 * close cancelled publication. If a new open won
				 * meanwhile, preserve the lifetime fence and arm
				 * only that new report revision.
				 */
				reconcile = usbhid->report_wanted &&
					    !usbhid->transport_stopping &&
					    usbhid->report_revision != revision;
			} else {
				/* No reset owner exists; park this exact open. */
				park = usbhid->report_wanted &&
				       !usbhid->transport_stopping;
				if (park)
					usbhid->report_wanted = false;
			}
		}
		if (!reconcile)
			usbhid->report_host_pending = false;
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (reconcile &&
	    !usbhid_report_queue_reconcile(hid, generation, false)) {
		waiter = NULL;
		hid_transport_lock();
		if (index >= 0 && index < CFG_TUH_HID &&
		    usbhid_report_rx_slots[index].owner == hid &&
		    usbhid_report_rx_slots[index].generation == generation &&
		    usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_NONE &&
		    usbhid->report_host_pending) {
			park = usbhid->report_wanted &&
			       !usbhid->transport_stopping;
			if (park)
				usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
	}

	if (park)
		usbhid_backend_rx_rearm_failed();
}

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
// TinyUSB reports completion in host-task context, not URB IRQ context. The
// local retry state therefore uses the shared transport task mutex, while the
// timer and reset owner tasks replace Linux's timer/workqueue handoff.
static void hid_io_error(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_retry *retry;
	unsigned long expires = 0;
	unsigned long now = jiffies;
	u32 generation = 0;
	TaskHandle_t waiter = NULL;
	bool device_reset = false;
	int index;

	hid_transport_lock();
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
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
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
		usbhid_report_recovery[index] =
			USBHID_REPORT_RECOVERY_DEVICE_RESET;
		generation = usbhid->generation;
		device_reset = true;
	} else {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_IO_RETRY;
		expires = now + msecs_to_jiffies(retry->retry_delay);
	}
	hid_transport_unlock();

	if (device_reset) {
		// schedule_work(&usbhid->reset_work);
		// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
		// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
		// 	usb_queue_reset_device(usbhid->intf);
		// }
		// TinyUSB has no usbcore in-place reset. Queue asynchronous full
		// teardown/re-enumeration after the same exhausted protocol retry;
		// DEVICE_RESET prevents close/open from rearming this old epoch.
		usbhid_report_request_device_reset(hid, index, generation);
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
	u32 async_generation;
	u32 generation;
	TaskHandle_t waiter = NULL;
	u8 ep_addr;
	bool retry_queue = false;
	bool park = false;
	int index;
	int ret;

	hid_transport_lock();
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
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
		return;
	}

	usbhid_report_recovery[index] =
		USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE;
	generation = usbhid->generation;
	ep_addr = usbhid_report_rx_slots[index].ep_addr;
	hid_transport_unlock();

	// rc = usb_clear_halt(hid_to_usb_dev(hid), usbhid->urbin->pipe);
	// The shared report task cannot block on TinyUSB. Queue usb_clear_halt()'s
	// standard endpoint request on the generic physical-device EP0 lane; its
	// completion continues the upstream reset_work path asynchronously below.
	ret = hid_async_device_epoch_snapshot(usbhid->dev_addr,
					      &async_generation);
	if (!ret)
		ret = hid_async_queue_usb_control_msg(hid, usbhid->dev_addr,
			async_generation, USB_REQ_CLEAR_FEATURE,
			USB_RECIP_ENDPOINT,
			USB_ENDPOINT_HALT, ep_addr, NULL, 0,
			USB_CTRL_SET_TIMEOUT,
			usbhid_report_clear_halt_complete, NULL);
	if (!ret)
		return;

	hid_transport_lock();
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
			usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
			/*
			 * usb_clear_halt() has not reached the wire. Local broker
			 * saturation is not the upstream transfer failure which queues
			 * usb_reset_device(), so park instead of resetting healthy USB.
			 */
			park = ret != -ENODEV && usbhid->report_wanted &&
			       !usbhid->transport_stopping;
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			if (park)
				usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (retry_queue) {
		usbhid_report_schedule_recovery_timer(hid, index,
			USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT,
			jiffies +
			msecs_to_jiffies(USBHID_CLEAR_HALT_QUEUE_RETRY_MS));
	} else if (park)
		usbhid_backend_rx_rearm_failed();
}

static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status)
{
	struct hid_device *hid = req->hid;
	struct usbhid_device *usbhid = hid->driver_data;
	u32 generation = 0;
	TaskHandle_t waiter = NULL;
	bool queue_reconcile = false;
	bool device_reset = false;
	bool park = false;
	int index;

	hid_transport_lock();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE) {
		usbhid_report_rx_slots[index].clear_halt_queue_retries = 0;
		generation = usbhid_report_rx_slots[index].generation;
		if (!status && generation == usbhid->generation &&
		    usbhid->report_wanted && !usbhid->transport_stopping) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE;
			queue_reconcile = true;
		} else {
			park = status && status != -ENODEV &&
				!req->wire_started &&
				generation == usbhid->generation &&
				usbhid->report_wanted &&
				!usbhid->transport_stopping;
			device_reset = status && status != -ENODEV &&
				req->wire_started &&
				generation == usbhid->generation &&
				usbhid->report_wanted &&
				!usbhid->transport_stopping;
			usbhid_report_recovery[index] = device_reset ?
				USBHID_REPORT_RECOVERY_DEVICE_RESET :
				USBHID_REPORT_RECOVERY_NONE;
			if (park)
				usbhid->report_wanted = false;
			if (!device_reset)
				usbhid->report_host_pending = false;
		}
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (!queue_reconcile) {
		if (device_reset) {
			// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
			// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
			// 	usb_queue_reset_device(usbhid->intf);
			// }
			// Linux turns usb_clear_halt() failure into a queued device reset.
			// TinyUSB lacks usbcore's in-place owner, so firmware uses the
			// stronger asynchronous full teardown/re-enumeration fallback.
			usbhid_report_request_device_reset(hid, index, generation);
		} else if (park) {
			/* Local submit starvation never reached usb_clear_halt()'s wire. */
			usbhid_backend_rx_rearm_failed();
		}
		return;
	}
	if (usbhid_report_queue_reconcile(hid, generation, false))
		return;

	usbhid_backend_rx_rearm_failed();
	waiter = NULL;
	hid_transport_lock();
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);
}

int usbhid_report_init(void)
{
	memset(usbhid_report_rx_slots, 0, sizeof(usbhid_report_rx_slots));
	memset(usbhid_report_retries, 0, sizeof(usbhid_report_retries));
	memset(usbhid_report_recovery, 0, sizeof(usbhid_report_recovery));
	usbhid_input_report_cursor = 0;
	// INIT_WORK(&usbhid->reset_work, hid_reset);
	// timer_setup(&usbhid->io_retry, hid_retry_timeout, 0);
	// Fixed report slots own retry timers; hid_async/report tasks replace the
	// Linux reset work item without allocating one object per interface.
	for (int i = 0; i < CFG_TUH_HID; i++)
		timer_setup(&usbhid_report_retries[i].io_retry,
			    hid_retry_timeout, 0);
	usbhid_control_report_queue =
		xQueueCreate(USBHID_CONTROL_REPORT_QUEUE_LEN,
			     sizeof(struct usbhid_control_report_event));
	if (!usbhid_control_report_queue)
		return -ENOMEM;
	return 0;
}

int usbhid_report_start(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool defer;
	int slot;
	int ret;

	if (!hid)
		return -ENODEV;
	usbhid = hid->driver_data;
	ret = usbhid_report_prepare(hid);
	if (ret)
		return ret;

	hid_transport_lock();
	if (usbhid->transport_stopping) {
		hid_transport_unlock();
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
	hid_transport_unlock();

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

	hid_transport_lock();
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	hid_transport_unlock();
	defer |= usbhid_report_cancel_recovery_timer(hid, true);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
}

static bool usbhid_input_report_take(struct usbhid_input_report_event *event)
{
	bool found = false;

	hid_transport_lock();
	for (int offset = 0; offset < CFG_TUH_HID; offset++) {
		int index = (usbhid_input_report_cursor + offset) % CFG_TUH_HID;
		struct usbhid_report_rx_slot *slot =
			&usbhid_report_rx_slots[index];
		struct hid_device *hid = slot->owner;
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (!usbhid || usbhid->report_owner != USBHID_REPORT_QUEUED)
			continue;
		event->hid = hid;
		event->data = slot->buffer;
		event->generation = slot->generation;
		event->revision = slot->revision;
		event->len = slot->actual_len;
		event->bufsize = slot->bufsize;
		event->xfer_result = slot->xfer_result;
		usbhid->report_owner = USBHID_REPORT_ACTIVE;
		usbhid_input_report_cursor = (u8)((index + 1) % CFG_TUH_HID);
		found = true;
		break;
	}
	hid_transport_unlock();
	return found;
}

int usbhid_control_report_submit(struct hid_device *hid, uint8_t report_type,
				 uint8_t *report, uint16_t bufsize,
				 uint16_t len,
				 usbhid_control_report_done_t done,
				 void *context)
{
	struct usbhid_device *usbhid;
	struct usbhid_control_report_event event = {
		.hid = hid,
		.data = report,
		.context = context,
		.done = done,
		.len = len,
		.bufsize = bufsize,
		.report_type = report_type,
	};

	if (!usbhid_control_report_queue || !hid || !report || !done)
		return -ENODEV;
	usbhid = hid->driver_data;
	if (len > bufsize)
		return -EMSGSIZE;

	hid_transport_lock();
	if (usbhid->transport_stopping) {
		hid_transport_unlock();
		return -ENODEV;
	}
	event.generation = usbhid->generation;
	hid_transport_unlock();

	/*
	 * The ordinary HID GET completion budget has five queue slots. Probe-owned
	 * GETs bypass them, so their lifecycle parser cannot block interrupt-IN or
	 * its host fence while driver_input_lock remains held.
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

	hid_transport_lock();
	usbhid->transport_stopping = true;
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	hid_transport_unlock();
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

	hid_transport_lock();
	usbhid->transport_stopping = true;
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	/* TinyUSB closes the physical endpoint around its unmount callback. */
	if (usbhid->report_owner == USBHID_REPORT_ARMED)
		usbhid->report_owner = USBHID_REPORT_STOPPED;
	slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid) {
		usbhid_report_rx_slots[slot].serial = 0;
		if (usbhid_report_rx_slots[slot].detach_state ==
		    USBHID_REPORT_DETACH_NONE)
			usbhid_report_rx_slots[slot].detach_state =
				USBHID_REPORT_DETACH_PENDING;
	}
	hid_transport_unlock();
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

	hid_transport_lock();
	slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	hid_transport_unlock();
	if (slot >= 0 && slot < CFG_TUH_HID)
		timer_delete_sync(&usbhid_report_retries[slot].io_retry);

	hid_transport_lock();
	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid) {
		configASSERT(usbhid->report_owner == USBHID_REPORT_STOPPED);
		configASSERT(!usbhid->report_host_pending);
		configASSERT(!usbhid_report_rx_slots[slot].serial);
		configASSERT(usbhid_report_rx_slots[slot].detach_state !=
			     USBHID_REPORT_DETACH_PENDING);
		configASSERT(usbhid_report_rx_slots[slot].detach_state !=
			     USBHID_REPORT_DETACH_QUEUED);
		memset(&usbhid_report_rx_slots[slot], 0,
		       sizeof(usbhid_report_rx_slots[slot]));
		usbhid_report_retries[slot].stop_retry = 0;
		usbhid_report_retries[slot].retry_delay = 0;
		usbhid_report_recovery[slot] = USBHID_REPORT_RECOVERY_NONE;
	}
	usbhid->report_slot = 0;
	usbhid->report_bufsize = 0;
	hid_transport_unlock();
}

bool usbhid_report_is_stopping(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	bool stopping;

	if (!hid)
		return true;
	usbhid = hid->driver_data;

	hid_transport_lock();
	stopping = usbhid->transport_stopping;
	hid_transport_unlock();
	return stopping;
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
// TinyUSB's host-owner callback is task context, so the shared transport task
// mutex and owner state replace the IRQ-side spinlock. wanted/stopping/owner
// are this port's POLLING/DISCONNECTED/RUNNING gates. TinyUSB submits only from
// its host owner and returns bool, so it cannot preserve Linux's
// -ENOSPC/HID_NO_BANDWIDTH distinction.
static void usbhid_report_reconcile_on_host(void *data)
{
	struct hid_device *hid = data;
	struct usbhid_device *usbhid = hid->driver_data;
	enum usbhid_report_host_action action;
	enum usbhid_report_recovery recovery;
	u32 generation;
	u32 revision;
	TaskHandle_t waiter;
	u8 ep_addr;
	int index;
	bool arm_revision_stale;
	bool device_reset;
	bool ok;

	for (;;) {
		waiter = NULL;
		hid_transport_lock();
		index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
		recovery = index >= 0 && index < CFG_TUH_HID &&
			usbhid_report_rx_slots[index].owner == hid ?
			(enum usbhid_report_recovery)
				usbhid_report_recovery[index] :
			USBHID_REPORT_RECOVERY_NONE;
		ep_addr = index >= 0 && index < CFG_TUH_HID ?
			usbhid_report_rx_slots[index].ep_addr : 0;
		arm_revision_stale =
			usbhid->report_owner == USBHID_REPORT_ARMED &&
			index >= 0 && index < CFG_TUH_HID &&
			usbhid_report_rx_slots[index].owner == hid &&
			usbhid_report_rx_slots[index].revision !=
				usbhid->report_revision;
		if ((usbhid->transport_stopping || !usbhid->report_wanted ||
		     arm_revision_stale) &&
		    usbhid->report_owner == USBHID_REPORT_ARMED) {
			usbhid->report_owner = USBHID_REPORT_STOPPED;
			action = USBHID_REPORT_HOST_ABORT;
		} else if (!usbhid->transport_stopping &&
			   usbhid->report_wanted &&
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
			if ((usbhid->transport_stopping ||
			     !usbhid->report_wanted) && index >= 0 &&
			    index < CFG_TUH_HID &&
			    usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE)
				usbhid_report_recovery[index] =
					USBHID_REPORT_RECOVERY_NONE;
			usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			hid_transport_unlock();
			if (waiter)
				xTaskNotifyGive(waiter);
			return;
		}
		generation = usbhid->generation;
		revision = usbhid->report_revision;
		hid_transport_unlock();

		if (action == USBHID_REPORT_HOST_ABORT) {
			usbhid_report_abort_on_host(hid);
			/*
			 * Fence re-arm through the report task. This is also the port's
			 * usb_kill_urb() boundary for close/reopen: an arm from the old
			 * report revision cannot survive into the new open epoch. If PIO
			 * completed while abort raced its SOF, that old HCD event is already
			 * ahead of the next host defer and cannot consume the new owner.
			 */
			if (usbhid_report_queue_reconcile(hid, generation, true))
				return;

			/* The pending table and its notification-only wake are bounded. */
			usbhid_backend_rx_rearm_failed();
			hid_transport_lock();
			usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			hid_transport_unlock();
			if (waiter)
				xTaskNotifyGive(waiter);
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

			hid_transport_lock();
			device_reset = false;
			if (index >= 0 && index < CFG_TUH_HID &&
			    usbhid_report_rx_slots[index].owner == hid &&
			    usbhid_report_rx_slots[index].generation == generation &&
			    usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
				if (!ok && usbhid->report_wanted &&
				    !usbhid->transport_stopping) {
					usbhid_report_recovery[index] =
						USBHID_REPORT_RECOVERY_DEVICE_RESET;
					device_reset = true;
				} else {
					usbhid_report_recovery[index] =
						USBHID_REPORT_RECOVERY_NONE;
				}
			}
			if (ok && !usbhid->transport_stopping &&
			    usbhid->report_wanted) {
				hid_transport_unlock();
				continue;
			}
			if (!device_reset)
				usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			hid_transport_unlock();
			// if (test_bit(HID_RESET_PENDING, &usbhid->iofl)) {
			// 	dev_dbg(&usbhid->intf->dev, "resetting device\n");
			// 	usb_queue_reset_device(usbhid->intf);
			// }
			// Linux's usb_clear_halt() resets the local endpoint only after
			// remote success. If the PIO DATA0 half fails, firmware queues full
			// asynchronous teardown/re-enumeration instead of rearming a stale
			// endpoint.
			if (device_reset)
				usbhid_report_request_device_reset(hid, index,
								   generation);
			if (waiter)
				xTaskNotifyGive(waiter);
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
			hid_transport_lock();
			if (!usbhid->transport_stopping &&
			    usbhid->report_wanted &&
			    usbhid->report_revision == revision) {
				usbhid->report_host_pending = false;
				hid_transport_unlock();
				return;
			}
			hid_transport_unlock();
			continue;
		}

		usbhid_backend_rx_rearm_failed();
		hid_transport_lock();
		if (usbhid->report_owner == USBHID_REPORT_ARMED)
			usbhid->report_owner = USBHID_REPORT_STOPPED;
		if (usbhid->transport_stopping || !usbhid->report_wanted) {
			usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			hid_transport_unlock();
			if (waiter)
				xTaskNotifyGive(waiter);
			return;
		}
		hid_transport_unlock();
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

static bool usbhid_report_process_detach_fence(void)
{
	bool pending = false;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_rx_slots[i].owner &&
		    usbhid_report_rx_slots[i].detach_state ==
			USBHID_REPORT_DETACH_PENDING) {
			usbhid_report_rx_slots[i].detach_state =
				USBHID_REPORT_DETACH_QUEUED;
			pending = true;
		}
	}
	hid_transport_unlock();

	if (!pending)
		return false;

	/*
	 * usbh_defer_func() may wait for TinyUSB's event queue. Do that here, never
	 * from the unmount callback whose current DEVICE_REMOVE event must return
	 * before this fence can acknowledge hcd_device_close().
	 */
	/* One host event fences every interface removed in the same device pass. */
	usbh_defer_func(usbhid_report_detach_fence_on_host, NULL, false);
	return true;
}

static bool usbhid_report_process_reconcile(void)
{
	struct usbhid_report_reconcile pending[CFG_TUH_HID];
	bool have_pending = false;
	u8 drain_mask = 0;

	hid_transport_lock();
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
	hid_transport_unlock();

	if (!have_pending)
		return false;

	/* Only an abort needs two SOFs before the host-owner fence. */
	if (drain_mask)
		usbhid_report_drain_abort_frames();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_device *pending_usbhid;
		bool reconcile;

		if (!pending[i].hid)
			continue;
		pending_usbhid = pending[i].hid->driver_data;
		hid_transport_lock();
		reconcile = pending[i].generation == pending_usbhid->generation &&
			    pending_usbhid->report_host_pending;
		hid_transport_unlock();
		if (reconcile)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					pending[i].hid, false);
	}
	return true;
}

static bool usbhid_control_report_process_one(void)
{
	struct usbhid_control_report_event event;
	struct usbhid_device *usbhid;
	bool process;

	if (xQueueReceive(usbhid_control_report_queue, &event, 0) != pdPASS)
		return false;

	usbhid = event.hid->driver_data;
	hid_transport_lock();
	process = event.generation == usbhid->generation &&
		  !usbhid->transport_stopping;
	hid_transport_unlock();

	/*
	 * Like upstream hid_ctrl(), a completed control report is delivered once.
	 * hid_safe_input_report() owns the nonblocking parser lock; its return value
	 * is not a USB completion status and must not cause this event to be replayed.
	 */
	if (process)
		(void)hid_safe_input_report(
			event.hid,
			(enum hid_report_type)event.report_type,
			event.data, event.bufsize, event.len, 0);
	usbhid_control_report_finish(&event, process ? 0 : -ENODEV);
	return true;
}

void usbhid_report_task(void *pvParameters)
{
	bool prefer_control = false;

	(void)pvParameters;
	hid_transport_lock();
	usbhid_report_task_handle = xTaskGetCurrentTaskHandle();
	hid_transport_unlock();

	for (;;) {
		struct usbhid_device *usbhid;
		struct usbhid_input_report_event event;
		TaskHandle_t waiter = NULL;
		bool clear_halt = false;
		bool defer = false;
		bool io_retry = false;
		bool payload_valid;
		bool completion_owned;
		bool status_current;
		bool transfer_failed;
		bool process;
		int index;

		/* Callback ingress publishes this durable predicate before its wake. */
		if (usbhid_report_process_detach_fence())
			continue;

		/* Host abort/rearm fences remain globally prior to both report lanes. */
		if (usbhid_report_process_reconcile())
			continue;

		if (prefer_control && usbhid_control_report_process_one()) {
			prefer_control = false;
			continue;
		}

		if (!usbhid_input_report_take(&event)) {
			if (!prefer_control && usbhid_control_report_process_one()) {
				prefer_control = false;
				continue;
			}
			if (usbhid_report_process_reconcile())
				continue;
			/*
			 * Fixed completion slots, the control queue, and the reconcile table
			 * are durable predicates. Index 1 is only their wake edge, so
			 * coalescing notifications cannot lose work.
			 */
			(void)ulTaskNotifyTakeIndexed(
				USBHID_REPORT_NOTIFY_INDEX, pdTRUE,
				portMAX_DELAY);
			continue;
		}
		prefer_control = true;
		usbhid = event.hid->driver_data;
		payload_valid = event.data && event.bufsize &&
			event.len <= event.bufsize && event.len <= UINT16_MAX;

		hid_transport_lock();
		index = usbhid->report_slot ?
			usbhid->report_slot - 1 : -1;
		completion_owned =
			usbhid->report_owner == USBHID_REPORT_ACTIVE &&
			event.generation == usbhid->generation;
		status_current = completion_owned &&
			event.revision == usbhid->report_revision &&
			usbhid->report_wanted && !usbhid->transport_stopping;
		process = status_current;
		/*
		 * Previous callback-side port:
		 *     protocol_mode = tuh_hid_get_protocol(dev_addr, instance);
		 *     if (protocol_mode == HID_PROTOCOL_BOOT) {
		 *             usbhid_transport_fault(USBHID_FAULT_PROTOCOL_BOOT);
		 *             parse = false;
		 *     }
		 *
		 * Linux usbhid relies on the USB reset-default Report protocol and does
		 * not send a generic SET_PROTOCOL. The build-local TinyUSB class now does
		 * the same, so successful exact-generation input needs no port-only mode
		 * gate before the upstream parser.
		 */
		transfer_failed = status_current &&
				 event.xfer_result != XFER_RESULT_SUCCESS;
		// static void hid_irq_in(struct urb *urb)
		// {
		// TinyUSB's boundary callback only published transport state. The
		// status, open gate, parser, and recovery below are the task-context
		// continuation of upstream hid_irq_in().
		// switch (urb->status) {
		// case 0:			/* success */
		// 	usbhid->retry_delay = 0;
		// Each fixed report slot owns Linux's retry epoch. Reset it before
		// parsing exactly when its interrupt transfer succeeds.
		if (status_current && event.xfer_result == XFER_RESULT_SUCCESS &&
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
		hid_transport_unlock();

		// 	if (!test_bit(HID_RESUME_RUNNING, &usbhid->iofl)) {
		// 		hid_safe_input_report(urb->context, HID_INPUT_REPORT,
		// 			      urb->transfer_buffer, urb->transfer_buffer_length,
		// 			      urb->actual_length, 1);
		// TinyUSB supplies the stable slot and exact actual length. Resume-time
		// suppression is not wired yet; normal open/ALWAYS_POLL parsing is here.
		/* usbcore guarantees this bound; validate the pinned PIO giveback. */
		if (status_current && event.xfer_result == XFER_RESULT_SUCCESS &&
		    !payload_valid)
			usbhid_backend_rx_report_dropped();
		if (process && event.xfer_result == XFER_RESULT_SUCCESS &&
		    payload_valid)
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

		hid_transport_lock();
		if (usbhid->report_owner == USBHID_REPORT_ACTIVE)
			usbhid->report_owner = USBHID_REPORT_STOPPED;
		if (completion_owned &&
		    event.generation == usbhid->generation &&
		    usbhid->report_owner == USBHID_REPORT_STOPPED &&
		    !usbhid->transport_stopping &&
		    usbhid->report_wanted &&
		    !usbhid->report_host_pending) {
			/*
			 * start() may have won while this completion was ACTIVE. Re-read
			 * current state here: an old snapshot must never strand wanted=true
			 * in STOPPED. A completion from an older open revision is discarded
			 * like a killed URB and only starts a fresh arm; its status cannot
			 * drive recovery in the new epoch.
			 */
			if (event.revision != usbhid->report_revision) {
				usbhid->report_host_pending = true;
				defer = true;
				goto report_done;
			}
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
	report_done:
		waiter = usbhid_report_idle_waiter_locked(event.hid);
		hid_transport_unlock();

		if (clear_halt)
			usbhid_report_try_clear_halt(event.hid);
		else if (io_retry)
			hid_io_error(event.hid);
		else if (defer)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					event.hid, false);
		if (waiter)
			xTaskNotifyGive(waiter);
	}
}
