#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "stdio_tusb_cdc.h"
#include "usbhid_private.h"

/*
 * Upstream Linux HID transport can block in hid_hw_wait(), usb_control_msg(),
 * and related request paths. TinyUSB host callbacks cannot wait for another
 * TinyUSB callback to complete, so this firmware bridge serializes the active
 * HID control/report transfer in one task and resumes the ported call sites
 * through explicit completions.
 */

#define HID_ASYNC_COMPLETION_QUEUE_LEN 4
#define HID_ASYNC_SUBMIT_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
#define HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
#define HID_ASYNC_PREPROBE_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
/* Match upstream usbhid's five-second watchdog for active ctrl/out I/O. */
#define HID_ASYNC_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(USB_CTRL_SET_TIMEOUT)
/* PIO-USB can publish an aborted completion at the end of the next SOF. */
#define HID_ASYNC_ABORT_DRAIN_TICKS ((TickType_t)2)
#define HID_ASYNC_DEVICE_ADDR_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)

enum hid_async_completion_kind {
	HID_ASYNC_COMPLETE_GET,
	HID_ASYNC_COMPLETE_SET,
	HID_ASYNC_COMPLETE_OUTPUT,
	HID_ASYNC_COMPLETE_IDLE,
	HID_ASYNC_COMPLETE_CLEAR_HALT,
	HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR,
	HID_ASYNC_COMPLETE_STRING_DESCRIPTOR,
	HID_ASYNC_COMPLETE_CANCEL_INSTANCE,
	HID_ASYNC_COMPLETE_CANCEL_DEVICE,
};

struct hid_async_completion {
	enum hid_async_completion_kind kind;
	u8 dev_addr;
	u8 instance;
	u8 report_id;
	u8 report_type;
	u16 len;
	u32 generation;
	u32 serial;
	u8 xfer_result;
};

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

struct hid_async_barrier {
	TaskHandle_t waiter;
	volatile bool done;
};

static QueueHandle_t hid_async_request_queue;
static QueueHandle_t hid_async_completion_queue;
static SemaphoreHandle_t hid_async_epoch_mutex;
/*
 * TinyUSB unplug closes the HID class and does not deliver a get/set report
 * completion for an active EP0 transfer. Track the one active async request so
 * unmount can wake only the matching device without disturbing another one.
 */
static volatile bool hid_async_active;
static volatile bool hid_async_accepting_completion;
static volatile u32 hid_async_active_generation;
static volatile u32 hid_async_active_serial;
static volatile enum hid_async_completion_kind hid_async_active_kind;
static volatile u8 hid_async_active_dev_addr;
static volatile u8 hid_async_active_instance;
static volatile u8 hid_async_active_report_id;
static volatile u8 hid_async_active_report_type;
static u32 hid_async_serial;
static u32 hid_async_device_generation[HID_ASYNC_DEVICE_ADDR_MAX + 1];

static int hid_async_submit(struct hid_async_request *req);
static void hid_async_xfer_complete(tuh_xfer_t *xfer);
static void hid_async_host_call_sync(struct hid_async_host_call *call);

static void hid_async_call_on_host(void *data)
{
	struct hid_async_host_call *call = data;
	TaskHandle_t waiter = call->waiter;
	bool is_current = false;

	/*
	 * TinyUSB submission and generation-less abort both run in the host owner.
	 * They may only target the request which asked for this call. A physical
	 * unmount advances the generation first; class/HCD close then owns the old
	 * transfer and an abort must not hit a newly reused USB address.
	 */
	taskENTER_CRITICAL();
	if (call->dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX)
		is_current = hid_async_active &&
			  hid_async_active_dev_addr == call->dev_addr &&
			  hid_async_active_instance == call->instance &&
			  hid_async_active_generation == call->generation &&
			  hid_async_active_serial == call->serial &&
			  hid_async_device_generation[call->dev_addr] ==
				call->generation;
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

int hid_async_init(void)
{
	hid_async_epoch_mutex = xSemaphoreCreateMutex();
	if (!hid_async_epoch_mutex)
		return -ENOMEM;

	hid_async_request_queue = xQueueCreate(HID_ASYNC_REQUEST_QUEUE_LEN,
					       sizeof(struct hid_async_request));
	if (!hid_async_request_queue)
		return -ENOMEM;

	hid_async_completion_queue = xQueueCreate(HID_ASYNC_COMPLETION_QUEUE_LEN,
						  sizeof(struct hid_async_completion));
	if (!hid_async_completion_queue)
		return -ENOMEM;

	return 0;
}

/*
 * report_stop() publishes usbhid->transport_stopping under the same critical
 * section. Therefore a request is either fully queued before teardown starts
 * or rejected after it; cancel + the FIFO barrier can drain the former set.
 */
static int hid_async_queue_hid_request(struct hid_async_request *req)
{
	struct usbhid_device *usbhid;
	int ret = 0;

	if (!hid_async_request_queue)
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
			if (xQueueSendToBack(hid_async_request_queue, req, 0) !=
			    pdPASS)
				ret = -EBUSY;
		}
	}
	taskEXIT_CRITICAL();
	return ret;
}

static int hid_async_queue_preprobe_request(struct hid_async_request *req)
{
	int ret = 0;

	if (!hid_async_request_queue || req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	req->generation = hid_async_device_generation[req->dev_addr];
	if (xQueueSendToBack(hid_async_request_queue, req, 0) != pdPASS)
		ret = -EBUSY;
	taskEXIT_CRITICAL();
	return ret;
}

static int hid_async_queue_preprobe_continuation(
		struct hid_async_request *req)
{
	int ret = 0;

	if (!hid_async_request_queue ||
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
	else if (xQueueSendToBack(hid_async_request_queue, req, 0) != pdPASS)
		ret = -EBUSY;
	taskEXIT_CRITICAL();
	return ret;
}

static bool hid_async_request_generation_current(
		const struct hid_async_request *req)
{
	bool is_current;

	if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return false;

	taskENTER_CRITICAL();
	is_current = req->generation == hid_async_device_generation[req->dev_addr];
	taskEXIT_CRITICAL();
	return is_current;
}

static bool hid_async_request_hid_stopping(
		const struct hid_async_request *req)
{
	struct usbhid_device *usbhid;
	bool stopping;

	if (!req->hid)
		return false;

	taskENTER_CRITICAL();
	usbhid = req->hid->driver_data;
	stopping = usbhid->transport_stopping;
	taskEXIT_CRITICAL();
	return stopping;
}

static bool hid_async_completion_is_cancel(
		enum hid_async_completion_kind kind)
{
	return kind == HID_ASYNC_COMPLETE_CANCEL_INSTANCE ||
	       kind == HID_ASYNC_COMPLETE_CANCEL_DEVICE;
}

static bool hid_async_active_kind_is_preprobe(void)
{
	return hid_async_active_kind == HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR ||
	       hid_async_active_kind == HID_ASYNC_COMPLETE_STRING_DESCRIPTOR;
}

static void hid_async_publish_active(struct hid_async_request *req)
{
	taskENTER_CRITICAL();
	req->serial = ++hid_async_serial;
	if (!req->serial)
		req->serial = ++hid_async_serial;
	if (req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR)
		hid_async_active_kind = HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR;
	else if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR)
		hid_async_active_kind = HID_ASYNC_COMPLETE_STRING_DESCRIPTOR;
	else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT)
		hid_async_active_kind = HID_ASYNC_COMPLETE_OUTPUT;
	else if (req->kind == HID_ASYNC_REQUEST_IDLE)
		hid_async_active_kind = HID_ASYNC_COMPLETE_IDLE;
	else if (req->kind == HID_ASYNC_REQUEST_CLEAR_HALT)
		hid_async_active_kind = HID_ASYNC_COMPLETE_CLEAR_HALT;
	else if (req->reqtype == HID_REQ_GET_REPORT)
		hid_async_active_kind = HID_ASYNC_COMPLETE_GET;
	else
		hid_async_active_kind = HID_ASYNC_COMPLETE_SET;
	hid_async_active_dev_addr = req->dev_addr;
	hid_async_active_instance = req->instance;
	hid_async_active_report_id = req->report_id;
	hid_async_active_report_type = req->report_type;
	hid_async_active_generation = req->generation;
	hid_async_active_serial = req->serial;
	hid_async_accepting_completion = true;
	hid_async_active = true;
	taskEXIT_CRITICAL();
}

static void hid_async_stop_accepting_completion(void)
{
	taskENTER_CRITICAL();
	hid_async_accepting_completion = false;
	taskEXIT_CRITICAL();
}

static void hid_async_clear_active(void)
{
	taskENTER_CRITICAL();
	hid_async_accepting_completion = false;
	hid_async_active = false;
	taskEXIT_CRITICAL();
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

	if (!hid_async_request_queue)
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

static int hid_async_submit_retry(struct hid_async_request *req, bool preprobe)
{
	TickType_t start = xTaskGetTickCount();
	TickType_t timeout = preprobe ?
		HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS :
		HID_ASYNC_SUBMIT_TIMEOUT_TICKS;
	int ret;

	do {
		if (!hid_async_request_generation_current(req))
			return -ENODEV;
		if (hid_async_request_hid_stopping(req))
			return -ENODEV;
		ret = hid_async_submit_current(req);
		if (!ret) {
			if (preprobe)
				async_msg("DBG: HID_PRE_SUB_OK");
			return 0;
		}
		if (ret != -EAGAIN)
			return ret;

		vTaskDelay(1);
	} while (xTaskGetTickCount() - start < timeout);

	if (!hid_async_request_generation_current(req))
		return -ENODEV;
	async_msg(preprobe ? "ERR: HID_PRE_SUB_TO" : "ERR: HID_SUBMIT_TO");
	return -ETIMEDOUT;
}

static bool hid_async_completion_matches(const struct hid_async_request *req,
					 const struct hid_async_completion *completion)
{
	enum hid_async_completion_kind kind;

	if (completion->dev_addr != req->dev_addr)
		return false;
	if (completion->serial != req->serial)
		return false;

	if (completion->kind == HID_ASYNC_COMPLETE_CANCEL_DEVICE)
		return completion->generation == req->generation;

	if (completion->kind == HID_ASYNC_COMPLETE_CANCEL_INSTANCE)
		return req->hid && completion->instance == req->instance &&
		       completion->generation == req->generation;

	if (completion->instance != req->instance)
		return false;
	if (completion->generation != req->generation)
		return false;

	if (req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR)
		return completion->kind == HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR;

	if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR)
		return completion->kind == HID_ASYNC_COMPLETE_STRING_DESCRIPTOR;

	if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT)
		return completion->kind == HID_ASYNC_COMPLETE_OUTPUT;
	if (req->kind == HID_ASYNC_REQUEST_IDLE)
		return completion->kind == HID_ASYNC_COMPLETE_IDLE;
	if (req->kind == HID_ASYNC_REQUEST_CLEAR_HALT)
		return completion->kind == HID_ASYNC_COMPLETE_CLEAR_HALT;

	kind = req->reqtype == HID_REQ_GET_REPORT ?
	       HID_ASYNC_COMPLETE_GET : HID_ASYNC_COMPLETE_SET;

	return completion->kind == kind &&
	       completion->report_id == req->report_id &&
	       completion->report_type == req->report_type;
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

static bool hid_async_apply_completion(struct hid_async_request *active,
				       const struct hid_async_completion *completion,
				       int *status, bool *canceled)
{
	if (!hid_async_completion_matches(active, completion))
		return false;

	active->xfer_result = completion->xfer_result;
	if (hid_async_completion_is_cancel(completion->kind)) {
		*status = -ENODEV;
		*canceled = true;
	} else if (completion->kind == HID_ASYNC_COMPLETE_OUTPUT) {
		active->actual_len = completion->len;
		*status = hid_async_xfer_status(completion->xfer_result);
	} else {
		active->actual_len = completion->len;
		/* Raw report ID zero is transport-owned, as in upstream usbhid. */
		if (completion->len && active->data_offset)
			active->actual_len += active->data_offset;
		*status = hid_async_xfer_status(completion->xfer_result);
	}

	return true;
}

static void hid_async_drop_completions(void)
{
	struct hid_async_completion completion;

	/*
	 * This is used only when the matching active device is being removed and
	 * the completion queue has no room for the cancel event that wakes it.
	 */
	while (xQueueReceive(hid_async_completion_queue, &completion, 0) == pdPASS) {
	}
}

static bool hid_async_drop_pre_submit_completions(
		const struct hid_async_request *req)
{
	struct hid_async_completion completion;
	bool canceled = false;

	while (xQueueReceive(hid_async_completion_queue, &completion, 0) == pdPASS) {
		if (hid_async_completion_matches(req, &completion) &&
		    hid_async_completion_is_cancel(completion.kind))
			canceled = true;
	}

	return canceled;
}

static int hid_async_queue_cancel(
		const struct hid_async_completion *completion)
{
	BaseType_t queued;

	if (xQueueSendToBack(hid_async_completion_queue, completion, 0) == pdPASS)
		return 0;

	/* Only one transfer is active, so queued records are stale or redundant. */
	hid_async_drop_completions();
	queued = xQueueSendToBack(hid_async_completion_queue, completion, 0);
	configASSERT(queued == pdPASS);
	return queued == pdPASS ? 0 : -EBUSY;
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

/*
 * Retire an active TinyUSB transfer before its stack-owned request is released.
 * PIO-USB can mark a transfer complete before publishing the HCD event at the
 * end of a later SOF, so an abort acknowledgement alone is not a lifetime
 * fence. Stop accepting callbacks, abort in the host owner, keep the request
 * alive for two SOFs, then put a second event through the host queue and drain
 * everything observed at the boundary.
 */
static bool hid_async_retire_active(struct hid_async_request *active,
				    int *status, bool *canceled)
{
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.dev_addr = active->dev_addr,
		.instance = active->instance,
		.ep_addr = active->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT ?
			   active->ep_addr : 0,
		.generation = active->generation,
		.serial = active->serial,
		.action = HID_ASYNC_HOST_ABORT,
	};
	struct hid_async_completion completion;
	bool transfer_completed = false;

	hid_async_stop_accepting_completion();
	hid_async_host_call_sync(&call);
	vTaskDelay(HID_ASYNC_ABORT_DRAIN_TICKS);
	call.action = HID_ASYNC_HOST_FENCE;
	hid_async_host_call_sync(&call);

	while (xQueueReceive(hid_async_completion_queue, &completion, 0) == pdPASS) {
		if (!hid_async_apply_completion(active, &completion,
						status, canceled))
			continue;
		if (!hid_async_completion_is_cancel(completion.kind))
			transfer_completed = true;
	}

	return transfer_completed;
}

int hid_async_cancel_device(u8 dev_addr, u8 instance)
{
	struct hid_async_completion completion;
	bool cancel_active = false;
	int ret;

	if (!hid_async_request_queue || !hid_async_completion_queue)
		return -ENODEV;

	/*
	 * A per-interface stop must never cancel preprobe or a sibling interface.
	 * Do not rotate the request FIFO or invoke queued continuations here: this
	 * entry is also used by the TinyUSB unmount callback. report_unplug() has
	 * already published usbhid->transport_stopping, so hid_async_task rejects and
	 * completes each queued request in task context. The lifecycle barrier then
	 * proves that no request retaining this HID remains before destruction.
	 */
	taskENTER_CRITICAL();
	if (hid_async_active && hid_async_accepting_completion &&
	    !hid_async_active_kind_is_preprobe() &&
	    hid_async_active_dev_addr == dev_addr &&
	    hid_async_active_instance == instance) {
		completion = (struct hid_async_completion) {
			.kind = HID_ASYNC_COMPLETE_CANCEL_INSTANCE,
			.dev_addr = dev_addr,
			.instance = instance,
			.generation = hid_async_active_generation,
			.serial = hid_async_active_serial,
		};
		cancel_active = true;
	}
	taskEXIT_CRITICAL();

	if (!cancel_active)
		return 0;
	ret = hid_async_queue_cancel(&completion);
	return ret;
}

int hid_async_cancel_device_sync(u8 dev_addr, u8 instance)
{
	struct hid_async_barrier sync = {
		.waiter = xTaskGetCurrentTaskHandle(),
	};
	struct hid_async_request barrier = {
		.kind = HID_ASYNC_REQUEST_BARRIER,
		.dev_addr = dev_addr,
		.instance = instance,
		.context = &sync,
	};
	bool done;
	int ret;

	do {
		ret = hid_async_cancel_device(dev_addr, instance);
		if (ret == -EBUSY)
			vTaskDelay(1);
	} while (ret == -EBUSY);
	if (ret)
		return ret;

	/*
	 * This FIFO fence also closes the dequeue-to-active publication window.
	 * No request retaining this HID can remain ahead of the notification.
	 */
	(void)ulTaskNotifyTake(pdTRUE, 0);
	if (xQueueSendToBack(hid_async_request_queue, &barrier,
			     portMAX_DELAY) != pdPASS)
		return -EBUSY;
	do {
		taskENTER_CRITICAL();
		done = sync.done;
		taskEXIT_CRITICAL();
		if (!done)
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	} while (!done);

	return 0;
}

int hid_async_cancel_dev_addr(u8 dev_addr)
{
	u32 generation;
	struct hid_async_completion completion;
	bool cancel_active = false;
	int ret;

	if (!hid_async_request_queue || !hid_async_completion_queue)
		return -ENODEV;
	if (dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	taskENTER_CRITICAL();
	generation = hid_async_device_generation[dev_addr];
	hid_async_device_generation[dev_addr]++;
	if (hid_async_active && hid_async_accepting_completion &&
	    hid_async_active_dev_addr == dev_addr &&
	    hid_async_active_generation == generation) {
		completion = (struct hid_async_completion) {
			.kind = HID_ASYNC_COMPLETE_CANCEL_DEVICE,
			.dev_addr = dev_addr,
			.instance = hid_async_active_instance,
			.generation = generation,
			.serial = hid_async_active_serial,
		};
		cancel_active = true;
	}
	taskEXIT_CRITICAL();

	ret = cancel_active ? hid_async_queue_cancel(&completion) : 0;
	return ret;
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

void hid_async_task(void *pvParameters)
{
	struct hid_async_request active;

	(void)pvParameters;

	while (1) {
		struct hid_async_completion completion;
		bool preprobe;
		bool canceled = false;
		bool retired = false;
		TickType_t xfer_wait_start;
		TickType_t xfer_timeout_ticks;
		int status;

		if (xQueueReceive(hid_async_request_queue, &active, portMAX_DELAY) != pdPASS)
			continue;

		if (active.kind == HID_ASYNC_REQUEST_BARRIER) {
			struct hid_async_barrier *barrier = active.context;
			TaskHandle_t waiter = barrier->waiter;

			/* With no active request, every queued completion is stale. */
			hid_async_drop_completions();
			taskENTER_CRITICAL();
			barrier->done = true;
			taskEXIT_CRITICAL();
			/* Publishing done releases the stack barrier. */
			xTaskNotifyGive(waiter);
			continue;
		}

		active.xfer_result = XFER_RESULT_INVALID;
		active.actual_len = 0;
		hid_async_publish_active(&active);

		canceled = hid_async_drop_pre_submit_completions(&active);
		if (!hid_async_request_generation_current(&active))
			canceled = true;
		if (hid_async_request_hid_stopping(&active))
			canceled = true;
		if (canceled) {
			hid_async_stop_accepting_completion();
			if (active.complete && active.hid)
				active.complete(&active, -ENODEV);
			hid_async_clear_active();
			continue;
		}

		preprobe = hid_async_request_is_preprobe(&active);
		status = hid_async_submit_retry(&active, preprobe);
		if (status < 0) {
			hid_async_stop_accepting_completion();
			if (active.kind == HID_ASYNC_REQUEST_IDLE)
				async_msg("ERR: HID_IDLE_SUB");
			else if (!active.report &&
				 active.reqtype == HID_REQ_SET_REPORT)
				async_msg("ERR: HID_RAW_SET_SUB");
			if (preprobe) {
				if (status != -ENODEV)
					hid_async_complete_preprobe_current(&active,
								    status);
			} else if (active.complete) {
				active.complete(&active, status);
			}
			hid_async_clear_active();
			continue;
		}
		xfer_wait_start = xTaskGetTickCount();
		xfer_timeout_ticks = preprobe ?
			HID_ASYNC_PREPROBE_XFER_TIMEOUT_TICKS :
			HID_ASYNC_XFER_TIMEOUT_TICKS;

		while (1) {
			TickType_t elapsed = xTaskGetTickCount() - xfer_wait_start;
			TickType_t wait_ticks = elapsed < xfer_timeout_ticks ?
				xfer_timeout_ticks - elapsed : 0;
			bool completed;

			if (xQueueReceive(hid_async_completion_queue, &completion,
					  wait_ticks) == pdPASS) {
				if (hid_async_apply_completion(&active, &completion,
							       &status, &canceled))
					break;
				continue;
			}

			completed = hid_async_retire_active(&active, &status,
							    &canceled);
			retired = true;
			if (!completed && !canceled) {
				if (preprobe)
					async_msg("ERR: HID_PRE_XFER_TO");
				else
					async_msg("ERR: HID_XFER_TO");
				status = -ETIMEDOUT;
			}
			break;
		}

		if (canceled && !retired) {
			(void)hid_async_retire_active(&active, &status, &canceled);
			retired = true;
		}
		hid_async_stop_accepting_completion();

		if (!hid_async_request_generation_current(&active))
			canceled = true;
		if (hid_async_request_hid_stopping(&active))
			canceled = true;

		if (canceled) {
			/* A removed preprobe object has no client lifetime left. */
			if (active.complete && active.hid)
				active.complete(&active, -ENODEV);
			hid_async_clear_active();
			continue;
		}

		if (active.kind == HID_ASYNC_REQUEST_IDLE) {
			async_msg(status < 0 ? "ERR: HID_IDLE_FAIL" :
					       "DBG: HID_IDLE_OK");
		} else if (active.kind == HID_ASYNC_REQUEST_CLEAR_HALT) {
			async_msg(status < 0 ? "ERR: HID_CLEAR_HALT_FAIL" :
					       "DBG: HID_CLEAR_HALT_OK");
		} else if (active.kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
			if (status < 0)
				async_msg(active.report ?
					  "ERR: HID_REPORT_OUT_FAIL" :
					  "ERR: HID_OUTPUT_FAIL");
			else
				async_msg(active.report ?
					  "DBG: HID_REPORT_OUT_OK" :
					  "DBG: HID_OUTPUT_OK");
		} else if (!active.report &&
			   active.reqtype == HID_REQ_SET_REPORT) {
			if (status < 0)
				async_msg("ERR: HID_RAW_SET_FAIL");
			else
				async_msg("DBG: HID_RAW_SET_OK");
		} else if (active.report && active.reqtype == HID_REQ_SET_REPORT) {
			if (status < 0)
				async_msg("ERR: HID_REPORT_SET_FAIL");
			else
				async_msg("DBG: HID_REPORT_SET_OK");
		}

		if (active.complete) {
			if (preprobe)
				hid_async_complete_preprobe_current(&active, status);
			else
				active.complete(&active, status);
		}
		hid_async_clear_active();
	}
}

static bool hid_async_stamp_xfer_completion(
		struct hid_async_completion *completion, u32 serial)
{
	bool is_current;

	taskENTER_CRITICAL();
	is_current = serial && hid_async_active &&
		  hid_async_accepting_completion &&
		  serial == hid_async_active_serial &&
		  completion->dev_addr == hid_async_active_dev_addr &&
		  completion->dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX &&
		  hid_async_active_generation ==
			hid_async_device_generation[completion->dev_addr];
	if (is_current) {
		completion->kind = hid_async_active_kind;
		completion->instance = hid_async_active_instance;
		completion->report_id = hid_async_active_report_id;
		completion->report_type = hid_async_active_report_type;
		completion->generation = hid_async_active_generation;
		completion->serial = serial;
	}
	taskEXIT_CRITICAL();

	return is_current;
}

static void hid_async_queue_completion(
		const struct hid_async_completion *completion)
{
	if (!hid_async_completion_queue)
		return;

	if (xQueueSendToBack(hid_async_completion_queue, completion, 0) != pdPASS) {
		BaseType_t queued;

		/*
		 * There is only one active transfer. A full queue can therefore contain
		 * only stale records; never lose the real completion that owns its buffer.
		 */
		hid_async_drop_completions();
		queued = xQueueSendToBack(hid_async_completion_queue,
					  completion, 0);
		(void)queued;
		configASSERT(queued == pdPASS);
	}
}

static void hid_async_xfer_complete(tuh_xfer_t *xfer)
{
	u32 actual = xfer->actual_len;
	struct hid_async_completion completion = {
		.dev_addr = xfer->daddr,
		.len = (u16)min_t(u32, actual, UINT16_MAX),
		.xfer_result = xfer->result,
	};
	u32 serial = (u32)xfer->user_data;

	if (!xfer->ep_addr) {
		u16 requested = tu_le16toh(xfer->setup->wLength);

		/* PIO-USB may count setup bytes for a no-data control request. */
		if (!requested)
			completion.len = 0;
	}
	if (!hid_async_stamp_xfer_completion(&completion, serial))
		return;
	hid_async_queue_completion(&completion);
}
