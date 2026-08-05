/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "host/hcd.h"
#include "host/usbh_pvt.h"
#include "stdio_tusb_cdc.h"

#include "linux/include/linux/hid.h"
#include "hid_async.h"
#include "hid_transport_sync.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"

/*
 * TinyUSB/FreeRTOS interrupt-IN executor; this is transport glue rather than a
 * line-preserving copy of drivers/hid/usbhid/hid-core.c. Upstream's
 * per-interface inbuf remains stable from
 * arm through task-side parsing. The exact TinyUSB endpoint callback publishes
 * its raw result and length into the fixed slot which already retains that
 * buffer; the endpoint is not rearmed until the report task consumes the slot.
 *
 * There is exactly one owner per HID interface:
 *
 *   STOPPED -> ARMED -> QUEUED -> ACTIVE -> STOPPED
 *
 * Host-owner reconciliation is tracked by report_host_pending while the fixed
 * RX slot retains the hid pointer. close() reconciles an already armed transfer
 * on the TinyUSB host task; a concurrent reopen either keeps that transfer or
 * rearms it after the abort completes.
 */
#define USBHID_CONTROL_REPORT_SLOTS (HID_ASYNC_REQUEST_QUEUE_LEN + 1)
#define USBHID_CONTROL_REPORT_QUEUE_LEN USBHID_CONTROL_REPORT_SLOTS
/* Preserve the old local saturation bound without its 32-ms polling. */
#define USBHID_CLEAR_HALT_ADMISSION_TIMEOUT_MS 8000u

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
	/* Host-owner arm failure; report task owns retry/reset policy. */
	USBHID_REPORT_RECOVERY_IO_ERROR_PENDING,
	USBHID_REPORT_RECOVERY_IO_RETRY,
	USBHID_REPORT_RECOVERY_IO_RETRY_CANCEL,
	USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT,
	USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT,
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
	bool opened_at_completion;
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

/*
 * Linux hid_start_in() returns only after usb_submit_urb() has made its first
 * physical submission attempt. The TinyUSB owner must perform that attempt,
 * so keep this caller-owned handoff alive until its one host pass returns.
 */
struct usbhid_report_start_call {
	struct hid_device *hid;
	TaskHandle_t waiter;
	bool done;
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
	/* Snapshot upstream HID_OPENED/RESUME_RUNNING at completion. */
	bool opened_at_completion;
	/* One coalesced host abort/rearm continuation for this exact slot epoch. */
	bool reconcile_pending;
	/* TinyUSB closes the HCD endpoint after its application unmount hooks. */
	u8 detach_state;
	/* Keep reset_work's broker ticket across event-driven -EBUSY retries. */
	struct hid_async_admission_node clear_halt_admission;
};

struct usbhid_report_retry {
	struct timer_list io_retry;
	/* Upstream interrupt-I/O protocol retry epoch. */
	unsigned long stop_retry;
	unsigned int retry_delay;
	/* Port-only fixed-broker admission bound; never aliases the retry epoch. */
	unsigned long clear_halt_deadline;
};

static QueueHandle_t usbhid_control_report_queue;
static TaskHandle_t usbhid_report_task_handle;
CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN
static struct usbhid_report_rx_slot usbhid_report_rx_slots[CFG_TUH_HID];
/* Keep Linux retry state out of the constrained TinyUSB scratch bank. */
static struct usbhid_report_retry usbhid_report_retries[CFG_TUH_HID];
static u8 usbhid_report_recovery[CFG_TUH_HID];
static u32 usbhid_report_serial;
/* Task-owned round-robin cursor over durable per-interface completions. */
static u8 usbhid_input_report_cursor;

static void usbhid_report_reconcile_on_host(void *data);
static void usbhid_report_detach_fence_on_host(void *data);
static void usbhid_report_xfer_complete(tuh_xfer_t *xfer);
static void hid_retry_timeout(struct timer_list *t);
static void hid_io_error(struct hid_device *hid);
static bool usbhid_report_process_io_error(void);
static bool usbhid_report_process_stopping_recovery(void);
static bool usbhid_report_process_clear_halt(void);
static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status);

static void usbhid_report_start_on_host(void *data)
{
	struct usbhid_report_start_call *call = data;
	TaskHandle_t waiter = call->waiter;

	/* This wrapper itself runs in TinyUSB's sole host owner. */
	usbhid_report_reconcile_on_host(call->hid);
	hid_transport_lock();
	call->done = true;
	hid_transport_unlock();
	/* Publishing done releases the stack-owned call; do not touch it again. */
	xTaskNotifyGive(waiter);
}

static void usbhid_report_notify_task(void)
{
	TaskHandle_t task;

	hid_transport_lock();
	task = usbhid_report_task_handle;
	hid_transport_unlock();
	if (task)
		(void)xTaskNotifyGive(task);
}

/* Caller has already retired the corresponding CLEAR_HALT recovery state. */
static void usbhid_report_clear_halt_cancel_admission_locked(int index)
{
	hid_async_admission_unlink_locked(
		&usbhid_report_rx_slots[index].clear_halt_admission);
}

/* Retry only admission nodes whose ticket and physical-slot predicate won. */
static void usbhid_report_admission_wake(void)
{
	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_recovery[i] ==
			    USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT &&
		    hid_async_admission_can_enter_locked(
			    &usbhid_report_rx_slots[i].clear_halt_admission))
			usbhid_report_recovery[i] =
				USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT;
	}
	hid_transport_unlock();
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

/*
 * Non-ALWAYS close mirrors usb_kill_urb(usbhid->urbin), not full teardown.
 * Recovery which has already published a device reset no longer owns this HID;
 * report_host_pending is the active-work predicate throughout that handoff.
 */
static bool usbhid_report_close_idle_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool transfer_released = true;
	int slot = usbhid->report_slot ? usbhid->report_slot - 1 : -1;

	if (slot >= 0 && slot < CFG_TUH_HID &&
	    usbhid_report_rx_slots[slot].owner == hid)
		transfer_released = !usbhid_report_rx_slots[slot].serial;

	return !usbhid->report_wanted &&
	       usbhid->report_owner == USBHID_REPORT_STOPPED &&
	       !usbhid->report_host_pending && transfer_released;
}

/* Publish both the close edge and Linux-style wake_up_all() transport edge. */
static TaskHandle_t usbhid_report_idle_waiter_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	if (usbhid_report_idle_locked(hid))
		usbhid_wait_wake_locked(hid);
	/* Intermediate edges may wake close; its loop rechecks the durable state. */
	return usbhid->report_close_waiter;
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
	bool inbuf_ready = true;
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
			if (!usbhid_report_rx_slots[i].owner &&
			    usbhid_report_rx_slots[i].detach_state ==
				USBHID_REPORT_DETACH_NONE) {
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
		inbuf_ready = usbhid->inbuf != NULL;
		usbhid->report_bufsize = (u16)insize;
		usbhid_report_rx_slots[slot].bufsize = (u16)insize;
	}
	hid_transport_unlock();
	if (!inbuf_ready) {
		async_msg("ERR: HID_RX_BUF_MISSING");
		configASSERT(inbuf_ready);
		return -ENODEV;
	}

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
	bool inbuf_ready;

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
	inbuf_ready = usbhid->inbuf != NULL;
	if (!inbuf_ready) {
		hid_transport_unlock();
		/* Host callback returns failure; report task publishes RX_REARM. */
		return false;
	}
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
	TaskHandle_t waiter = NULL;
	bool published = false;
	bool invalid = false;
	u32 serial;

	if (!xfer || !(serial = (u32)xfer->user_data))
		return;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_report_rx_slot *slot =
			&usbhid_report_rx_slots[i];
		struct hid_device *hid;
		struct usbhid_device *usbhid;
		bool payload_owned;
		bool tuple_valid;

		/* The arm serial is unique; address/endpoint belong to validation. */
		if (slot->serial != serial)
			continue;
		hid = usbhid_report_owner_lookup_locked((unsigned int)i);
		usbhid = hid->driver_data;
		/*
		 * Pinned TinyUSB gives endpoint completions back only from its USBH
		 * owner task, serialized with reconcile_on_host(). If that boundary
		 * ever becomes ISR/direct-reentrant, this validation must be replaced
		 * by an atomic owner transition rather than publishing a stale tuple.
		 */
		tuple_valid = slot->owner == hid &&
			usbhid->report_slot == (u8)(i + 1) &&
			slot->dev_addr == xfer->daddr &&
			slot->ep_addr == xfer->ep_addr &&
			usbhid->dev_addr == xfer->daddr &&
			usbhid->usb_altsetting.interrupt_in_endpoint == xfer->ep_addr &&
			usbhid->report_owner == USBHID_REPORT_ARMED &&
			slot->generation == usbhid->generation &&
			slot->buffer == (u8 *)usbhid->inbuf &&
			slot->bufsize == usbhid->report_bufsize;
		slot->serial = 0;
		if (!tuple_valid) {
			invalid = true;
			slot->opened_at_completion = false;
			/* Restore the registry-owned back-reference before task teardown. */
			slot->owner = hid;
			usbhid->report_wanted = false;
			usbhid->report_revision++;
			payload_owned =
				usbhid->report_owner == USBHID_REPORT_QUEUED ||
				usbhid->report_owner == USBHID_REPORT_ACTIVE;
			if (usbhid->report_owner == USBHID_REPORT_ARMED ||
			    usbhid->report_owner > USBHID_REPORT_ACTIVE)
				usbhid->report_owner = USBHID_REPORT_STOPPED;
			if (!payload_owned) {
				slot->actual_len = 0;
				slot->xfer_result = XFER_RESULT_INVALID;
			}
			waiter = usbhid_report_idle_waiter_locked(hid);
			break;
		}

		slot->actual_len = xfer->actual_len;
		slot->xfer_result = (u8)xfer->result;
		slot->opened_at_completion =
			usbhid->report_open_state == USBHID_REPORT_OPEN ||
			(usbhid->report_open_state == USBHID_REPORT_RESUMING &&
			 usbhid->report_rebuild_state ==
				USBHID_REPORT_REBUILD_RESUME_PENDING);
		usbhid->report_owner = USBHID_REPORT_QUEUED;
		published = true;
		break;
	}
	hid_transport_unlock();

	if (waiter)
		xTaskNotifyGive(waiter);
	if (invalid) {
		/* Callback publishes one bit; lifecycle owns diagnostics. */
		usbhid_report_notify_task();
		usbhid_backend_rx_invariant_failed();
		return;
	}

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
					   u32 generation)
{
	bool queued = false;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (usbhid_report_rx_slots[i].owner == hid &&
		    usbhid_report_rx_slots[i].generation == generation) {
			usbhid_report_rx_slots[i].reconcile_pending = true;
			queued = true;
			break;
		}
	}
	hid_transport_unlock();

	if (!queued)
		return false;

	/* The owner slot is the durable predicate; publish it before waking. */
	usbhid_report_notify_task();
	return true;
}

static void usbhid_report_schedule_io_retry_timer(
		struct hid_device *hid, int index, unsigned long expires)
{
	struct usbhid_device *usbhid = hid->driver_data;
	TaskHandle_t waiter = NULL;
	bool still_current;

	mod_timer(&usbhid_report_retries[index].io_retry, expires);

	/* Close/unplug may race the interval between publishing and arming. */
	hid_transport_lock();
	still_current = usbhid_report_rx_slots[index].owner == hid &&
		  usbhid_report_rx_slots[index].generation == usbhid->generation &&
		  usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_IO_RETRY &&
		  usbhid->report_wanted && !usbhid->transport_stopping &&
		  usbhid->report_host_pending;
	hid_transport_unlock();
	if (still_current ||
	    !timer_delete(&usbhid_report_retries[index].io_retry))
		return;

	hid_transport_lock();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] == USBHID_REPORT_RECOVERY_IO_RETRY) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
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
// Firmware retains the protocol retry timer. CLEAR_HALT reset work is a
// durable report-task state; its active async slot shares the lifecycle fence.
static bool usbhid_report_cancel_recovery(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	enum usbhid_report_recovery recovery = USBHID_REPORT_RECOVERY_NONE;
	struct timer_list *timer = NULL;
	TaskHandle_t waiter = NULL;
	int index = -1;
	int was_pending;
	bool cancel_reset = false;
	bool local_wait = false;
	bool reconcile = false;

	hid_transport_lock();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	if (index >= 0 && index < CFG_TUH_HID &&
	    usbhid_report_rx_slots[index].owner == hid) {
		recovery = usbhid_report_recovery[index];
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT ||
		    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			usbhid_report_retries[index].clear_halt_deadline = 0;
			usbhid_report_clear_halt_cancel_admission_locked(index);
			if (usbhid->report_owner == USBHID_REPORT_STOPPED) {
				if (usbhid->report_wanted &&
				    !usbhid->transport_stopping) {
					usbhid->report_host_pending = true;
					reconcile = true;
				} else {
					usbhid->report_host_pending = false;
				}
			}
			waiter = usbhid_report_idle_waiter_locked(hid);
			local_wait = true;
		} else if (recovery == USBHID_REPORT_RECOVERY_IO_RETRY) {
			timer = &usbhid_report_retries[index].io_retry;
			/* Keep the static slot from being reused across timer deletion. */
			usbhid->io_pending++;
		} else if (recovery == USBHID_REPORT_RECOVERY_DEVICE_RESET) {
			/* Keep hid/cache alive while canceling its queued reset ticket. */
			usbhid->io_pending++;
			cancel_reset = true;
		}
	}
	hid_transport_unlock();
	if (local_wait) {
		if (waiter)
			xTaskNotifyGive(waiter);
		return reconcile;
	}
	if (cancel_reset) {
		bool const cancelled =
			usbhid_backend_cancel_device_reset(hid);

		hid_transport_lock();
		if (cancelled && usbhid_report_rx_slots[index].owner == hid &&
		    usbhid_report_recovery[index] == recovery) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
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
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
		usbhid_io_put(hid);
		return reconcile;
	}
	if (!timer)
		return false;

	was_pending = timer_delete_sync(timer);
	hid_transport_lock();
	if (was_pending && usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_recovery[index] == recovery) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
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
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);
	/* Release through the common wait-head wake edge after timer state settles. */
	usbhid_io_put(hid);
	return reconcile;
}

/*
 * Upstream usb_kill_urb()/hid_cancel_delayed_stuff() run from process context.
 * TinyUSB announces physical detach inside its host callback, so that callback
 * publishes transport_stopping and this report-task continuation owns the
 * synchronous retry-timer cancellation. The io_pending lease is Linux USB
 * core's missing interface reference across the unlocked timer wait.
 */
static bool usbhid_report_process_stopping_recovery(void)
{
	struct hid_device *hid = NULL;
	struct usbhid_device *usbhid = NULL;
	struct timer_list *timer = NULL;
	TaskHandle_t waiter = NULL;
	u32 generation = 0;
	bool processed = false;
	int index = -1;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		enum usbhid_report_recovery recovery;

		hid = usbhid_report_rx_slots[i].owner;
		usbhid = hid ? hid->driver_data : NULL;
		if (!usbhid || !usbhid->transport_stopping)
			continue;
		recovery = usbhid_report_recovery[i];
		if (recovery == USBHID_REPORT_RECOVERY_IO_RETRY) {
			/* Claim before unlock so the timer callback cannot retire hid. */
			usbhid_report_recovery[i] =
				USBHID_REPORT_RECOVERY_IO_RETRY_CANCEL;
			usbhid->io_pending++;
			timer = &usbhid_report_retries[i].io_retry;
			generation = usbhid_report_rx_slots[i].generation;
			index = i;
			processed = true;
			break;
		}
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT ||
		    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT) {
			/* No async request owns these pre-wire reset_work states. */
			usbhid_report_recovery[i] = USBHID_REPORT_RECOVERY_NONE;
			usbhid_report_retries[i].clear_halt_deadline = 0;
			usbhid_report_clear_halt_cancel_admission_locked(i);
			if (usbhid->report_owner == USBHID_REPORT_STOPPED)
				usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			processed = true;
			break;
		}
	}
	hid_transport_unlock();

	if (waiter)
		xTaskNotifyGive(waiter);
	if (!timer)
		return processed;

	(void)timer_delete_sync(timer);
	waiter = NULL;
	hid_transport_lock();
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    usbhid_report_recovery[index] ==
		USBHID_REPORT_RECOVERY_IO_RETRY_CANCEL) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED)
			usbhid->report_host_pending = false;
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);
	usbhid_io_put(hid);
	return true;
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
		}
	} else if (hid && recovery == USBHID_REPORT_RECOVERY_IO_RETRY) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		if (usbhid->report_owner == USBHID_REPORT_STOPPED)
			usbhid->report_host_pending = false;
	}
	if (hid)
		waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (!queue_retry)
		return;
	if (usbhid_report_queue_reconcile(hid, generation))
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
						int index, u32 generation,
						u32 revision,
						bool reset_work_running)
{
	struct usbhid_device *usbhid = hid->driver_data;
	TaskHandle_t waiter = NULL;
	bool reconcile = false;
	bool park = false;
	int ret;

	/*
	 * report_host_pending is the firmware replacement for reset_work's hid
	 * lifetime. Keep that existing fence through publication: a timer callback
	 * may otherwise be preempted by detach between dropping it and this call.
	 * The caller captured revision under the same lock which changed recovery
	 * to DEVICE_RESET, closing Linux cancel_work_sync()'s close/reopen race.
	 */
	ret = usbhid_backend_queue_device_reset(hid, revision,
					       reset_work_running);
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
	    !usbhid_report_queue_reconcile(hid, generation)) {
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
// TinyUSB's host owner publishes a failed arm to the report task. The local
// retry state therefore uses the shared transport task mutex, while the timer
// and reset owner tasks replace Linux's timer/workqueue handoff.
static void hid_io_error(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_report_retry *retry;
	unsigned long expires = 0;
	unsigned long now = jiffies;
	u32 generation = 0;
	u32 revision = 0;
	TaskHandle_t waiter = NULL;
	bool device_reset = false;
	bool requeue = false;
	bool park = false;
	bool pending_owned;
	int index;

	hid_transport_lock();
	index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
	pending_owned = index >= 0 && index < CFG_TUH_HID &&
		usbhid_report_rx_slots[index].owner == hid &&
		usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_IO_ERROR_PENDING;
	if (!pending_owned ||
	    usbhid_report_rx_slots[index].generation != usbhid->generation ||
	    usbhid_report_rx_slots[index].revision != usbhid->report_revision ||
	    usbhid->report_owner != USBHID_REPORT_STOPPED ||
	    !usbhid->report_host_pending || !usbhid->report_wanted ||
	    usbhid->transport_stopping) {
		if (pending_owned) {
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			if (usbhid->report_owner == USBHID_REPORT_STOPPED) {
				/* A close/reopen discards the old failure and arms its new epoch. */
				requeue = usbhid_report_rx_slots[index].generation ==
						usbhid->generation &&
					  usbhid->report_wanted &&
					  !usbhid->transport_stopping;
				usbhid->report_host_pending = requeue;
				park = !requeue && usbhid->report_wanted &&
				       !usbhid->transport_stopping;
				if (park)
					usbhid->report_wanted = false;
			}
		}
		generation = usbhid->generation;
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
		if (!requeue) {
			if (park)
				usbhid_backend_rx_rearm_failed();
			return;
		}
		if (usbhid_report_queue_reconcile(hid, generation))
			return;

		/* The report slot no longer retains this open epoch. */
		usbhid_backend_rx_rearm_failed();
		waiter = NULL;
		hid_transport_lock();
		if (usbhid_report_rx_slots[index].owner == hid &&
		    usbhid_report_rx_slots[index].generation == generation &&
		    usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_NONE &&
		    usbhid->report_host_pending) {
			usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
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
		revision = usbhid->report_revision;
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
		usbhid_report_request_device_reset(hid, index, generation,
						   revision, false);
		return;
	}
	usbhid_report_schedule_io_retry_timer(hid, index, expires);
}

/*
 * Upstream calls hid_io_error() after usb_submit_urb() fails. TinyUSB permits
 * the physical submit only in its host owner, so that owner publishes this
 * fixed-slot state and the report task resumes the upstream retry policy.
 */
static bool usbhid_report_process_io_error(void)
{
	struct hid_device *hid = NULL;
	TaskHandle_t waiter = NULL;
	bool processed = false;

	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_device *usbhid;

		if (usbhid_report_recovery[i] !=
		    USBHID_REPORT_RECOVERY_IO_ERROR_PENDING)
			continue;
		processed = true;
		hid = usbhid_report_rx_slots[i].owner;
		usbhid = hid ? hid->driver_data : NULL;
		if (usbhid && usbhid->report_host_pending)
			break;

		/* No lifetime fence remains, so retire without exporting a pointer. */
		usbhid_report_recovery[i] = USBHID_REPORT_RECOVERY_NONE;
		if (usbhid && usbhid->report_owner == USBHID_REPORT_STOPPED) {
			usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
		}
		hid = NULL;
		break;
	}
	hid_transport_unlock();

	if (waiter)
		xTaskNotifyGive(waiter);
	if (hid) {
		usbhid_backend_rx_rearm_failed();
		hid_io_error(hid);
	}
	return processed;
}

// Async replacement for the HID_CLEAR_HALT half of hid_reset() above. The
// broker sends remote CLEAR_FEATURE; only its completion may request the
// host-owner PIO DATA0 reset and rearm.
static bool usbhid_report_process_clear_halt(void)
{
	struct hid_device *hid = NULL;
	struct usbhid_device *usbhid;
	enum usbhid_report_recovery recovery;
	u32 async_generation;
	u32 generation;
	TaskHandle_t waiter = NULL;
	u8 dev_addr;
	u8 ep_addr;
	bool park = false;
	unsigned long now;
	int index = -1;
	int ret;

	hid_transport_lock();
	now = jiffies;
	for (int i = 0; i < CFG_TUH_HID; i++) {
		recovery = usbhid_report_recovery[i];
		if (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_SUBMIT ||
		    (recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT &&
		     time_after_eq(now,
			usbhid_report_retries[i].clear_halt_deadline))) {
			index = i;
			hid = usbhid_report_rx_slots[i].owner;
			break;
		}
	}
	if (index < 0) {
		hid_transport_unlock();
		return false;
	}
	usbhid = hid->driver_data;
	if (!usbhid->report_wanted || usbhid->transport_stopping) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		usbhid_report_retries[index].clear_halt_deadline = 0;
		usbhid_report_clear_halt_cancel_admission_locked(index);
		usbhid->report_host_pending = false;
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
		return true;
	}
	if (time_after_eq(
		now, usbhid_report_retries[index].clear_halt_deadline)) {
		usbhid_report_recovery[index] = USBHID_REPORT_RECOVERY_NONE;
		usbhid_report_retries[index].clear_halt_deadline = 0;
		usbhid_report_clear_halt_cancel_admission_locked(index);
		usbhid->report_wanted = false;
		usbhid->report_host_pending = false;
		waiter = usbhid_report_idle_waiter_locked(hid);
		hid_transport_unlock();
		if (waiter)
			xTaskNotifyGive(waiter);
		usbhid_backend_rx_rearm_failed();
		return true;
	}

	/* Claim reset_work before unlock so stop cannot retire this hid pointer. */
	usbhid_report_recovery[index] =
		USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE;
	/* The generic admission edge wakes this same report-task notification. */
	usbhid_report_rx_slots[index].clear_halt_admission.task =
		usbhid_report_task_handle;
	generation = usbhid->generation;
	dev_addr = usbhid->dev_addr;
	ep_addr = usbhid_report_rx_slots[index].ep_addr;
	hid_transport_unlock();

	// rc = usb_clear_halt(hid_to_usb_dev(hid), usbhid->urbin->pipe);
	// The shared report task cannot block on TinyUSB. Queue usb_clear_halt()'s
	// standard endpoint request on the generic physical-device EP0 lane; its
	// completion continues the upstream reset_work path asynchronously below.
	ret = hid_async_device_epoch_snapshot(dev_addr, &async_generation);
	if (!ret)
		ret = hid_async_queue_usb_control_msg_admitted(hid, dev_addr,
			async_generation, USB_REQ_CLEAR_FEATURE,
			USB_RECIP_ENDPOINT,
			USB_ENDPOINT_HALT, ep_addr, NULL, 0,
			USB_CTRL_SET_TIMEOUT,
			&usbhid_report_rx_slots[index].clear_halt_admission,
			usbhid_report_clear_halt_complete, NULL);
	if (!ret) {
		hid_transport_lock();
		if (usbhid_report_rx_slots[index].owner == hid &&
		    usbhid_report_rx_slots[index].generation == generation &&
		    usbhid_report_recovery[index] ==
			USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE) {
			usbhid_report_retries[index].clear_halt_deadline = 0;
		}
		hid_transport_unlock();
		return true;
	}

	hid_transport_lock();
	recovery = usbhid_report_recovery[index];
	if (usbhid_report_rx_slots[index].owner == hid &&
	    usbhid_report_rx_slots[index].generation == generation &&
	    recovery == USBHID_REPORT_RECOVERY_CLEAR_HALT_ACTIVE) {
		if (ret == -EBUSY && usbhid->report_wanted &&
		    !usbhid->transport_stopping &&
		    time_before(jiffies,
			usbhid_report_retries[index].clear_halt_deadline)) {
			/* The linked admission node owns the next capacity wake edge. */
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT;
		} else {
			/*
			 * usb_clear_halt() has not reached the wire. A local fatal error
			 * is not the upstream transfer failure which queues
			 * usb_reset_device(), so park instead of resetting healthy USB.
			 */
			park = ret != -ENODEV && usbhid->report_wanted &&
			       !usbhid->transport_stopping;
			usbhid_report_recovery[index] =
				USBHID_REPORT_RECOVERY_NONE;
			usbhid_report_retries[index].clear_halt_deadline = 0;
			/* Snapshot failure did not enter the broker; this is still safe. */
			usbhid_report_clear_halt_cancel_admission_locked(index);
			if (park)
				usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
		}
	}
	waiter = usbhid_report_idle_waiter_locked(hid);
	hid_transport_unlock();
	if (waiter)
		xTaskNotifyGive(waiter);

	if (park)
		usbhid_backend_rx_rearm_failed();
	return true;
}

static void usbhid_report_clear_halt_complete(
		const struct hid_async_request *req, int status)
{
	struct hid_device *hid = req->hid;
	struct usbhid_device *usbhid = hid->driver_data;
	u32 generation = 0;
	u32 revision = 0;
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
		usbhid_report_retries[index].clear_halt_deadline = 0;
		generation = usbhid_report_rx_slots[index].generation;
		if (!status && generation == usbhid->generation &&
		    !usbhid->transport_stopping) {
			/*
			 * usb_clear_halt() resets the host endpoint even while HID is
			 * closed; report_wanted gates only hid_start_in() below.
			 */
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
				!usbhid->transport_stopping;
			if (device_reset)
				revision = usbhid->report_revision;
			/* A running upstream reset_work reports wire failure after close. */
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
			usbhid_report_request_device_reset(hid, index, generation,
							   revision, true);
		} else if (park) {
			/* Local submit starvation never reached usb_clear_halt()'s wire. */
			usbhid_backend_rx_rearm_failed();
		}
		return;
	}
	if (usbhid_report_queue_reconcile(hid, generation))
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
	struct usbhid_report_start_call call = { 0 };
	struct usbhid_device *usbhid;
	bool done;
	bool defer;
	int slot;
	int ret;

	if (!hid)
		return -ENODEV;
	/* Waiting in either TinyUSB owner would deadlock its own host continuation. */
	if (!hid_async_sync_call_allowed())
		return -EAGAIN;
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

	if (defer) {
		call.hid = hid;
		call.waiter = xTaskGetCurrentTaskHandle();
		/* Notification is an edge; the stack-owned done bit is the predicate. */
		(void)ulTaskNotifyTake(pdTRUE, 0);
		usbh_defer_func(usbhid_report_start_on_host, &call, false);
		do {
			hid_transport_lock();
			done = call.done;
			hid_transport_unlock();
			if (!done)
				(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		} while (!done);
	}
	/*
	 * Upstream returns usb_submit_urb()'s first result. The host pass above has
	 * already published an arm failure into the report recovery state, so zero
	 * here means that asynchronous ownership was established and ordered before
	 * the caller's resume drain.
	 */
	return 0;
}

void usbhid_report_close(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	TaskHandle_t task;
	bool always_poll;
	bool close_idle;
	bool defer = false;

	if (!hid)
		return;
	usbhid = hid->driver_data;
	task = xTaskGetCurrentTaskHandle();
	always_poll = hid->quirks & HID_QUIRK_ALWAYS_POLL;

	hid_transport_lock();
	/* Mirrors upstream's atomic HID_OPENED/HID_IN_POLLING close transition. */
	usbhid->report_open_state = USBHID_REPORT_CLOSED;
	if (always_poll) {
		hid_transport_unlock();
		return;
	}
	/* usbhid->mutex serializes the sole close waiter before this publication. */
	usbhid->report_close_waiter = task;
	usbhid->report_wanted = false;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	close_idle = usbhid_report_close_idle_locked(hid);
	hid_transport_unlock();
	defer |= usbhid_report_cancel_recovery(hid);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);

	/* usb_kill_urb() does not return while giveback/parser still owns IN. */
	while (!close_idle) {
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		hid_transport_lock();
		close_idle = usbhid_report_close_idle_locked(hid);
		hid_transport_unlock();
	}

	/* Relay an idle edge hidden from the broader teardown/hid_hw_wait waiter. */
	hid_transport_lock();
	usbhid->report_close_waiter = NULL;
	if (usbhid_report_idle_locked(hid))
		usbhid_wait_wake_locked(hid);
	hid_transport_unlock();
}

/*
 * A logical driver rebuild must close interrupt-IN before it takes the parser
 * lock, but unlike ordinary close it must parse a completion which already won
 * the host abort race against the still-live input graph. Full hid_hw_stop()
 * follows after the caller owns that lock.
 *
 * The usbhid lifecycle mutex serializes this waiter with open/close/start/stop.
 */
int usbhid_report_quiesce_rebuild(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	wait_queue_head_t *protocol_wait;
	TaskHandle_t task;
	bool close_idle;
	bool rebuild_resume;
	bool reset_pending;
	bool defer = false;
	int ret = 0;

	if (!hid)
		return -ENODEV;
	usbhid = hid->driver_data;
	task = xTaskGetCurrentTaskHandle();

	hid_transport_lock();
	if (usbhid->disconnect_queued || usbhid->transport_stopping) {
		hid_transport_unlock();
		return -ENODEV;
	}

	rebuild_resume =
		usbhid->report_open_state == USBHID_REPORT_OPEN;
	usbhid->report_rebuild_state = USBHID_REPORT_REBUILD_DRAINING;
	usbhid->report_close_waiter = task;
	usbhid->report_wanted = false;
	/* Keep open_state/revision stable until the last accepted report parses. */
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	hid_transport_unlock();
	usbhid_report_notify_task();
	defer |= usbhid_report_cancel_recovery(hid);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);

	for (;;) {
		int index;

		hid_transport_lock();
		close_idle = usbhid_report_close_idle_locked(hid);
		index = usbhid->report_slot ? usbhid->report_slot - 1 : -1;
		reset_pending = index >= 0 && index < CFG_TUH_HID &&
			usbhid_report_rx_slots[index].owner == hid &&
			usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_DEVICE_RESET &&
			!usbhid->report_host_pending;
		hid_transport_unlock();
		if (close_idle || reset_pending)
			break;
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}

	hid_transport_lock();
	usbhid->report_close_waiter = NULL;
	if (reset_pending) {
		usbhid->report_rebuild_state = USBHID_REPORT_REBUILD_IDLE;
		hid_transport_unlock();
		return -EAGAIN;
	}
	usbhid->transport_stopping = true;
	for (protocol_wait = usbhid->protocol_waits; protocol_wait;
	     protocol_wait = protocol_wait->transport_next)
		hid_compat_waitqueue_cancel(protocol_wait);
	usbhid->report_open_state = USBHID_REPORT_CLOSED;
	usbhid->report_revision++;
	if (usbhid->disconnect_queued) {
		usbhid->report_rebuild_state = USBHID_REPORT_REBUILD_IDLE;
		ret = -ENODEV;
	} else {
		usbhid->report_rebuild_state =
			rebuild_resume ? USBHID_REPORT_REBUILD_RESUME_PENDING :
				USBHID_REPORT_REBUILD_IDLE;
	}
	if (usbhid_report_idle_locked(hid))
		usbhid_wait_wake_locked(hid);
	hid_transport_unlock();

	return ret;
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
		event->opened_at_completion = slot->opened_at_completion;
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
	 * The ordinary HID GET completion budget has five queue slots. GETs owned
	 * by the lifecycle task bypass them, so their parser cannot block
	 * interrupt-IN or its host fence while driver_input_lock remains held.
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
	wait_queue_head_t *protocol_wait;
	bool defer = false;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	hid_transport_lock();
	usbhid->transport_stopping = true;
	for (protocol_wait = usbhid->protocol_waits; protocol_wait;
	     protocol_wait = protocol_wait->transport_next)
		hid_compat_waitqueue_cancel(protocol_wait);
	usbhid->report_wanted = false;
	usbhid->report_open_state = USBHID_REPORT_CLOSED;
	usbhid->report_revision++;
	if (usbhid->report_owner == USBHID_REPORT_ARMED &&
	    !usbhid->report_host_pending) {
		usbhid->report_host_pending = true;
		defer = true;
	}
	hid_transport_unlock();
	usbhid_report_notify_task();
	(void)usbhid_report_cancel_recovery(hid);

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
}

void usbhid_report_unplug(struct hid_device *hid)
{
	struct usbhid_device *usbhid;
	wait_queue_head_t *protocol_wait;
	int slot;

	if (!hid)
		return;
	usbhid = hid->driver_data;

	hid_transport_lock();
	usbhid->transport_stopping = true;
	for (protocol_wait = usbhid->protocol_waits; protocol_wait;
	     protocol_wait = protocol_wait->transport_next)
		hid_compat_waitqueue_cancel(protocol_wait);
	usbhid->report_wanted = false;
	usbhid->report_open_state = USBHID_REPORT_CLOSED;
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
	// (void)usbhid_report_cancel_recovery(hid, false);
	// TinyUSB unmount callback cannot enter even the non-waiting timer wheel:
	// it publishes stopping above, and the report task cancels that retry.
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
	if (slot >= 0)
		timer_delete_sync(&usbhid_report_retries[slot].io_retry);

	hid_transport_lock();
	if (slot >= 0) {
		/* Erase only after every HCD/task owner released the slot. */
		usbhid_report_recovery[slot] = USBHID_REPORT_RECOVERY_NONE;
		usbhid_report_clear_halt_cancel_admission_locked(slot);
		memset(&usbhid_report_rx_slots[slot], 0,
		       sizeof(usbhid_report_rx_slots[slot]));
		usbhid_report_retries[slot].stop_retry = 0;
		usbhid_report_retries[slot].retry_delay = 0;
		usbhid_report_retries[slot].clear_halt_deadline = 0;
	}
	usbhid->report_slot = 0;
	usbhid->report_bufsize = 0;
	hid_transport_unlock();
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
// its host owner. The current PIO HCD reserves one of its fixed endpoint slots
// at class open and has no runtime bandwidth scheduler, so a successfully
// opened interrupt endpoint cannot later return Linux's -ENOSPC here. The
// typed firmware submit adapter still preserves -ENODEV/-EBUSY/EPIPE/timeout;
// a future scheduled HCD must add an explicit -ENOSPC result at that adapter.
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
			if (usbhid_report_queue_reconcile(hid, generation))
				return;

			/* The report slot no longer owns a safe host continuation. */
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
			 * usb_clear_halt() resets the host toggle after the remote request.
			 * The SHA-pinned PIO HCD now owns that controller-specific step.
			 */
			ok = tuh_hid_mounted(usbhid->dev_addr, usbhid->instance) &&
			     hcd_edpt_clear_stall(
				usbh_get_rhport(usbhid->dev_addr),
				usbhid->dev_addr, ep_addr);

			hid_transport_lock();
			device_reset = false;
			if (index >= 0 && index < CFG_TUH_HID &&
			    usbhid_report_rx_slots[index].owner == hid &&
			    usbhid_report_rx_slots[index].generation == generation &&
			    usbhid_report_recovery[index] ==
				USBHID_REPORT_RECOVERY_RESET_DATA_TOGGLE) {
				if (!ok && !usbhid->transport_stopping) {
					usbhid_report_recovery[index] =
						USBHID_REPORT_RECOVERY_DEVICE_RESET;
					revision = usbhid->report_revision;
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
								   generation,
								   revision,
								   true);
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
		// publishes the old URB error edge; the report task owns hid_io_error().
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
		if (index < 0 || index >= CFG_TUH_HID ||
		    usbhid_report_rx_slots[index].owner != hid ||
		    usbhid->generation != generation) {
			/* The failed submit no longer belongs to a live report epoch. */
			usbhid->report_wanted = false;
			usbhid->report_host_pending = false;
			waiter = usbhid_report_idle_waiter_locked(hid);
			hid_transport_unlock();
			usbhid_backend_rx_rearm_failed();
			if (waiter)
				xTaskNotifyGive(waiter);
			return;
		}
		if (usbhid->report_revision != revision) {
			/* close/reopen won; retry only the new open epoch. */
			hid_transport_unlock();
			continue;
		}
		/* !mounted short-circuits arm_on_host(), so publish its epoch here. */
		usbhid_report_rx_slots[index].generation = generation;
		usbhid_report_rx_slots[index].revision = revision;
		usbhid_report_recovery[index] =
			USBHID_REPORT_RECOVERY_IO_ERROR_PENDING;
		hid_transport_unlock();
		usbhid_report_notify_task();
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

	memset(pending, 0, sizeof(pending));
	hid_transport_lock();
	for (int i = 0; i < CFG_TUH_HID; i++) {
		if (!usbhid_report_rx_slots[i].reconcile_pending)
			continue;
		pending[i].hid = usbhid_report_rx_slots[i].owner;
		pending[i].generation = usbhid_report_rx_slots[i].generation;
		usbhid_report_rx_slots[i].reconcile_pending = false;
		have_pending = true;
	}
	hid_transport_unlock();

	if (!have_pending)
		return false;

	/*
	 * PIO's alarm IRQ and every HCD abort run on CORE1. The host task therefore
	 * cannot resume until a completion which raced abort has been published.
	 * Queueing this one-shot continuation immediately puts it after that event
	 * in TinyUSB's FIFO; no SOF polling is needed.
	 */
	for (int i = 0; i < CFG_TUH_HID; i++) {
		struct usbhid_device *pending_usbhid;
		bool reconcile;

		if (!pending[i].hid)
			continue;
		hid_transport_lock();
		reconcile = usbhid_report_rx_slots[i].owner == pending[i].hid &&
			    usbhid_report_rx_slots[i].generation ==
				pending[i].generation;
		pending_usbhid = reconcile ? pending[i].hid->driver_data : NULL;
		reconcile = pending_usbhid &&
			    pending[i].generation == pending_usbhid->generation &&
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

/*
 * Upstream reset_work blocks inside usb_clear_halt() and needs no allocation
 * deadline wake. A saturated fixed broker instead sleeps this report task
 * until either a capacity notification or the preserved absolute local bound.
 */
static TickType_t usbhid_report_clear_halt_wait_timeout(void)
{
	TickType_t timeout = portMAX_DELAY;
	unsigned long now;

	hid_transport_lock();
	now = jiffies;
	for (int i = 0; i < CFG_TUH_HID; i++) {
		TickType_t remaining;

		if (usbhid_report_recovery[i] !=
		    USBHID_REPORT_RECOVERY_CLEAR_HALT_WAIT_SLOT)
			continue;
		remaining = time_after_eq(
			now, usbhid_report_retries[i].clear_halt_deadline) ?
			0 : (TickType_t)(
				usbhid_report_retries[i].clear_halt_deadline - now);
		if (remaining < timeout)
			timeout = remaining;
	}
	hid_transport_unlock();
	return timeout;
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
		bool defer = false;
		bool io_error = false;
		bool payload_valid;
		bool completion_owned;
		bool protocol_only;
		bool status_current;
		bool transfer_failed;
		bool process;
		int index;

		/* Admission and report work share one coalesced wake edge. */
		if (ulTaskNotifyTake(pdTRUE, 0))
			usbhid_report_admission_wake();

		/* TinyUSB callback only published stop; task owns timer cancellation. */
		if (usbhid_report_process_stopping_recovery())
			continue;

		/* Host owner publishes failed arm; report task owns retry policy. */
		if (usbhid_report_process_io_error())
			continue;

		/* Callback ingress publishes this durable predicate before its wake. */
		if (usbhid_report_process_detach_fence())
			continue;

		/* Host abort/rearm fences remain globally prior to both report lanes. */
		if (usbhid_report_process_reconcile())
			continue;

		/* Linux reset_work is a durable predicate, not a periodic poll. */
		if (usbhid_report_process_clear_halt())
			continue;

		if (prefer_control && usbhid_control_report_process_one()) {
			prefer_control = false;
			continue;
		}

		if (!usbhid_input_report_take(&event)) {
			TickType_t wait_timeout;

			if (!prefer_control && usbhid_control_report_process_one()) {
				prefer_control = false;
				continue;
			}
			if (usbhid_report_process_reconcile())
				continue;
			wait_timeout = usbhid_report_clear_halt_wait_timeout();
			if (!wait_timeout)
				continue;
			/* Every queue/state above is durable; notification is only its edge. */
			if (ulTaskNotifyTake(pdTRUE, wait_timeout))
				usbhid_report_admission_wake();
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
			!usbhid->transport_stopping &&
			(usbhid->report_wanted ||
			 usbhid->report_rebuild_state ==
				USBHID_REPORT_REBUILD_DRAINING);
		/*
		 * Linux cannot deliver to an eventX fd before userspace opens it.
		 * Firmware's first input_open_device() starts one HID endpoint shared
		 * by all sibling input_dev objects, so keep parsing behind the final
		 * post-probe activation publication. Transport completion/recovery and
		 * rearm remain live while this parser-only gate is closed.
		 */
		process = status_current && usbhid->driver_ready;
		/*
		 * hid_device_io_start() lets an upstream protocol driver receive
		 * replies during probe. Keep ordinary field/input parsing behind
		 * driver_ready, but let the driver's explicit probe gate enter only
		 * its raw_event matcher.
		 */
		protocol_only = status_current && !usbhid->driver_ready &&
				usbhid->probe_raw_event;
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
		if (completion_owned &&
		    event.xfer_result == XFER_RESULT_SUCCESS &&
		    index >= 0 && index < CFG_TUH_HID &&
		    usbhid_report_rx_slots[index].owner == event.hid)
			usbhid_report_retries[index].retry_delay = 0;
		// 	if (!test_bit(HID_OPENED, &usbhid->iofl))
		// 		break;
		// The completion snapshot mirrors upstream's HID_OPENED and
		// HID_RESUME_RUNNING tests without racing deferred parsing against open.
		if (!event.opened_at_completion) {
			process = false;
			protocol_only = false;
		}
		// 	usbhid_mark_busy(usbhid);
		// Runtime-PM busy tracking is absent at this firmware boundary.
		hid_transport_unlock();

		// 	if (!test_bit(HID_RESUME_RUNNING, &usbhid->iofl)) {
		// 		hid_safe_input_report(urb->context, HID_INPUT_REPORT,
		// 			      urb->transfer_buffer, urb->transfer_buffer_length,
		// 			      urb->actual_length, 1);
		// TinyUSB supplies the stable slot and exact actual length. RESUMING
		// completions were snapshotted closed above, preserving this suppression.
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
		else if (protocol_only &&
			 event.xfer_result == XFER_RESULT_SUCCESS &&
			 payload_valid)
			(void)hid_safe_raw_event_only(event.hid,
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
					usbhid_report_retries[index].clear_halt_deadline =
						jiffies + msecs_to_jiffies(
							USBHID_CLEAR_HALT_ADMISSION_TIMEOUT_MS);
					usbhid->report_host_pending = true;
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
				if (index >= 0 && index < CFG_TUH_HID &&
				    usbhid_report_rx_slots[index].owner == event.hid) {
					usbhid_report_rx_slots[index].generation =
						event.generation;
					usbhid_report_rx_slots[index].revision =
						event.revision;
					usbhid_report_recovery[index] =
						USBHID_REPORT_RECOVERY_IO_ERROR_PENDING;
					usbhid->report_host_pending = true;
					io_error = true;
				}
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

		if (io_error)
			hid_io_error(event.hid);
		else if (defer)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					event.hid, false);
		if (waiter)
			xTaskNotifyGive(waiter);
	}
}
