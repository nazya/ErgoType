#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "host/hcd.h"
#include "host/hub.h"
#include "host/usbh_pvt.h"
#include "pio_usb.h"

#include "hid_async.h"
#include "stdio_tusb_cdc.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"

/*
 * Upstream Linux HID transport can block in hid_hw_wait(), usb_control_msg(),
 * and related request paths. TinyUSB host callbacks cannot wait for another
 * TinyUSB callback to complete, so this firmware bridge owns bounded physical-
 * device control/OUT lanes in one executor task and resumes task-side ported
 * call sites through explicit completions.
 */

#define HID_ASYNC_SUBMIT_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
/* Match upstream usbhid's five-second watchdog for active ctrl/out I/O. */
#define HID_ASYNC_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(USB_CTRL_SET_TIMEOUT)
/* PIO-USB can publish an aborted completion at the end of a later SOF. */
#define HID_ASYNC_ABORT_DRAIN_FRAMES 2u
/* TinyUSB control transfers contain SETUP, optional DATA, then ACK. */
#define HID_ASYNC_EP0_DRAIN_MAX 3u
#define HID_ASYNC_DEVICE_ADDR_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
#define HID_ASYNC_NORMAL_SLOT_COUNT \
	(HID_ASYNC_REQUEST_QUEUE_LEN + 1u + CFG_TUH_HID)
#define HID_ASYNC_RECOVERY_SLOT_COUNT 1u
#define HID_ASYNC_SLOT_COUNT \
	(HID_ASYNC_NORMAL_SLOT_COUNT + HID_ASYNC_RECOVERY_SLOT_COUNT)
#define HID_ASYNC_NOTIFY_INDEX 1u

_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > HID_ASYNC_NOTIFY_INDEX,
	       "HID async executor needs notification index 1");
_Static_assert(HID_ASYNC_SLOT_COUNT <= UINT8_MAX,
	       "HID async one-based FIFO links must fit in u8");

enum hid_async_host_action {
	HID_ASYNC_HOST_FENCE,
	HID_ASYNC_HOST_SUBMIT,
	HID_ASYNC_HOST_ABORT,
	HID_ASYNC_HOST_RECOVER_EP0,
};

struct hid_async_host_call {
	TaskHandle_t waiter;
	struct hid_async_request *request;
	u8 dev_addr;
	u8 instance;
	u8 ep_addr;
	u32 generation;
	u32 serial;
	enum hid_async_host_action action;
	int status;
	volatile bool done;
};

enum hid_async_lane {
	HID_ASYNC_LANE_DEVICE_CTRL,
	HID_ASYNC_LANE_DEVICE_OUT,
};

enum hid_async_slot_state {
	HID_ASYNC_SLOT_FREE,
	HID_ASYNC_SLOT_QUEUED,
	HID_ASYNC_SLOT_SUBMIT_PENDING,
	HID_ASYNC_SLOT_ACTIVE,
	HID_ASYNC_SLOT_RETIRING,
	/* Pins HUB_RESET metadata across its immediate host-owner callback. */
	HID_ASYNC_SLOT_HOST_COMPLETING,
	HID_ASYNC_SLOT_COMPLETING,
	HID_ASYNC_SLOT_WAIT_PARSE,
	HID_ASYNC_SLOT_RELEASE_PENDING,
};

struct hid_async_slot {
	struct hid_async_request req;
	u8 next;
	u8 lane;
	u8 state;
	bool accepting_completion;
	bool completion_ready;
	bool cancel_requested;
	bool submit_started;
	/* True when logical enqueue happened after the global EP0 gate closed. */
	bool gate_parked;
	int completion_status;
	TickType_t queued_at;
	TickType_t submit_start;
	TickType_t retry_at;
	TickType_t xfer_start;
};

static struct hid_async_slot *hid_async_slots;
static TaskHandle_t hid_async_task_handle;
static TaskHandle_t hid_async_host_task_handle;
/* Bounded device-level lanes avoid per-device transport queues. */
static u8 hid_async_device_ctrl_head;
static u8 hid_async_device_ctrl_tail;
static u8 hid_async_device_out_head;
static u8 hid_async_device_out_tail;
static u8 hid_async_ctrl_active;
/* Coordinated remove/re-enumeration temporarily owns TinyUSB's global EP0. */
static bool hid_async_control_gate;
static u8 hid_async_schedule_cursor;
static u32 hid_async_serial;
static u32 hid_async_device_generation[HID_ASYNC_DEVICE_ADDR_MAX + 1];

static int hid_async_submit(struct hid_async_request *req);
static void hid_async_xfer_complete(tuh_xfer_t *xfer);
static void hid_async_host_call_sync(struct hid_async_host_call *call);
static void hid_async_notify_task(void);

/* SHA-pinned TinyUSB host-owner extension generated in the build directory. */
int usbh_port_control_recover_on_host(uint8_t daddr,
				      tuh_xfer_cb_t complete_cb,
				      uintptr_t user_data);

static bool hid_async_lane_is_out(enum hid_async_lane lane)
{
	return lane == HID_ASYNC_LANE_DEVICE_OUT;
}

static bool hid_async_request_bypasses_control_gate(
		const struct hid_async_request *req)
{
	return req->kind == HID_ASYNC_REQUEST_HUB_RESET;
}

static bool hid_async_slot_parked_by_control_gate(
		const struct hid_async_slot *slot)
{
	return hid_async_control_gate &&
	       !hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
	       !hid_async_request_bypasses_control_gate(&slot->req);
}

static struct hid_async_slot *hid_async_slot_for_request_locked(
		struct hid_async_request *req)
{
	if (!hid_async_slots || !req)
		return NULL;
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (&hid_async_slots[i].req == req)
			return &hid_async_slots[i];
	}
	return NULL;
}

static void hid_async_call_on_host(void *data)
{
	struct hid_async_host_call *call = data;
	struct hid_async_slot *slot;
	TaskHandle_t waiter = call->waiter;
	bool logical_aborted;
	bool physical_aborted;
	bool gated = false;
	bool is_current = false;
	TaskHandle_t host_task = xTaskGetCurrentTaskHandle();

	taskENTER_CRITICAL();
	configASSERT(!hid_async_host_task_handle ||
		     hid_async_host_task_handle == host_task);
	hid_async_host_task_handle = host_task;
	taskEXIT_CRITICAL();

	/*
	 * TinyUSB submission and generation-less abort both run in the host owner.
	 * They may only target the request which asked for this call. A physical
	 * unmount advances the generation first; class/HCD close then owns the old
	 * transfer and an abort must not hit a newly reused USB address.
	 */
	taskENTER_CRITICAL();
	slot = hid_async_slot_for_request_locked(call->request);
	if (slot && call->dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX &&
	    slot->req.serial == call->serial &&
	    slot->req.dev_addr == call->dev_addr &&
	    slot->req.instance == call->instance &&
	    slot->req.generation == call->generation &&
	    hid_async_device_generation[call->dev_addr] == call->generation) {
		if (call->action == HID_ASYNC_HOST_SUBMIT)
			is_current = slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING &&
				     !slot->cancel_requested &&
				     (!slot->req.hid ||
				      !((struct usbhid_device *)
					slot->req.hid->driver_data)->transport_stopping);
		else if (call->action == HID_ASYNC_HOST_ABORT ||
			 call->action == HID_ASYNC_HOST_RECOVER_EP0)
			is_current = slot->state == HID_ASYNC_SLOT_RETIRING &&
				     !slot->completion_ready;
		if (is_current && call->action == HID_ASYNC_HOST_SUBMIT)
			gated = hid_async_slot_parked_by_control_gate(slot);
	}
	taskEXIT_CRITICAL();

	call->status = call->action == HID_ASYNC_HOST_FENCE ? 0 : -ENODEV;
	if (call->action == HID_ASYNC_HOST_SUBMIT) {
		/* A gate which raced the defer parks this request for release. */
		call->status = gated ? -EAGAIN :
			       is_current ? hid_async_submit(call->request) :
					    -ENODEV;
	} else if (call->action == HID_ASYNC_HOST_ABORT && is_current) {
		call->status = 0;
		if (call->ep_addr) {
			(void)tuh_edpt_abort_xfer(call->dev_addr, call->ep_addr);
		} else {
			/*
			 * TinyUSB owns one global logical EP0 transfer. Its public abort
			 * releases that owner immediately, but PIO may already have
			 * published completion for the current SETUP/DATA/ACK stage. Keep
			 * TinyUSB's old owner until the HCD proves that it canceled an
			 * active stage without publishing completion. A raced completion
			 * remains FIFO-ahead of the next host pass and may safely advance
			 * the old logical transfer instead of being applied to a new one.
			 */
			physical_aborted = hcd_edpt_abort_xfer(
				usbh_get_rhport(call->dev_addr), call->dev_addr, 0);
			if (!physical_aborted) {
				call->status = -EAGAIN;
			} else {
				logical_aborted = tuh_edpt_abort_xfer(call->dev_addr, 0);
				/* The validated old owner must still be the global EP0. */
				configASSERT(logical_aborted);
				(void)logical_aborted;
			}
		}
	} else if (call->action == HID_ASYNC_HOST_RECOVER_EP0 && is_current) {
		call->status = usbh_port_control_recover_on_host(
			call->dev_addr, hid_async_xfer_complete,
			(uintptr_t)call->serial);
	}
	taskENTER_CRITICAL();
	call->done = true;
	taskEXIT_CRITICAL();
	/* Publishing done releases the stack-owned call; do not touch it again. */
	xTaskNotifyGive(waiter);
}

static u8 hid_async_tinyusb_report_type(enum hid_report_type type)
{
	return (u8)type + 1;
}

static bool hid_async_report_type_valid(enum hid_report_type type)
{
	return type >= HID_INPUT_REPORT && type < HID_REPORT_TYPES;
}

static void hid_async_notify_task(void)
{
	TaskHandle_t task;

	taskENTER_CRITICAL();
	task = hid_async_task_handle;
	taskEXIT_CRITICAL();
	if (task)
		xTaskNotifyGiveIndexed(task, HID_ASYNC_NOTIFY_INDEX);
}

void hid_async_host_task_register(void)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();

	/* TinyUSB host callbacks and tuh_task() share this single owner task. */
	taskENTER_CRITICAL();
	configASSERT(!hid_async_host_task_handle ||
		     hid_async_host_task_handle == task);
	hid_async_host_task_handle = task;
	taskEXIT_CRITICAL();
}

bool hid_async_sync_call_allowed(void)
{
	TaskHandle_t caller;
	TaskHandle_t executor;
	TaskHandle_t host;

	if (xPortIsInsideInterrupt() ||
	    xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
		return false;
	caller = xTaskGetCurrentTaskHandle();

	taskENTER_CRITICAL();
	executor = hid_async_task_handle;
	host = hid_async_host_task_handle;
	taskEXIT_CRITICAL();

	/* Either owner would wait for work which only that same task can advance. */
	return caller != executor && caller != host;
}

int hid_async_device_epoch_snapshot(u8 dev_addr, u32 *generation)
{
	if (!hid_async_slots || !generation || !dev_addr ||
	    dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	*generation = hid_async_device_generation[dev_addr];
	taskEXIT_CRITICAL();
	return 0;
}

static void hid_async_drain_abort_frames(void)
{
	u32 start = pio_usb_host_get_frame_number();

	/* FreeRTOS ticks and the independent PIO SOF timer need not share phase. */
	while (pio_usb_host_get_frame_number() - start <
	       HID_ASYNC_ABORT_DRAIN_FRAMES)
		vTaskDelay(1);
}

static struct hid_async_slot *hid_async_slot_from_id(u8 id)
{
	if (!id || id > HID_ASYNC_SLOT_COUNT)
		return NULL;
	return &hid_async_slots[id - 1u];
}

static u8 hid_async_slot_id(const struct hid_async_slot *slot)
{
	return (u8)(slot - hid_async_slots) + 1u;
}

static void hid_async_slot_fifo_locked(struct hid_async_slot *slot,
				       u8 **head, u8 **tail)
{
	if (slot->lane == HID_ASYNC_LANE_DEVICE_CTRL) {
		*head = &hid_async_device_ctrl_head;
		*tail = &hid_async_device_ctrl_tail;
		return;
	}
	configASSERT(slot->lane == HID_ASYNC_LANE_DEVICE_OUT);
	*head = &hid_async_device_out_head;
	*tail = &hid_async_device_out_tail;
}

static int hid_async_slot_queue_locked(const struct hid_async_request *req,
				       enum hid_async_lane lane)
{
	struct hid_async_slot *slot = NULL;
	struct hid_async_slot *tail_slot;
	u8 first = req->kind == HID_ASYNC_REQUEST_HUB_RESET ?
		HID_ASYNC_NORMAL_SLOT_COUNT : 0;
	u8 end = req->kind == HID_ASYNC_REQUEST_HUB_RESET ?
		HID_ASYNC_SLOT_COUNT : HID_ASYNC_NORMAL_SLOT_COUNT;
	u8 *head;
	u8 *tail;
	u8 id;

	/* The recovery slot cannot be retained by ordinary parser/control work. */
	for (u8 i = first; i < end; i++) {
		if (hid_async_slots[i].state == HID_ASYNC_SLOT_FREE) {
			slot = &hid_async_slots[i];
			break;
		}
	}
	if (!slot)
		return -EBUSY;

	memset(slot, 0, sizeof(*slot));
	slot->req = *req;
	slot->lane = (u8)lane;
	slot->state = HID_ASYNC_SLOT_QUEUED;
	slot->queued_at = xTaskGetTickCount();
	slot->gate_parked = hid_async_control_gate &&
		!hid_async_lane_is_out(lane) &&
		!hid_async_request_bypasses_control_gate(req);
	id = hid_async_slot_id(slot);
	hid_async_slot_fifo_locked(slot, &head, &tail);
	if (*tail) {
		tail_slot = hid_async_slot_from_id(*tail);
		configASSERT(tail_slot);
		configASSERT(!tail_slot->next);
		tail_slot->next = id;
	} else {
		configASSERT(!*head);
		*head = id;
	}
	*tail = id;
	return 0;
}

static bool hid_async_slot_is_head_locked(struct hid_async_slot *slot)
{
	struct hid_async_slot *previous;
	u8 *head;
	u8 *tail;
	u8 cursor;

	hid_async_slot_fifo_locked(slot, &head, &tail);
	(void)tail;
	if (slot->lane != HID_ASYNC_LANE_DEVICE_CTRL &&
	    slot->lane != HID_ASYNC_LANE_DEVICE_OUT)
		return *head == hid_async_slot_id(slot);

	/*
	 * Linux USB core orders URBs per device endpoint, not across unrelated
	 * devices. The fixed pool uses one control list and one OUT list for HID
	 * and generic requests, so skip older entries with a different physical
	 * ordering key instead of imposing a firmware-only global head-of-line
	 * block. A completed GET_REPORT remains stored through hid_ctrl()-shaped
	 * parsing but no longer owns EP0; retain only usbhid's same-interface
	 * control-report order while letting generic or sibling requests pass it.
	 */
	cursor = *head;
	while (cursor && cursor != hid_async_slot_id(slot)) {
		previous = hid_async_slot_from_id(cursor);
		configASSERT(previous);
		if (previous->state == HID_ASYNC_SLOT_WAIT_PARSE ||
		    previous->state == HID_ASYNC_SLOT_RELEASE_PENDING) {
			if (previous->state == HID_ASYNC_SLOT_WAIT_PARSE &&
			    slot->req.kind == HID_ASYNC_REQUEST_REPORT &&
			    previous->req.kind == HID_ASYNC_REQUEST_REPORT &&
			    previous->req.hid == slot->req.hid)
				return false;
			cursor = previous->next;
			continue;
		}
		/* HUB_RESET owns EP0 while older normal control work is gated. */
		if (hid_async_request_bypasses_control_gate(&slot->req) &&
		    hid_async_slot_parked_by_control_gate(previous)) {
			cursor = previous->next;
			continue;
		}
		if (previous->req.dev_addr == slot->req.dev_addr &&
		    previous->req.generation == slot->req.generation &&
		    (slot->lane == HID_ASYNC_LANE_DEVICE_CTRL ||
		     previous->req.ep_addr == slot->req.ep_addr))
			return false;
		cursor = previous->next;
	}
	configASSERT(cursor == hid_async_slot_id(slot));
	return true;
}

static u8 *hid_async_slot_release_locked(struct hid_async_slot *slot)
{
	struct hid_async_slot *previous_slot = NULL;
	u8 *owned_data = slot->req.data_owned ? slot->req.data : NULL;
	u8 *head;
	u8 *tail;
	u8 previous = 0;
	u8 cursor;
	u8 id = hid_async_slot_id(slot);

	hid_async_slot_fifo_locked(slot, &head, &tail);
	cursor = *head;
	while (cursor && cursor != id) {
		previous = cursor;
		previous_slot = hid_async_slot_from_id(cursor);
		configASSERT(previous_slot);
		cursor = previous_slot->next;
	}
	configASSERT(cursor == id);
	if (previous_slot)
		previous_slot->next = slot->next;
	else
		*head = slot->next;
	if (*tail == id)
		*tail = previous;
	configASSERT(!!*head == !!*tail);
	memset(slot, 0, sizeof(*slot));
	return owned_data;
}

int hid_async_init(void)
{
	size_t slots_size = sizeof(*hid_async_slots) * HID_ASYNC_SLOT_COUNT;

	/*
	 * Linux USB core orders transfers by physical endpoint. One shared fixed
	 * pool owns request metadata. Its control and OUT lists use per-device/per-
	 * endpoint head scans; queued SET_REPORT payloads retain exact upstream-
	 * style snapshots. Lifecycle owns descriptor scratch outside this executor.
	 */
	hid_async_slots = pvPortMalloc(slots_size);
	if (!hid_async_slots)
		return -ENOMEM;
	memset(hid_async_slots, 0, slots_size);
	hid_async_schedule_cursor = HID_ASYNC_SLOT_COUNT - 1u;
	hid_async_control_gate = false;

	return 0;
}

/*
 * report_stop() publishes usbhid->transport_stopping under the same critical
 * section. Therefore a request is either fully queued before teardown starts
 * or rejected after it; cancellation plus the durable slot scan drains the
 * former set.
 */
static int hid_async_queue_hid_request(struct hid_async_request *req)
{
	struct usbhid_device *usbhid;
	enum hid_async_lane lane;
	int ret = 0;

	if (!hid_async_slots)
		return -ENODEV;

	taskENTER_CRITICAL();
	if (!req->hid)
		ret = -ENODEV;
	else {
		usbhid = req->hid->driver_data;
		if (usbhid->transport_stopping)
			ret = -ENODEV;
		else if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
			ret = -ENODEV;
		else {
			req->generation =
				hid_async_device_generation[req->dev_addr];
			/*
			 * Linux USB core orders all submissions to the same physical
			 * endpoint, regardless of which HID hook created them.
			 */
			lane = req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT ?
				HID_ASYNC_LANE_DEVICE_OUT :
				HID_ASYNC_LANE_DEVICE_CTRL;
			ret = hid_async_slot_queue_locked(req, lane);
		}
	}
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   u8 *data, u16 data_size,
			   hid_async_complete_t complete, void *context)
{
	struct usb_device *dev;
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	bool interrupt_out;
	u32 maxpacket;
	u32 len;
	int ret;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid || !report || !hid_async_report_type_valid(report->type))
		return -EINVAL;

	if (reqtype != HID_REQ_GET_REPORT && reqtype != HID_REQ_SET_REPORT)
		return -ENOSYS;

	len = hid_report_len(report);
	dev = hid_to_usb_dev(hid);
	usbhid = hid->driver_data;
	if (reqtype == HID_REQ_GET_REPORT) {
		/* Match upstream hid_submit_ctrl() EP0 receive sizing. */
		maxpacket = dev->descriptor.bMaxPacketSize0;
		if (!maxpacket)
			maxpacket = 8;
		len += !len;
		len = DIV_ROUND_UP(len, maxpacket) * maxpacket;
		len = min_t(u32, len, usbhid->bufsize);
		if (len > data_size || (len && !data))
			return -EMSGSIZE;
	}

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.report = report;
	req.reqtype = reqtype;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = report->id;
	req.report_type = hid_async_tinyusb_report_type(report->type);
	req.len = (u16)len;
	req.data = data;
	req.complete = complete;
	req.context = context;

	interrupt_out = reqtype == HID_REQ_SET_REPORT &&
			report->type == HID_OUTPUT_REPORT &&
			usbhid->usb_altsetting.has_interrupt_out &&
			usbhid->usb_altsetting.interrupt_out_endpoint;

	if (reqtype == HID_REQ_SET_REPORT) {
		/*
		 * usbhid->ctrl/out[].raw_report = hid_alloc_report_buf(report,
		 *                                                    GFP_ATOMIC);
		 * hid_output_report(report, usbhid->ctrl/out[].raw_report);
		 * TinyUSB has no Linux URB FIFO entry. Its durable slot owns the same
		 * exact enqueue-time snapshot until completion or fenced cancellation.
		 */
		req.data = hid_alloc_report_buf(report, GFP_ATOMIC);
		if (!req.data)
			return -ENOMEM;
		req.data_owned = true;
		hid_output_report(report, req.data);
		if (interrupt_out) {
			/* hid_output_report() already produced the endpoint wire image. */
			req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
			req.ep_addr =
				usbhid->usb_altsetting.interrupt_out_endpoint;
		}
	}

	ret = hid_async_queue_hid_request(&req);
	if (ret) {
		if (req.data_owned)
			kfree(req.data);
		return ret;
	}

	if (reqtype == HID_REQ_SET_REPORT)
		async_msg(interrupt_out ? "DBG: HID_REPORT_OUT_Q" :
					  "DBG: HID_REPORT_SET_Q");
	return 0;
}

/*
 * Upstream Linux USB core owns control and per-endpoint queues while task
 * callers sleep. The firmware captures the address epoch before enqueue,
 * retains the caller-owned buffer pointer, and wakes the caller after
 * task-context completion; neither TinyUSB callbacks nor the executor itself
 * may wait here.
 * A matching HID owner is non-owning slot metadata. Synchronous callers hold
 * an io_pending lease; report recovery uses its report-lifecycle barrier.
 * Teardown publishes transport_stopping and drains every owned slot before
 * destroying either object, while the owner tag makes interface stop cancel
 * work immediately instead of leaving it to the physical-device timeout.
 */
static int hid_async_queue_usb_request(struct hid_async_request *req,
				       u32 generation,
				       enum hid_async_lane lane)
{
	struct usbhid_device *usbhid;
	int ret;

	if (!hid_async_slots || !req->dev_addr ||
	    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	if (generation != hid_async_device_generation[req->dev_addr]) {
		ret = -ENODEV;
	} else if (req->hid) {
		usbhid = req->hid->driver_data;
		if (usbhid->transport_stopping ||
		    usbhid->dev_addr != req->dev_addr) {
			ret = -ENODEV;
		} else {
			req->instance = usbhid->instance;
			req->generation = generation;
			ret = hid_async_slot_queue_locked(req, lane);
		}
	} else {
		req->generation = generation;
		ret = hid_async_slot_queue_locked(req, lane);
	}
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

static u32 hid_async_timeout_from_ms(int timeout)
{
	TickType_t ticks = pdMS_TO_TICKS((u32)timeout);

	return ticks ? (u32)ticks : 1u;
}

int hid_async_control_gate_acquire(void)
{
	int ret = 0;

	if (!hid_async_slots)
		return -ENODEV;

	taskENTER_CRITICAL();
	if (hid_async_control_gate) {
		ret = -EBUSY;
	} else {
		hid_async_control_gate = true;
	}
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

bool hid_async_control_gate_idle(void)
{
	bool idle;

	if (!hid_async_slots)
		return false;

	taskENTER_CRITICAL();
	idle = hid_async_control_gate && !hid_async_ctrl_active;
	taskEXIT_CRITICAL();
	return idle;
}

void hid_async_control_gate_release(u32 paused_ticks)
{
	TickType_t now = xTaskGetTickCount();
	bool released = false;

	if (!hid_async_slots)
		return;

	taskENTER_CRITICAL();
	if (hid_async_control_gate) {
		for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
			struct hid_async_slot *slot = &hid_async_slots[i];

			if (slot->state != HID_ASYNC_SLOT_QUEUED ||
			    hid_async_lane_is_out(
				(enum hid_async_lane)slot->lane) ||
			    hid_async_request_bypasses_control_gate(&slot->req))
				continue;
			/* Gate time is not part of a parked request's watchdog. */
			if (slot->gate_parked) {
				slot->queued_at = now;
				if (slot->submit_started)
					slot->submit_start = now;
				slot->gate_parked = false;
			} else {
				slot->queued_at += (TickType_t)paused_ticks;
				if (slot->submit_started)
					slot->submit_start +=
					(TickType_t)paused_ticks;
			}
		}
		hid_async_control_gate = false;
		released = true;
	}
	taskEXIT_CRITICAL();
	if (released)
		hid_async_notify_task();
}

int hid_async_queue_hub_port_reset(u8 hub_addr, u32 generation, u8 hub_port,
				   hid_async_complete_t complete, void *context)
{
	struct hid_async_request req;

	if (!hub_port || !complete)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_HUB_RESET;
	req.dev_addr = hub_addr;
	req.hub_port = hub_port;
	req.timeout_ticks = hid_async_timeout_from_ms(USB_CTRL_SET_TIMEOUT);
	req.complete_on_cancel = true;
	req.complete = complete;
	req.context = context;
	return hid_async_queue_usb_request(&req, generation,
					   HID_ASYNC_LANE_DEVICE_CTRL);
}

int hid_async_queue_usb_control_msg(struct hid_device *owner, u8 dev_addr,
				    u32 generation,
				    u8 request, u8 requesttype,
				    u16 value, u16 index,
				    void *data, u16 size,
				    int timeout,
				    hid_async_complete_t complete,
				    void *context)
{
	struct hid_async_request req;

	if (size && !data)
		return -EINVAL;
	if (timeout <= 0)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_CONTROL;
	req.hid = owner;
	req.dev_addr = dev_addr;
	req.control_request = request;
	req.control_requesttype = requesttype;
	req.control_value = value;
	req.control_index = index;
	req.len = size;
	req.data = data;
	req.timeout_ticks = hid_async_timeout_from_ms(timeout);
	req.complete_on_cancel = true;
	req.complete = complete;
	req.context = context;
	return hid_async_queue_usb_request(&req, generation,
					   HID_ASYNC_LANE_DEVICE_CTRL);
}

int hid_async_queue_usb_interrupt_out(struct hid_device *owner, u8 dev_addr,
				      u32 generation,
				      u8 ep_addr, void *data,
				      u16 size, int timeout,
				      hid_async_complete_t complete,
				      void *context)
{
	struct hid_async_request req;

	if (!ep_addr || (ep_addr & TUSB_DIR_IN_MASK))
		return -EINVAL;
	if (size && !data)
		return -EINVAL;
	if (timeout <= 0)
		return -EINVAL;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_INTERRUPT;
	req.hid = owner;
	req.dev_addr = dev_addr;
	req.ep_addr = ep_addr;
	req.len = size;
	req.data = data;
	req.timeout_ticks = hid_async_timeout_from_ms(timeout);
	req.complete_on_cancel = true;
	req.complete = complete;
	req.context = context;
	return hid_async_queue_usb_request(&req, generation,
					   HID_ASYNC_LANE_DEVICE_OUT);
}

static int hid_async_submit_control(struct hid_async_request *req)
{
	struct usbhid_device *usbhid = req->hid->driver_data;
	u8 *data = req->data;
	u16 len = req->len;
	tusb_control_request_t const request = {
		.bmRequestType_bit = {
			.recipient = TUSB_REQ_RCPT_INTERFACE,
			.type = TUSB_REQ_TYPE_CLASS,
			.direction = req->reqtype == HID_REQ_GET_REPORT ?
				     TUSB_DIR_IN : TUSB_DIR_OUT,
		},
		.bRequest = (u8)req->reqtype,
		.wValue = tu_htole16(TU_U16(req->report_type, req->report_id)),
		.wIndex = tu_htole16((u16)
			usbhid->usb_altsetting.desc.bInterfaceNumber),
		.wLength = tu_htole16(len),
	};
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = 0,
		.setup = &request,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	return tuh_control_xfer(&xfer) ? 0 : -EAGAIN;
}

static int hid_async_submit_usb_control(struct hid_async_request *req)
{
	u8 *data = req->data;
	u16 len = req->len;
	tusb_control_request_t const request = {
		.bmRequestType = req->control_requesttype,
		.bRequest = req->control_request,
		.wValue = tu_htole16(req->control_value),
		.wIndex = tu_htole16(req->control_index),
		.wLength = tu_htole16(len),
	};
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = 0,
		.setup = &request,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	return tuh_control_xfer(&xfer) ? 0 : -EAGAIN;
}

static int hid_async_submit_hub_reset(struct hid_async_request *req)
{
	/* Match TinyUSB hub attach: SET_FEATURE(PORT_RESET) on the parent EP0. */
	return hub_port_reset(req->dev_addr, req->hub_port,
			      hid_async_xfer_complete,
			      (uintptr_t)req->serial) ? 0 : -EAGAIN;
}

static int hid_async_submit_interrupt_out(struct hid_async_request *req)
{
	u8 *data = req->data;
	u16 len = req->len;
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = req->ep_addr,
		.buflen = len,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	return tuh_edpt_xfer(&xfer) ? 0 : -EAGAIN;
}

static int hid_async_submit(struct hid_async_request *req)
{
	if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		return hid_async_submit_interrupt_out(req);
	} else if (req->kind == HID_ASYNC_REQUEST_USB_INTERRUPT) {
		return hid_async_submit_interrupt_out(req);
	} else if (req->kind == HID_ASYNC_REQUEST_USB_CONTROL) {
		return hid_async_submit_usb_control(req);
	} else if (req->kind == HID_ASYNC_REQUEST_HUB_RESET) {
		return hid_async_submit_hub_reset(req);
	} else {
		return hid_async_submit_control(req);
	}
}

static int hid_async_submit_current(struct hid_async_request *req)
{
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.request = req,
		.dev_addr = req->dev_addr,
		.instance = req->instance,
		.generation = req->generation,
		.serial = req->serial,
		.action = HID_ASYNC_HOST_SUBMIT,
		.status = -EIO,
	};

	if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	/* TinyUSB owns its global EP0 and class endpoint state in the host task. */
	hid_async_host_call_sync(&call);
	return call.status;
}

static TickType_t hid_async_request_xfer_timeout(
		const struct hid_async_request *req)
{
	if (req->timeout_ticks)
		return (TickType_t)req->timeout_ticks;
	return HID_ASYNC_XFER_TIMEOUT_TICKS;
}

static int hid_async_xfer_status(u8 result)
{
	switch (result) {
	case XFER_RESULT_SUCCESS:
		return 0;
	case XFER_RESULT_STALLED:
		return -EPIPE;
	case XFER_RESULT_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static void hid_async_host_call_sync(struct hid_async_host_call *call)
{
	bool done;

	taskENTER_CRITICAL();
	call->done = false;
	taskEXIT_CRITICAL();
	(void)ulTaskNotifyTake(pdTRUE, 0);
	usbh_defer_func(hid_async_call_on_host, call, false);
	do {
		taskENTER_CRITICAL();
		done = call->done;
		taskEXIT_CRITICAL();
		if (!done)
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	} while (!done);
}

int hid_async_cancel_device(u8 dev_addr, u8 instance)
{
	if (!hid_async_slots)
		return -ENODEV;

	/*
	 * A per-interface stop must never cancel physical-device control work or a
	 * sibling interface.
	 * Do not unlink slots or invoke continuations here: this entry is also used
	 * by the TinyUSB unmount callback. report_unplug() has already published
	 * usbhid->transport_stopping, so hid_async_task retires/completes each slot
	 * in task context. The synchronous slot scan then proves that no request
	 * retaining this HID remains before destruction.
	 */
	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state != HID_ASYNC_SLOT_FREE && slot->req.hid &&
		    slot->req.dev_addr == dev_addr &&
		    slot->req.instance == instance)
			slot->cancel_requested = true;
	}
	taskEXIT_CRITICAL();
	hid_async_notify_task();
	return 0;
}

int hid_async_cancel_device_sync(u8 dev_addr, u8 instance)
{
	bool pending;
	int ret;

	ret = hid_async_cancel_device(dev_addr, instance);
	if (ret)
		return ret;

	/*
	 * transport_stopping closes the producer side before this call. Slot state
	 * is the durable dequeue/active/completion fence; a wake edge may coalesce.
	 */
	do {
		taskENTER_CRITICAL();
		pending = false;
		for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
			struct hid_async_slot *slot = &hid_async_slots[i];

			if (slot->state != HID_ASYNC_SLOT_FREE &&
			    slot->req.hid && slot->req.dev_addr == dev_addr &&
			    slot->req.instance == instance) {
				pending = true;
				break;
			}
		}
		taskEXIT_CRITICAL();
		if (pending) {
			hid_async_notify_task();
			vTaskDelay(1);
		}
	} while (pending);

	return 0;
}

int hid_async_cancel_dev_addr(u8 dev_addr)
{
	u32 generation;

	if (!hid_async_slots)
		return -ENODEV;
	if (dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	generation = hid_async_device_generation[dev_addr];
	hid_async_device_generation[dev_addr]++;
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state != HID_ASYNC_SLOT_FREE &&
		    slot->req.dev_addr == dev_addr &&
		    slot->req.generation == generation)
			slot->cancel_requested = true;
	}
	taskEXIT_CRITICAL();
	hid_async_notify_task();
	return 0;
}

static bool hid_async_tick_reached(TickType_t now, TickType_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

static TickType_t hid_async_ticks_left(TickType_t now, TickType_t start,
				       TickType_t timeout)
{
	TickType_t elapsed = now - start;

	return elapsed >= timeout ? 0 : timeout - elapsed;
}

static bool hid_async_slot_invalid_locked(struct hid_async_slot *slot)
{
	struct usbhid_device *usbhid;

	if (slot->cancel_requested ||
	    slot->req.dev_addr > HID_ASYNC_DEVICE_ADDR_MAX ||
	    slot->req.generation !=
		hid_async_device_generation[slot->req.dev_addr])
		return true;
	if (!slot->req.hid)
		return false;
	usbhid = slot->req.hid->driver_data;
	return usbhid->transport_stopping;
}

static void hid_async_slot_clear_physical_locked(struct hid_async_slot *slot)
{
	if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
	    hid_async_ctrl_active == hid_async_slot_id(slot))
		hid_async_ctrl_active = 0;
}

static void hid_async_log_result(const struct hid_async_request *req,
				 int status, bool submit_failure)
{
	if (submit_failure)
		return;

	if (req->kind == HID_ASYNC_REQUEST_USB_CONTROL &&
	    req->control_request == TUSB_REQ_CLEAR_FEATURE &&
	    req->control_requesttype == TUSB_REQ_RCPT_ENDPOINT &&
	    req->control_value == TUSB_REQ_FEATURE_EDPT_HALT && !req->len) {
		/* Preserve the recovery trace after merging its EP0 wire builder. */
		async_msg(status < 0 ? "ERR: HID_CLEAR_HALT_FAIL" :
				       "DBG: HID_CLEAR_HALT_OK");
	} else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		async_msg(status < 0 ? "ERR: HID_REPORT_OUT_FAIL" :
				       "DBG: HID_REPORT_OUT_OK");
	} else if (req->report && req->reqtype == HID_REQ_SET_REPORT) {
		async_msg(status < 0 ? "ERR: HID_REPORT_SET_FAIL" :
				       "DBG: HID_REPORT_SET_OK");
	}
}

static void hid_async_finish_slot(struct hid_async_slot *slot, int status,
				  bool canceled, bool submit_failure)
{
	struct hid_async_request *req = &slot->req;
	u8 *owned_data = NULL;

	taskENTER_CRITICAL();
	slot->accepting_completion = false;
	slot->completion_ready = false;
	hid_async_slot_clear_physical_locked(slot);
	slot->state = HID_ASYNC_SLOT_COMPLETING;
	taskEXIT_CRITICAL();

	if (canceled) {
		if (req->complete && (req->hid || req->complete_on_cancel))
			req->complete(req, -ENODEV);
	} else {
		hid_async_log_result(req, status, submit_failure);
		if (req->complete)
			req->complete(req, status);
	}

	taskENTER_CRITICAL();
	if (slot->state == HID_ASYNC_SLOT_COMPLETING ||
	    slot->state == HID_ASYNC_SLOT_RELEASE_PENDING)
		owned_data = hid_async_slot_release_locked(slot);
	else
		configASSERT(slot->state == HID_ASYNC_SLOT_WAIT_PARSE);
	taskEXIT_CRITICAL();
	/* heap_4 must not run while the scheduler-wide critical section is held. */
	kfree(owned_data);
}

int hid_async_control_report_hold(const struct hid_async_request *req)
{
	struct hid_async_slot *slot;
	int ret = -ENODEV;

	if (!req || !req->hid || !req->report ||
	    req->reqtype != HID_REQ_GET_REPORT)
		return -EINVAL;

	taskENTER_CRITICAL();
	slot = hid_async_slot_for_request_locked((struct hid_async_request *)req);
	if (slot && slot->state == HID_ASYNC_SLOT_COMPLETING &&
	    slot->req.serial == req->serial &&
	    !hid_async_slot_invalid_locked(slot)) {
		slot->state = HID_ASYNC_SLOT_WAIT_PARSE;
		ret = 0;
	}
	taskEXIT_CRITICAL();
	return ret;
}

void hid_async_control_report_release(struct hid_device *hid, u32 serial)
{
	bool released = false;

	if (!hid || !serial || !hid_async_slots)
		return;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state == HID_ASYNC_SLOT_WAIT_PARSE &&
		    slot->req.hid == hid && slot->req.serial == serial) {
			slot->state = HID_ASYNC_SLOT_RELEASE_PENDING;
			released = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (released)
		hid_async_notify_task();
}

/*
 * Retire one physical transfer before its stable pool slot can be reused.
 * Other endpoint callbacks remain durable in their own slots while this
 * executor waits for PIO-USB's delayed abort event and the host-owner fence.
 */
static int hid_async_retire_slot(struct hid_async_slot *slot)
{
	struct hid_async_request *req = &slot->req;
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.request = req,
		.dev_addr = req->dev_addr,
		.instance = req->instance,
		.ep_addr = hid_async_lane_is_out(
			(enum hid_async_lane)slot->lane) ? req->ep_addr : 0,
		.generation = req->generation,
		.serial = req->serial,
		.action = HID_ASYNC_HOST_ABORT,
	};
	bool completed;
	bool ep0 = !call.ep_addr;

	taskENTER_CRITICAL();
	if (slot->state == HID_ASYNC_SLOT_HOST_COMPLETING) {
		taskEXIT_CRITICAL();
		return -EAGAIN;
	}
	completed = slot->completion_ready;
	if (!completed)
		slot->state = HID_ASYNC_SLOT_RETIRING;
	taskEXIT_CRITICAL();
	if (completed)
		return 1;

	if (ep0) {
		/*
		 * A PIO completion which wins the abort race may advance one old
		 * TinyUSB control stage. Leave the global EP0 occupied, drain that
		 * FIFO event, and retry the newly active stage. Three host-owner
		 * fences cover TinyUSB's finite SETUP/DATA/ACK chain. If the exact
		 * callback still did not arrive, the HCD event was lost; synthesize
		 * TinyUSB's terminal TIMEOUT giveback for that serial only.
		 */
		for (u8 drain = 0; drain < HID_ASYNC_EP0_DRAIN_MAX; drain++) {
			hid_async_host_call_sync(&call);
			if (call.status != -EAGAIN)
				break;
			hid_async_drain_abort_frames();
		}
		if (call.status == -EAGAIN) {
			call.action = HID_ASYNC_HOST_RECOVER_EP0;
			hid_async_host_call_sync(&call);
			if (call.status > 0)
				async_msg("ERR: HID_EP0_EVENT_LOST");
			else if (!call.status)
				async_msg("ERR: HID_EP0_CALLBACK_LOST");
			else if (call.status != -ENODEV)
				async_msg("ERR: HID_EP0_OWNER_MISMATCH");
		}
	} else {
		hid_async_host_call_sync(&call);
		hid_async_drain_abort_frames();
		call.action = HID_ASYNC_HOST_FENCE;
		hid_async_host_call_sync(&call);
	}

	taskENTER_CRITICAL();
	slot->accepting_completion = false;
	completed = slot->completion_ready;
	hid_async_slot_clear_physical_locked(slot);
	taskEXIT_CRITICAL();
	return completed ? 1 : 0;
}

static bool hid_async_process_released(void)
{
	u8 *owned_data = NULL;
	bool processed = false;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (hid_async_slots[i].state !=
				HID_ASYNC_SLOT_RELEASE_PENDING)
			continue;
		owned_data = hid_async_slot_release_locked(&hid_async_slots[i]);
		processed = true;
		break;
	}
	taskEXIT_CRITICAL();
	kfree(owned_data);
	return processed;
}

static bool hid_async_process_active(TickType_t now)
{
	struct hid_async_slot *slot = NULL;
	TickType_t timeout = 0;
	bool canceled = false;
	bool completed = false;
	bool timed_out = false;
	int retire_status;
	int status = 0;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state != HID_ASYNC_SLOT_ACTIVE)
			continue;
		timeout = hid_async_request_xfer_timeout(&candidate->req);
		completed = candidate->completion_ready;
		canceled = hid_async_slot_invalid_locked(candidate);
		timed_out = candidate->req.timeout_ticks ?
			now - candidate->queued_at >= timeout :
			now - candidate->xfer_start >= timeout;
		if (completed || canceled || timed_out) {
			slot = candidate;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (!slot)
		return false;

	if (!completed && (canceled || timed_out)) {
		retire_status = hid_async_retire_slot(slot);
		if (retire_status == -EAGAIN)
			return false;
		completed = retire_status > 0;
	}

	taskENTER_CRITICAL();
	if (completed)
		status = slot->completion_status;
	canceled = hid_async_slot_invalid_locked(slot);
	hid_async_slot_clear_physical_locked(slot);
	taskEXIT_CRITICAL();

	if (canceled) {
		status = -ENODEV;
	} else if (!completed && timed_out) {
		async_msg("ERR: HID_XFER_TO");
		status = -ETIMEDOUT;
	}
	hid_async_finish_slot(slot, status, canceled, false);
	return true;
}

static bool hid_async_process_queued_terminal(TickType_t now)
{
	struct hid_async_slot *slot = NULL;
	bool canceled = false;
	bool timed_out = false;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state != HID_ASYNC_SLOT_QUEUED)
			continue;
		canceled = hid_async_slot_invalid_locked(candidate);
		timed_out = !hid_async_slot_parked_by_control_gate(candidate) &&
			candidate->req.timeout_ticks &&
			now - candidate->queued_at >=
			(TickType_t)candidate->req.timeout_ticks;
		if (canceled || timed_out) {
			slot = candidate;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (!slot)
		return false;

	/* Queued work has no physical owner, so it can leave any FIFO position. */
	hid_async_finish_slot(slot, canceled ? -ENODEV : -ETIMEDOUT,
				 canceled, timed_out);
	return true;
}

static struct hid_async_slot *hid_async_find_ready_head(TickType_t now)
{
	struct hid_async_slot *slot = NULL;

	taskENTER_CRITICAL();
	for (u8 n = 0; n < HID_ASYNC_SLOT_COUNT; n++) {
		u8 index = (u8)((hid_async_schedule_cursor + 1u + n) %
				HID_ASYNC_SLOT_COUNT);
		struct hid_async_slot *candidate = &hid_async_slots[index];
		TickType_t timeout;
		bool submit_expired;

		if (candidate->state != HID_ASYNC_SLOT_QUEUED ||
		    !hid_async_slot_is_head_locked(candidate) ||
		    hid_async_slot_invalid_locked(candidate) ||
		    hid_async_slot_parked_by_control_gate(candidate))
			continue;
		timeout = candidate->req.timeout_ticks ?
			(TickType_t)candidate->req.timeout_ticks :
			HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
		submit_expired = candidate->req.timeout_ticks ?
			now - candidate->queued_at >= timeout :
			candidate->submit_started &&
			now - candidate->submit_start >= timeout;
		/* A busy shared EP0 must not mask another lane's submit watchdog. */
		if (!submit_expired) {
			if (!hid_async_lane_is_out(
				(enum hid_async_lane)candidate->lane) &&
			    hid_async_ctrl_active)
				continue;
			if (candidate->submit_started &&
			    !hid_async_tick_reached(now, candidate->retry_at))
				continue;
		}
		hid_async_schedule_cursor = index;
		slot = candidate;
		break;
	}
	taskEXIT_CRITICAL();
	return slot;
}

static bool hid_async_start_slot(struct hid_async_slot *slot, TickType_t now)
{
	struct hid_async_request *req = &slot->req;
	TickType_t timeout = req->timeout_ticks ?
		(TickType_t)req->timeout_ticks :
		HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
	bool canceled;
	bool gated;
	bool timed_out;
	int ret;

	taskENTER_CRITICAL();
	canceled = hid_async_slot_invalid_locked(slot);
	gated = hid_async_slot_parked_by_control_gate(slot);
	if (!gated && !slot->submit_started) {
		slot->submit_started = true;
		slot->submit_start = now;
	}
	timed_out = !gated &&
		(req->timeout_ticks ? now - slot->queued_at >= timeout :
		    now - slot->submit_start >= timeout);
	if (!canceled && !timed_out && !gated) {
		if (!req->serial) {
			req->serial = ++hid_async_serial;
			if (!req->serial)
				req->serial = ++hid_async_serial;
		}
		req->xfer_result = XFER_RESULT_INVALID;
		req->actual_len = 0;
		slot->completion_ready = false;
		slot->accepting_completion = true;
		slot->state = HID_ASYNC_SLOT_SUBMIT_PENDING;
		if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane)) {
			configASSERT(!hid_async_ctrl_active);
			hid_async_ctrl_active = hid_async_slot_id(slot);
		}
	}
	taskEXIT_CRITICAL();
	if (gated && !canceled)
		return false;

	if (canceled) {
		hid_async_finish_slot(slot, -ENODEV, true, false);
		return true;
	}
	if (timed_out) {
		async_msg("ERR: HID_SUBMIT_TO");
		hid_async_finish_slot(slot, -ETIMEDOUT, false, true);
		return true;
	}

	ret = hid_async_submit_current(req);
	now = xTaskGetTickCount();
	taskENTER_CRITICAL();
	if (!ret) {
		req->wire_started = true;
		if (slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING) {
			slot->state = HID_ASYNC_SLOT_ACTIVE;
			slot->xfer_start = now;
		} else {
			/* A fast HUB_RESET completion owns/published this state. */
			configASSERT(slot->state == HID_ASYNC_SLOT_HOST_COMPLETING ||
				     (slot->state == HID_ASYNC_SLOT_ACTIVE &&
				      slot->completion_ready));
		}
	} else if (ret == -EAGAIN) {
		slot->accepting_completion = false;
		slot->state = HID_ASYNC_SLOT_QUEUED;
		slot->retry_at = now + 1;
		hid_async_slot_clear_physical_locked(slot);
	} else {
		slot->accepting_completion = false;
		hid_async_slot_clear_physical_locked(slot);
	}
	canceled = hid_async_slot_invalid_locked(slot);
	taskEXIT_CRITICAL();

	if (!ret) {
		return true;
	}
	if (ret == -EAGAIN)
		return true;

	hid_async_finish_slot(slot, canceled ? -ENODEV : ret,
				 canceled, true);
	return true;
}

static TickType_t hid_async_next_wait(TickType_t now)
{
	TickType_t wait = portMAX_DELAY;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];
		TickType_t left;
		TickType_t timeout;

		if (slot->state == HID_ASYNC_SLOT_RELEASE_PENDING ||
		    slot->completion_ready ||
		    ((slot->state == HID_ASYNC_SLOT_ACTIVE ||
		      slot->state == HID_ASYNC_SLOT_QUEUED) &&
		     hid_async_slot_invalid_locked(slot))) {
			wait = 0;
			break;
		}
		if (slot->state == HID_ASYNC_SLOT_ACTIVE) {
			timeout = hid_async_request_xfer_timeout(&slot->req);
			left = hid_async_ticks_left(now,
					slot->req.timeout_ticks ?
					slot->queued_at : slot->xfer_start,
						    timeout);
			wait = min_t(TickType_t, wait, left);
			continue;
		}
		if (slot->state != HID_ASYNC_SLOT_QUEUED)
			continue;
		if (hid_async_slot_parked_by_control_gate(slot))
			continue;
		if (slot->req.timeout_ticks) {
			left = hid_async_ticks_left(now, slot->queued_at,
				(TickType_t)slot->req.timeout_ticks);
			wait = min_t(TickType_t, wait, left);
			if (!left)
				continue;
		}
		if (!hid_async_slot_is_head_locked(slot))
			continue;
		if (slot->submit_started && !slot->req.timeout_ticks) {
			timeout = HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
			left = hid_async_ticks_left(now, slot->submit_start,
						    timeout);
			wait = min_t(TickType_t, wait, left);
			if (!left)
				continue;
		}
		if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
		    hid_async_ctrl_active)
			continue;
		if (!slot->submit_started) {
			wait = 0;
			break;
		}
		left = hid_async_tick_reached(now, slot->retry_at) ?
			0 : slot->retry_at - now;
		wait = min_t(TickType_t, wait, left);
	}
	taskEXIT_CRITICAL();
	return wait;
}

void hid_async_task(void *pvParameters)
{
	(void)pvParameters;
	taskENTER_CRITICAL();
	hid_async_task_handle = xTaskGetCurrentTaskHandle();
	taskEXIT_CRITICAL();

	for (;;) {
		struct hid_async_slot *slot;
		TickType_t now = xTaskGetTickCount();
		TickType_t wait;

		if (hid_async_process_released())
			continue;
		if (hid_async_process_active(now))
			continue;
		if (hid_async_process_queued_terminal(now))
			continue;
		slot = hid_async_find_ready_head(now);
		if (slot) {
			(void)hid_async_start_slot(slot, now);
			continue;
		}

		wait = hid_async_next_wait(now);
		(void)ulTaskNotifyTakeIndexed(HID_ASYNC_NOTIFY_INDEX, pdTRUE,
					       wait);
	}
}

static void hid_async_xfer_complete(tuh_xfer_t *xfer)
{
	struct hid_async_slot *slot = NULL;
	void *host_context = NULL;
	u32 serial = (u32)xfer->user_data;
	u32 actual = min_t(u32, xfer->actual_len, UINT16_MAX);
	u8 hub_addr = 0;
	u8 hub_port = 0;
	int host_status = 0;
	bool completed = false;
	bool host_complete = false;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];
		struct hid_async_request *req = &candidate->req;

		if (!serial || req->serial != serial ||
		    (candidate->state != HID_ASYNC_SLOT_ACTIVE &&
		     candidate->state != HID_ASYNC_SLOT_SUBMIT_PENDING &&
		     candidate->state != HID_ASYNC_SLOT_RETIRING) ||
		    !candidate->accepting_completion ||
		    req->dev_addr != xfer->daddr ||
		    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX ||
		    req->generation != hid_async_device_generation[req->dev_addr])
			continue;
		if ((hid_async_lane_is_out(
			(enum hid_async_lane)candidate->lane) &&
		     xfer->ep_addr != req->ep_addr) ||
		    (!hid_async_lane_is_out(
			(enum hid_async_lane)candidate->lane) && xfer->ep_addr))
			continue;

		slot = candidate;
		if (!xfer->ep_addr && !req->len)
			actual = 0;
		req->actual_len = (u16)actual;
		req->xfer_result = xfer->result;
		slot->completion_status = hid_async_xfer_status(xfer->result);
		slot->accepting_completion = false;
		if (req->kind == HID_ASYNC_REQUEST_HUB_RESET) {
			/* Pin this stable slot until the immediate host handoff returns. */
			slot->state = HID_ASYNC_SLOT_HOST_COMPLETING;
			hub_addr = req->dev_addr;
			hub_port = req->hub_port;
			host_context = req->context;
			host_status = slot->completion_status;
			host_complete = true;
		} else {
			slot->completion_ready = true;
			completed = true;
		}
		break;
	}
	taskEXIT_CRITICAL();

	if (host_complete) {
		/* TinyUSB hub status must not clear C_PORT_RESET before ATTACH. */
		usbhid_backend_hub_reset_host_complete(hub_addr, hub_port,
						       host_context, host_status);

		taskENTER_CRITICAL();
		/* HOST_COMPLETING prevents SMP teardown from recycling this serial. */
		configASSERT(slot &&
			     slot->state == HID_ASYNC_SLOT_HOST_COMPLETING &&
			     slot->req.serial == serial &&
			     slot->req.kind == HID_ASYNC_REQUEST_HUB_RESET);
		slot->state = HID_ASYNC_SLOT_ACTIVE;
		slot->completion_ready = true;
		completed = true;
		taskEXIT_CRITICAL();
	}
	if (completed)
		hid_async_notify_task();
}
