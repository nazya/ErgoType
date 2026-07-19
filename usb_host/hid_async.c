#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "stdio_tusb_cdc.h"
#include "usbhid_private.h"

/*
 * Upstream Linux HID transport can block in hid_hw_wait(), usb_control_msg(),
 * and related request paths. TinyUSB host callbacks cannot wait for another
 * TinyUSB callback to complete, so this firmware bridge owns bounded per-HID
 * control/OUT lanes in one executor task and resumes the ported call sites
 * through explicit completions.
 */

#define HID_ASYNC_SUBMIT_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
#define HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
#define HID_ASYNC_PREPROBE_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
/* Match upstream usbhid's five-second watchdog for active ctrl/out I/O. */
#define HID_ASYNC_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(USB_CTRL_SET_TIMEOUT)
/* PIO-USB can publish an aborted completion at the end of the next SOF. */
#define HID_ASYNC_ABORT_DRAIN_TICKS ((TickType_t)2)
#define HID_ASYNC_DEVICE_ADDR_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
#define HID_ASYNC_SLOT_COUNT \
	(HID_ASYNC_REQUEST_QUEUE_LEN + 1u + CFG_TUH_HID)
#define HID_ASYNC_NOTIFY_INDEX 1u

_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > HID_ASYNC_NOTIFY_INDEX,
	       "HID async executor needs notification index 1");
_Static_assert(HID_ASYNC_SLOT_COUNT <= UINT8_MAX,
	       "HID async one-based FIFO links must fit in u8");

enum hid_async_host_action {
	HID_ASYNC_HOST_FENCE,
	HID_ASYNC_HOST_SUBMIT,
	HID_ASYNC_HOST_ABORT,
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
	HID_ASYNC_LANE_PREPROBE,
	HID_ASYNC_LANE_CTRL,
	HID_ASYNC_LANE_OUT,
};

enum hid_async_slot_state {
	HID_ASYNC_SLOT_FREE,
	HID_ASYNC_SLOT_QUEUED,
	HID_ASYNC_SLOT_SUBMIT_PENDING,
	HID_ASYNC_SLOT_ACTIVE,
	HID_ASYNC_SLOT_RETIRING,
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
	int completion_status;
	TickType_t submit_start;
	TickType_t retry_at;
	TickType_t xfer_start;
};

static struct hid_async_slot *hid_async_slots;
static SemaphoreHandle_t hid_async_epoch_mutex;
static TaskHandle_t hid_async_task_handle;
static u8 hid_async_preprobe_head;
static u8 hid_async_preprobe_tail;
static u8 hid_async_ctrl_active;
static u8 hid_async_schedule_cursor;
static u32 hid_async_serial;
static u32 hid_async_device_generation[HID_ASYNC_DEVICE_ADDR_MAX + 1];

static int hid_async_submit(struct hid_async_request *req);
static void hid_async_xfer_complete(tuh_xfer_t *xfer);
static void hid_async_host_call_sync(struct hid_async_host_call *call);
static void hid_async_notify_task(void);

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
	bool is_current = false;

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
		else if (call->action == HID_ASYNC_HOST_ABORT)
			is_current = slot->state == HID_ASYNC_SLOT_RETIRING;
	}
	taskEXIT_CRITICAL();

	if (call->action == HID_ASYNC_HOST_SUBMIT)
		call->status = is_current ? hid_async_submit(call->request) :
					    -ENODEV;
	else if (call->action == HID_ASYNC_HOST_ABORT && is_current)
		(void)tuh_edpt_abort_xfer(call->dev_addr, call->ep_addr);
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
	struct usbhid_device *usbhid;

	if (slot->lane == HID_ASYNC_LANE_PREPROBE) {
		*head = &hid_async_preprobe_head;
		*tail = &hid_async_preprobe_tail;
		return;
	}

	configASSERT(slot->req.hid);
	usbhid = slot->req.hid->driver_data;
	if (slot->lane == HID_ASYNC_LANE_CTRL) {
		*head = &usbhid->async_ctrl_head;
		*tail = &usbhid->async_ctrl_tail;
	} else {
		configASSERT(slot->lane == HID_ASYNC_LANE_OUT);
		*head = &usbhid->async_out_head;
		*tail = &usbhid->async_out_tail;
	}
}

static int hid_async_slot_queue_locked(const struct hid_async_request *req,
				       enum hid_async_lane lane)
{
	struct hid_async_slot *slot = NULL;
	struct hid_async_slot *tail_slot;
	u8 *head;
	u8 *tail;
	u8 id;

	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
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
	u8 *head;
	u8 *tail;

	hid_async_slot_fifo_locked(slot, &head, &tail);
	(void)tail;
	return *head == hid_async_slot_id(slot);
}

static void hid_async_slot_release_locked(struct hid_async_slot *slot)
{
	struct hid_async_slot *next;
	u8 *head;
	u8 *tail;
	u8 id = hid_async_slot_id(slot);

	hid_async_slot_fifo_locked(slot, &head, &tail);
	configASSERT(*head == id);
	*head = slot->next;
	if (!*head)
		*tail = 0;
	else {
		next = hid_async_slot_from_id(*head);
		configASSERT(next);
	}
	memset(slot, 0, sizeof(*slot));
}

int hid_async_init(void)
{
	hid_async_epoch_mutex = xSemaphoreCreateMutex();
	if (!hid_async_epoch_mutex)
		return -ENOMEM;

	/*
	 * Upstream keeps per-interface ctrl/out FIFO entries in usbhid_device.
	 * Per-interface full wire buffers would be too expensive, so their
	 * one-based lists share this single bounded startup allocation instead.
	 */
	hid_async_slots = pvPortMalloc(sizeof(*hid_async_slots) *
					 HID_ASYNC_SLOT_COUNT);
	if (!hid_async_slots)
		return -ENOMEM;
	memset(hid_async_slots, 0,
	       sizeof(*hid_async_slots) * HID_ASYNC_SLOT_COUNT);
	hid_async_schedule_cursor = HID_ASYNC_SLOT_COUNT - 1u;

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
			lane = req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT ?
				HID_ASYNC_LANE_OUT : HID_ASYNC_LANE_CTRL;
			ret = hid_async_slot_queue_locked(req, lane);
		}
	}
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

static int hid_async_queue_preprobe_request(struct hid_async_request *req)
{
	int ret = 0;

	if (!hid_async_slots || req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	req->generation = hid_async_device_generation[req->dev_addr];
	ret = hid_async_slot_queue_locked(req, HID_ASYNC_LANE_PREPROBE);
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

static int hid_async_queue_preprobe_continuation(
		struct hid_async_request *req)
{
	int ret = 0;

	if (!hid_async_slots ||
	    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	/*
	 * A descriptor continuation belongs to the epoch of the request which
	 * produced it. Never restamp an old chain with the generation of a device
	 * which has already reused the same USB address.
	 */
	taskENTER_CRITICAL();
	if (req->generation != hid_async_device_generation[req->dev_addr])
		ret = -ENODEV;
	else
		ret = hid_async_slot_queue_locked(req,
						 HID_ASYNC_LANE_PREPROBE);
	taskEXIT_CRITICAL();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

static void hid_async_complete_preprobe_current(
		const struct hid_async_request *req, int status)
{
	bool is_current;

	if (!req->complete || !hid_async_epoch_mutex ||
	    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return;

	/*
	 * The task-side continuation can allocate, log, and queue the next
	 * descriptor request, so do not run it in a critical section. The mutex
	 * serializes that continuation with task-context device retirement. TinyUSB
	 * unmount only advances the generation; the lifecycle task waits for this
	 * grace period before reusing the old descriptor cache object.
	 */
	xSemaphoreTake(hid_async_epoch_mutex, portMAX_DELAY);
	taskENTER_CRITICAL();
	is_current = req->generation ==
		     hid_async_device_generation[req->dev_addr];
	taskEXIT_CRITICAL();
	if (is_current)
		req->complete(req, status);
	xSemaphoreGive(hid_async_epoch_mutex);
}

int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
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
	if (len > HID_ASYNC_REPORT_MAX)
		return -EIO;
	dev = hid_to_usb_dev(hid);
	usbhid = hid->driver_data;
	if (reqtype == HID_REQ_GET_REPORT) {
		/* Match upstream hid_submit_ctrl() EP0 receive sizing. */
		maxpacket = dev->descriptor.bMaxPacketSize0;
		if (!maxpacket)
			maxpacket = 8;
		len += !len;
		len = DIV_ROUND_UP(len, maxpacket) * maxpacket;
		len = min_t(u32, len, HID_ASYNC_REPORT_MAX);
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
	req.complete = complete;
	req.context = context;

	interrupt_out = reqtype == HID_REQ_SET_REPORT &&
			report->type == HID_OUTPUT_REPORT &&
			usbhid->usb_altsetting.has_interrupt_out &&
			usbhid->usb_altsetting.interrupt_out_endpoint;

	if (reqtype == HID_REQ_SET_REPORT) {
		hid_output_report(report, req.data);
		if (interrupt_out) {
			/* hid_output_report() already produced the endpoint wire image. */
			req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
			req.ep_addr =
				usbhid->usb_altsetting.interrupt_out_endpoint;
		}
	}

	ret = hid_async_queue_hid_request(&req);
	if (ret)
		return ret;

	if (reqtype == HID_REQ_SET_REPORT)
		async_msg(interrupt_out ? "DBG: HID_REPORT_OUT_Q" :
					  "DBG: HID_REPORT_SET_Q");
	return 0;
}

int hid_async_queue_output_report(struct hid_device *hid, const __u8 *buf,
				  size_t len, hid_async_complete_t complete,
				  void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	bool skip_report_id;
	size_t wire_len;
	int ret;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid || !buf || !len)
		return -EINVAL;
	usbhid = hid->driver_data;

	if (!usbhid->usb_altsetting.has_interrupt_out ||
	    !usbhid->usb_altsetting.interrupt_out_endpoint)
		return -ENOSYS;
	skip_report_id = buf[0] == 0;
	wire_len = len - skip_report_id;
	if (wire_len > HID_ASYNC_DATA_MAX)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
	req.hid = hid;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = buf[0];
	req.ep_addr = usbhid->usb_altsetting.interrupt_out_endpoint;
	req.len = (u16)wire_len;
	req.complete = complete;
	req.context = context;
	if (wire_len)
		memcpy(req.data, buf + skip_report_id, wire_len);

	ret = hid_async_queue_hid_request(&req);

	if (!ret)
		async_msg("DBG: HID_OUTPUT_Q");
	return ret;
}

int hid_async_queue_raw_set_report(struct hid_device *hid, u8 report_id,
				   enum hid_report_type report_type,
				   const __u8 *buf, size_t len,
				   hid_async_complete_t complete,
				   void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	bool skip_report_id;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid || !buf || !len ||
	    !hid_async_report_type_valid(report_type))
		return -EINVAL;
	if (len > HID_ASYNC_REPORT_MAX)
		return -EIO;
	usbhid = hid->driver_data;

	/* Match usbhid_set_raw_report(): byte zero is always transport-owned. */
	skip_report_id = report_id == 0 ||
			 (report_type == HID_OUTPUT_REPORT &&
			  (hid->quirks & HID_QUIRK_SKIP_OUTPUT_REPORT_ID));

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.reqtype = HID_REQ_SET_REPORT;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = report_id;
	req.report_type = hid_async_tinyusb_report_type(report_type);
	req.data_offset = skip_report_id ? 1 : 0;
	req.len = (u16)len;
	req.complete = complete;
	req.context = context;
	req.data[0] = skip_report_id ? 0 : report_id;
	if (req.len > 1)
		memcpy(req.data + 1, buf + 1, req.len - 1);

	return hid_async_queue_hid_request(&req);
}

static int hid_async_queue_raw_get(struct hid_device *hid, u8 report_id,
				   enum hid_report_type report_type,
				   size_t len, hid_async_complete_t complete,
				   void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid || !len || !hid_async_report_type_valid(report_type))
		return -EINVAL;
	if (len > HID_ASYNC_REPORT_MAX)
		return -EIO;
	usbhid = hid->driver_data;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.reqtype = HID_REQ_GET_REPORT;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = report_id;
	req.report_type = hid_async_tinyusb_report_type(report_type);
	req.len = (u16)len;
	req.complete = complete;
	req.context = context;

	/*
	 * usbhid_get_raw_report() leaves report ID 0 in buf[0] and receives
	 * unnumbered payload at buf + 1, then counts that byte in ret.
	 */
	req.data[0] = report_id;
	if (!report_id)
		req.data_offset = 1;

	return hid_async_queue_hid_request(&req);
}

int hid_async_queue_raw_get_report_id(struct hid_device *hid, u8 report_id,
				      enum hid_report_type report_type,
				      size_t len,
				      hid_async_complete_t complete,
				      void *context)
{
	return hid_async_queue_raw_get(hid, report_id, report_type, len,
				       complete, context);
}

int hid_async_queue_idle(struct hid_device *hid, u8 report_id, u8 idle,
			 hid_async_complete_t complete, void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	int ret;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid)
		return -EINVAL;
	usbhid = hid->driver_data;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_IDLE;
	req.hid = hid;
	req.reqtype = HID_REQ_SET_IDLE;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = report_id;
	req.control_value = TU_U16(idle, report_id);
	req.complete = complete;
	req.context = context;

	ret = hid_async_queue_hid_request(&req);
	if (!ret)
		async_msg("DBG: HID_IDLE_Q");
	return ret;
}

int hid_async_queue_clear_halt(struct hid_device *hid, u8 ep_addr,
			       hid_async_complete_t complete, void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;

	if (!hid_async_slots)
		return -ENODEV;
	if (!hid || !ep_addr)
		return -EINVAL;
	usbhid = hid->driver_data;

	memset(&req, 0, sizeof(req));
	/*
	 * Upstream Linux: no equivalent glue. This is the asynchronous transport
	 * replacement for blocking usb_clear_halt() in usbhid reset_work.
	 */
	req.kind = HID_ASYNC_REQUEST_CLEAR_HALT;
	req.hid = hid;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.ep_addr = ep_addr;
	req.complete = complete;
	req.context = context;

	return hid_async_queue_hid_request(&req);
}

int hid_async_queue_device_descriptor(u8 dev_addr,
				      hid_async_complete_t complete,
				      void *context)
{
	struct hid_async_request req;

	if (!hid_async_slots)
		return -ENODEV;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR;
	req.dev_addr = dev_addr;
	req.len = sizeof(tusb_desc_device_t);
	req.complete = complete;
	req.context = context;

	return hid_async_queue_preprobe_request(&req);
}

int hid_async_queue_string_descriptor(u8 dev_addr, u8 index, u16 langid,
				      u32 generation,
				      hid_async_complete_t complete,
				      void *context)
{
	struct hid_async_request req;

	if (!hid_async_slots)
		return -ENODEV;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_STRING_DESCRIPTOR;
	req.dev_addr = dev_addr;
	req.string_index = index;
	req.string_langid = langid;
	req.generation = generation;
	req.len = HID_ASYNC_DATA_MAX;
	req.complete = complete;
	req.context = context;

	return hid_async_queue_preprobe_continuation(&req);
}

#if 0
/*
 * Deferred: async usb_control_msg()/usb_interrupt_msg() bridge is for drivers
 * outside the current CMake allowlist. Report GET/SET remains active above.
 */
static int hid_async_queue_usb_control_msg_flags(struct hid_device *hid,
						 struct usb_device *dev,
						 unsigned int pipe,
						 u8 request, u8 requesttype,
						 u16 value, u16 index,
						 const void *data, u16 size,
						 int timeout,
						 bool complete_on_cancel,
						 hid_async_complete_t complete,
						 void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	u8 *req_data;

	if (!hid_async_slots)
		return -ENODEV;
	usbhid = hid->driver_data;

	// usb_control_msg() carries direction in both pipe and bmRequestType.
	// TinyUSB control setup uses bmRequestType, and the blocking timeout
	// argument has no async equivalent here.
	(void)pipe;
	(void)timeout;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_CONTROL;
	req.hid = hid;
	req.dev_addr = dev->dev_addr;
	req.instance = usbhid->instance;
	req.control_request = request;
	req.control_requesttype = requesttype;
	req.control_value = value;
	req.control_index = index;
	req.len = size;
	req.complete_on_cancel = complete_on_cancel;
	req.complete = complete;
	req.context = context;
	if (size > HID_ASYNC_REPORT_MAX) {
		req.heap_data = pvPortMalloc(size);
		if (!req.heap_data)
			return -ENOMEM;
	}
	req_data = hid_async_request_data(&req);
	if (data && size)
		memcpy(req_data, data, size);

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS) {
		hid_async_request_free(&req);
		return -EBUSY;
	}

	return 0;
}

int hid_async_queue_usb_control_msg(struct hid_device *hid,
				    struct usb_device *dev,
				    unsigned int pipe,
				    u8 request, u8 requesttype,
				    u16 value, u16 index,
				    const void *data, u16 size,
				    int timeout,
				    hid_async_complete_t complete,
				    void *context)
{
	return hid_async_queue_usb_control_msg_flags(hid, dev, pipe, request,
						     requesttype, value, index,
						     data, size, timeout,
						     false, complete, context);
}

int hid_async_queue_usb_control_msg_cancelable(struct hid_device *hid,
					       struct usb_device *dev,
					       unsigned int pipe,
					       u8 request, u8 requesttype,
					       u16 value, u16 index,
					       const void *data, u16 size,
					       int timeout,
					       hid_async_complete_t complete,
					       void *context)
{
	return hid_async_queue_usb_control_msg_flags(hid, dev, pipe, request,
						     requesttype, value, index,
						     data, size, timeout,
						     true, complete, context);
}

int hid_async_queue_usb_interrupt_msg(struct hid_device *hid,
				      struct usb_device *dev,
				      unsigned int pipe,
				      const void *data, u16 size,
				      int timeout,
				      hid_async_complete_t complete,
				      void *context)
{
	struct usbhid_device *usbhid;
	struct hid_async_request req;

	if (!hid_async_request_queue)
		return -ENODEV;
	usbhid = hid->driver_data;

	if (size > HID_ASYNC_REPORT_MAX)
		return -EIO;

	/*
	 * usb_interrupt_msg() targets an interrupt endpoint selected by pipe.
	 * TinyUSB HID host exposes interrupt OUT through the HID instance, so
	 * pipe and timeout are only kept for the Linux-shaped call boundary.
	 */
	(void)pipe;
	(void)timeout;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_INTERRUPT;
	req.hid = hid;
	req.dev_addr = dev->dev_addr;
	req.instance = usbhid->instance;
	req.len = size;
	req.complete = complete;
	req.context = context;
	if (data && size)
		memcpy(req.data, data, size);

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
}
#endif

static int hid_async_submit_control(struct hid_async_request *req)
{
	struct usbhid_device *usbhid = req->hid->driver_data;
	u8 *data = req->data + req->data_offset;
	u16 len = req->len - req->data_offset;
	u16 value = req->kind == HID_ASYNC_REQUEST_IDLE ?
		    req->control_value : TU_U16(req->report_type, req->report_id);
	tusb_control_request_t const request = {
		.bmRequestType_bit = {
			.recipient = TUSB_REQ_RCPT_INTERFACE,
			.type = TUSB_REQ_TYPE_CLASS,
			.direction = req->reqtype == HID_REQ_GET_REPORT ?
				     TUSB_DIR_IN : TUSB_DIR_OUT,
		},
		.bRequest = (u8)req->reqtype,
		.wValue = tu_htole16(value),
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

static int hid_async_submit_clear_halt(struct hid_async_request *req)
{
	tusb_control_request_t const request = {
		.bmRequestType_bit = {
			.recipient = TUSB_REQ_RCPT_ENDPOINT,
			.type = TUSB_REQ_TYPE_STANDARD,
			.direction = TUSB_DIR_OUT,
		},
		.bRequest = TUSB_REQ_CLEAR_FEATURE,
		.wValue = tu_htole16(TUSB_REQ_FEATURE_EDPT_HALT),
		.wIndex = tu_htole16(req->ep_addr),
		.wLength = 0,
	};
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = 0,
		.setup = &request,
		.buffer = NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	return tuh_control_xfer(&xfer) ? 0 : -EAGAIN;
}

static int hid_async_submit_interrupt_out(struct hid_async_request *req)
{
	u8 *data = req->data + req->data_offset;
	u16 len = req->len - req->data_offset;
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
	u8 *data = req->data + req->data_offset;
	u16 len = req->len - req->data_offset;
	bool ok;

	if (req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR) {
		ok = tuh_descriptor_get_device(req->dev_addr, data, len,
					       hid_async_xfer_complete,
					       (uintptr_t)req->serial);
	} else if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR) {
		ok = tuh_descriptor_get_string(req->dev_addr, req->string_index,
					       req->string_langid, data, len,
					       hid_async_xfer_complete,
					       (uintptr_t)req->serial);
	} else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		return hid_async_submit_interrupt_out(req);
	} else if (req->kind == HID_ASYNC_REQUEST_CLEAR_HALT) {
		return hid_async_submit_clear_halt(req);
	} else {
		return hid_async_submit_control(req);
	}

	return ok ? 0 : -EAGAIN;
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

static bool hid_async_request_is_preprobe(const struct hid_async_request *req)
{
	return req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR ||
	       req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR;
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
	 * A per-interface stop must never cancel preprobe or a sibling interface.
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

int hid_async_synchronize_preprobe(void)
{
	if (!hid_async_epoch_mutex)
		return -ENODEV;

	/* Task-context grace period for a pre-probe continuation already running. */
	xSemaphoreTake(hid_async_epoch_mutex, portMAX_DELAY);
	xSemaphoreGive(hid_async_epoch_mutex);
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
	if (slot->lane != HID_ASYNC_LANE_OUT &&
	    hid_async_ctrl_active == hid_async_slot_id(slot))
		hid_async_ctrl_active = 0;
}

static void hid_async_log_result(const struct hid_async_request *req,
				 int status, bool submit_failure)
{
	if (submit_failure) {
		if (req->kind == HID_ASYNC_REQUEST_IDLE)
			async_msg("ERR: HID_IDLE_SUB");
		else if (!req->report && req->reqtype == HID_REQ_SET_REPORT)
			async_msg("ERR: HID_RAW_SET_SUB");
		return;
	}

	if (req->kind == HID_ASYNC_REQUEST_IDLE) {
		async_msg(status < 0 ? "ERR: HID_IDLE_FAIL" :
				       "DBG: HID_IDLE_OK");
	} else if (req->kind == HID_ASYNC_REQUEST_CLEAR_HALT) {
		async_msg(status < 0 ? "ERR: HID_CLEAR_HALT_FAIL" :
				       "DBG: HID_CLEAR_HALT_OK");
	} else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		if (status < 0)
			async_msg(req->report ? "ERR: HID_REPORT_OUT_FAIL" :
						"ERR: HID_OUTPUT_FAIL");
		else
			async_msg(req->report ? "DBG: HID_REPORT_OUT_OK" :
						"DBG: HID_OUTPUT_OK");
	} else if (!req->report && req->reqtype == HID_REQ_SET_REPORT) {
		async_msg(status < 0 ? "ERR: HID_RAW_SET_FAIL" :
				       "DBG: HID_RAW_SET_OK");
	} else if (req->report && req->reqtype == HID_REQ_SET_REPORT) {
		async_msg(status < 0 ? "ERR: HID_REPORT_SET_FAIL" :
				       "DBG: HID_REPORT_SET_OK");
	}
}

static void hid_async_finish_slot(struct hid_async_slot *slot, int status,
				  bool canceled, bool submit_failure)
{
	struct hid_async_request *req = &slot->req;
	bool preprobe = hid_async_request_is_preprobe(req);

	taskENTER_CRITICAL();
	slot->accepting_completion = false;
	slot->completion_ready = false;
	hid_async_slot_clear_physical_locked(slot);
	slot->state = HID_ASYNC_SLOT_COMPLETING;
	taskEXIT_CRITICAL();

	if (canceled) {
		/* A removed preprobe object has no client lifetime left. */
		if (req->complete && req->hid)
			req->complete(req, -ENODEV);
	} else {
		hid_async_log_result(req, status, submit_failure);
		if (req->complete) {
			if (preprobe)
				hid_async_complete_preprobe_current(req, status);
			else
				req->complete(req, status);
		}
	}

	taskENTER_CRITICAL();
	if (slot->state == HID_ASYNC_SLOT_COMPLETING ||
	    slot->state == HID_ASYNC_SLOT_RELEASE_PENDING)
		hid_async_slot_release_locked(slot);
	else
		configASSERT(slot->state == HID_ASYNC_SLOT_WAIT_PARSE);
	taskEXIT_CRITICAL();
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
static bool hid_async_retire_slot(struct hid_async_slot *slot)
{
	struct hid_async_request *req = &slot->req;
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.request = req,
		.dev_addr = req->dev_addr,
		.instance = req->instance,
		.ep_addr = req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT ?
			   req->ep_addr : 0,
		.generation = req->generation,
		.serial = req->serial,
		.action = HID_ASYNC_HOST_ABORT,
	};
	bool completed;

	taskENTER_CRITICAL();
	completed = slot->completion_ready;
	if (!completed) {
		slot->accepting_completion = false;
		slot->state = HID_ASYNC_SLOT_RETIRING;
	}
	taskEXIT_CRITICAL();
	if (completed)
		return true;

	hid_async_host_call_sync(&call);
	vTaskDelay(HID_ASYNC_ABORT_DRAIN_TICKS);
	call.action = HID_ASYNC_HOST_FENCE;
	hid_async_host_call_sync(&call);

	taskENTER_CRITICAL();
	completed = slot->completion_ready;
	hid_async_slot_clear_physical_locked(slot);
	taskEXIT_CRITICAL();
	return completed;
}

static bool hid_async_process_released(void)
{
	bool processed = false;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (hid_async_slots[i].state !=
				HID_ASYNC_SLOT_RELEASE_PENDING)
			continue;
		hid_async_slot_release_locked(&hid_async_slots[i]);
		processed = true;
		break;
	}
	taskEXIT_CRITICAL();
	return processed;
}

static bool hid_async_process_active(TickType_t now)
{
	struct hid_async_slot *slot = NULL;
	TickType_t timeout = 0;
	bool canceled = false;
	bool completed = false;
	bool timed_out = false;
	int status = 0;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state != HID_ASYNC_SLOT_ACTIVE)
			continue;
		timeout = hid_async_request_is_preprobe(&candidate->req) ?
			HID_ASYNC_PREPROBE_XFER_TIMEOUT_TICKS :
			HID_ASYNC_XFER_TIMEOUT_TICKS;
		completed = candidate->completion_ready;
		canceled = hid_async_slot_invalid_locked(candidate);
		timed_out = now - candidate->xfer_start >= timeout;
		if (completed || canceled || timed_out) {
			slot = candidate;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (!slot)
		return false;

	if (!completed && (canceled || timed_out))
		completed = hid_async_retire_slot(slot);

	taskENTER_CRITICAL();
	if (completed)
		status = slot->completion_status;
	canceled = hid_async_slot_invalid_locked(slot);
	hid_async_slot_clear_physical_locked(slot);
	taskEXIT_CRITICAL();

	if (canceled) {
		status = -ENODEV;
	} else if (!completed && timed_out) {
		async_msg(hid_async_request_is_preprobe(&slot->req) ?
			  "ERR: HID_PRE_XFER_TO" : "ERR: HID_XFER_TO");
		status = -ETIMEDOUT;
	}
	hid_async_finish_slot(slot, status, canceled, false);
	return true;
}

static struct hid_async_slot *hid_async_find_canceled_head(void)
{
	struct hid_async_slot *slot = NULL;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state == HID_ASYNC_SLOT_QUEUED &&
		    hid_async_slot_is_head_locked(candidate) &&
		    hid_async_slot_invalid_locked(candidate)) {
			slot = candidate;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return slot;
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
		    hid_async_slot_invalid_locked(candidate))
			continue;
		timeout = hid_async_request_is_preprobe(&candidate->req) ?
			HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS :
			HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
		submit_expired = candidate->submit_started &&
			now - candidate->submit_start >= timeout;
		/* A busy shared EP0 must not mask another lane's submit watchdog. */
		if (!submit_expired) {
			if (candidate->lane != HID_ASYNC_LANE_OUT &&
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
	TickType_t timeout = hid_async_request_is_preprobe(req) ?
		HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS :
		HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
	bool canceled;
	bool timed_out;
	int ret;

	taskENTER_CRITICAL();
	canceled = hid_async_slot_invalid_locked(slot);
	if (!slot->submit_started) {
		slot->submit_started = true;
		slot->submit_start = now;
	}
	timed_out = now - slot->submit_start >= timeout;
	if (!canceled && !timed_out) {
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
		if (slot->lane != HID_ASYNC_LANE_OUT) {
			configASSERT(!hid_async_ctrl_active);
			hid_async_ctrl_active = hid_async_slot_id(slot);
		}
	}
	taskEXIT_CRITICAL();

	if (canceled) {
		hid_async_finish_slot(slot, -ENODEV, true, false);
		return true;
	}
	if (timed_out) {
		async_msg(hid_async_request_is_preprobe(req) ?
			  "ERR: HID_PRE_SUB_TO" : "ERR: HID_SUBMIT_TO");
		hid_async_finish_slot(slot, -ETIMEDOUT, false, true);
		return true;
	}

	ret = hid_async_submit_current(req);
	now = xTaskGetTickCount();
	taskENTER_CRITICAL();
	if (!ret) {
		slot->state = HID_ASYNC_SLOT_ACTIVE;
		slot->xfer_start = now;
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
		if (hid_async_request_is_preprobe(req))
			async_msg("DBG: HID_PRE_SUB_OK");
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
		    (slot->cancel_requested &&
		     (slot->state == HID_ASYNC_SLOT_ACTIVE ||
		      (slot->state == HID_ASYNC_SLOT_QUEUED &&
		       hid_async_slot_is_head_locked(slot))))) {
			wait = 0;
			break;
		}
		if (slot->state == HID_ASYNC_SLOT_ACTIVE) {
			timeout = hid_async_request_is_preprobe(&slot->req) ?
				HID_ASYNC_PREPROBE_XFER_TIMEOUT_TICKS :
				HID_ASYNC_XFER_TIMEOUT_TICKS;
			left = hid_async_ticks_left(now, slot->xfer_start,
						    timeout);
			wait = min_t(TickType_t, wait, left);
			continue;
		}
		if (slot->state != HID_ASYNC_SLOT_QUEUED ||
		    !hid_async_slot_is_head_locked(slot))
			continue;
		if (slot->submit_started) {
			timeout = hid_async_request_is_preprobe(&slot->req) ?
				HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS :
				HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
			left = hid_async_ticks_left(now, slot->submit_start,
						    timeout);
			wait = min_t(TickType_t, wait, left);
			if (!left)
				continue;
		}
		if (slot->lane != HID_ASYNC_LANE_OUT && hid_async_ctrl_active)
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
		slot = hid_async_find_canceled_head();
		if (slot) {
			hid_async_finish_slot(slot, -ENODEV, true, false);
			continue;
		}
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
	u32 serial = (u32)xfer->user_data;
	u32 actual = min_t(u32, xfer->actual_len, UINT16_MAX);
	bool completed = false;

	taskENTER_CRITICAL();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];
		struct hid_async_request *req = &candidate->req;

		if (!serial || req->serial != serial ||
		    (candidate->state != HID_ASYNC_SLOT_ACTIVE &&
		     candidate->state != HID_ASYNC_SLOT_SUBMIT_PENDING) ||
		    !candidate->accepting_completion ||
		    req->dev_addr != xfer->daddr ||
		    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX ||
		    req->generation != hid_async_device_generation[req->dev_addr])
			continue;
		if ((candidate->lane == HID_ASYNC_LANE_OUT &&
		     xfer->ep_addr != req->ep_addr) ||
		    (candidate->lane != HID_ASYNC_LANE_OUT && xfer->ep_addr))
			continue;

		slot = candidate;
		if (!xfer->ep_addr && req->len == req->data_offset)
			actual = 0;
		req->actual_len = (u16)actual;
		if (actual && req->data_offset)
			req->actual_len += req->data_offset;
		req->xfer_result = xfer->result;
		slot->completion_status = hid_async_xfer_status(xfer->result);
		slot->completion_ready = true;
		slot->accepting_completion = false;
		completed = true;
		break;
	}
	taskEXIT_CRITICAL();
	if (completed)
		hid_async_notify_task();
}
