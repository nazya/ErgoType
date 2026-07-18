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

/*
 * Upstream Linux HID transport can block in hid_hw_wait(), usb_control_msg(),
 * and related request paths. TinyUSB host callbacks cannot wait for another
 * TinyUSB callback to complete, so this firmware bridge serializes the active
 * HID control/report transfer in one task and resumes the ported call sites
 * through explicit completions.
 */

#define HID_ASYNC_QUEUE_LEN 4
#define HID_ASYNC_COMPLETION_QUEUE_LEN 4
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

struct hid_async_host_call {
	TaskHandle_t waiter;
	u8 dev_addr;
	u8 instance;
	u8 ep_addr;
	u32 generation;
	u32 serial;
	bool abort;
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

static void hid_async_device_descriptor_complete(tuh_xfer_t *xfer);
static void hid_async_string_descriptor_complete(tuh_xfer_t *xfer);

static void hid_async_call_on_host(void *data)
{
	struct hid_async_host_call *call = data;
	TaskHandle_t waiter = call->waiter;
	bool is_current = false;

	/*
	 * Generation-less TinyUSB abort must execute in its owner task and may only
	 * target the request which asked for the fence. A physical unmount advances
	 * the generation first; in that case class/HCD close already retires the old
	 * transfer and aborting could hit a newly reused USB address.
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

	if (call->abort && is_current)
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

int hid_async_init(void)
{
	hid_async_epoch_mutex = xSemaphoreCreateMutex();
	if (!hid_async_epoch_mutex)
		return -ENOMEM;

	hid_async_request_queue = xQueueCreate(HID_ASYNC_QUEUE_LEN,
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
 * report_stop() publishes ll_transport_stopping under the same critical
 * section. Therefore a request is either fully queued before teardown starts
 * or rejected after it; cancel + the FIFO barrier can drain the former set.
 */
static int hid_async_queue_hid_request(struct hid_async_request *req)
{
	int ret = 0;

	if (!hid_async_request_queue)
		return -ENODEV;

	taskENTER_CRITICAL();
	if (!req->hid || req->hid->ll_transport_stopping)
		ret = -ENODEV;
	else if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		ret = -ENODEV;
	else {
		req->generation = hid_async_device_generation[req->dev_addr];
		if (xQueueSendToBack(hid_async_request_queue, req, 0) != pdPASS)
			ret = -EBUSY;
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
	bool stopping;

	if (!req->hid)
		return false;

	taskENTER_CRITICAL();
	stopping = req->hid->ll_transport_stopping;
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
	 * The callback can allocate, log, and queue its descriptor continuation, so
	 * do not run it in a critical section. The mutex instead serializes the whole
	 * state transition with device epoch retirement. An unmount callback may
	 * wait here, but the unlock path never depends on TinyUSB host progress.
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
	struct hid_async_request req;
	u32 len;
	int ret;

	if (!hid_async_request_queue)
		return -ENODEV;

	if (reqtype != HID_REQ_GET_REPORT && reqtype != HID_REQ_SET_REPORT)
		return -ENOSYS;

	len = hid_report_len(report);
	if (len > HID_ASYNC_REPORT_MAX)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.report = report;
	req.reqtype = reqtype;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
	req.report_id = report->id;
	req.report_type = hid_async_tinyusb_report_type(report->type);
	req.len = (u16)len;
	req.complete = complete;
	req.context = context;

	if (reqtype == HID_REQ_SET_REPORT)
		hid_output_report(report, req.data);

	ret = hid_async_queue_hid_request(&req);
	if (ret)
		return ret;

	if (reqtype == HID_REQ_SET_REPORT)
		async_msg("DBG: HID_REPORT_SET_Q");
	return 0;
}

int hid_async_queue_output_report(struct hid_device *hid, const __u8 *buf,
				  size_t len, hid_async_complete_t complete,
				  void *context)
{
	struct hid_async_request req;

	if (!hid_async_request_queue)
		return -ENODEV;

	if (!len || len > HID_ASYNC_REPORT_MAX + 1)
		return -EIO;
	if (!hid->usb_altsetting.has_interrupt_out ||
	    !hid->usb_altsetting.interrupt_out_endpoint)
		return -ENOSYS;
	if (len - (buf[0] == 0) > CFG_TUH_HID_EPOUT_BUFSIZE)
		return -ENOSYS;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
	req.hid = hid;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
	req.report_id = buf[0];
	req.ep_addr = hid->usb_altsetting.interrupt_out_endpoint;
	req.len = (u16)(len - 1);
	req.complete = complete;
	req.context = context;
	memcpy(req.data, buf + 1, req.len);

	return hid_async_queue_hid_request(&req);
}

int hid_async_queue_raw_set_report(struct hid_device *hid, u8 report_id,
				   enum hid_report_type report_type,
				   const __u8 *buf, size_t len,
				   hid_async_complete_t complete,
				   void *context)
{
	struct hid_async_request req;
	const __u8 *data = buf;
	size_t data_len = len;

	if (!hid_async_request_queue)
		return -ENODEV;

	if (report_id == 0) {
		data++;
		data_len--;
	}

	if (data_len > HID_ASYNC_REPORT_MAX)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.reqtype = HID_REQ_SET_REPORT;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
	req.report_id = report_id;
	req.report_type = hid_async_tinyusb_report_type(report_type);
	req.len = (u16)data_len;
	req.complete = complete;
	req.context = context;
	memcpy(req.data, data, req.len);

	return hid_async_queue_hid_request(&req);
}

static int hid_async_queue_raw_get(struct hid_device *hid, u8 report_id,
				   enum hid_report_type report_type,
				   size_t len, hid_async_complete_t complete,
				   void *context)
{
	struct hid_async_request req;

	if (!hid_async_request_queue)
		return -ENODEV;

	if (len > HID_ASYNC_REPORT_MAX)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.reqtype = HID_REQ_GET_REPORT;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
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
	req.len = HID_ASYNC_DATA_MAX;
	req.complete = complete;
	req.context = context;

	return hid_async_queue_preprobe_request(&req);
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
	struct hid_async_request req;
	u8 *req_data;

	if (!hid_async_request_queue)
		return -ENODEV;

	// usb_control_msg() carries direction in both pipe and bmRequestType.
	// TinyUSB control setup uses bmRequestType, and the blocking timeout
	// argument has no async equivalent here.
	(void)pipe;
	(void)timeout;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_CONTROL;
	req.hid = hid;
	req.dev_addr = dev->dev_addr;
	req.instance = hid->instance;
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
	struct hid_async_request req;

	if (!hid_async_request_queue)
		return -ENODEV;

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
	req.instance = hid->instance;
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

static int hid_async_submit(struct hid_async_request *req)
{
	u8 *data = req->data + req->data_offset;
	u16 len = req->len - req->data_offset;
	bool ok;

	if (req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR) {
		ok = tuh_descriptor_get_device(req->dev_addr, data, len,
					       hid_async_device_descriptor_complete,
					       0);
	} else if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR) {
		ok = tuh_descriptor_get_string(req->dev_addr, req->string_index,
					       req->string_langid, data, len,
					       hid_async_string_descriptor_complete,
					       0);
	} else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		ok = tuh_hid_send_report(req->dev_addr, req->instance,
					 req->report_id, data, len);
	} else if (req->reqtype == HID_REQ_GET_REPORT) {
		ok = tuh_hid_get_report(req->dev_addr, req->instance,
					req->report_id, req->report_type,
					data, len);
	} else {
		ok = tuh_hid_set_report(req->dev_addr, req->instance,
					req->report_id, req->report_type,
					data, len);
	}

	return ok ? 0 : -EIO;
}

static int hid_async_submit_current(struct hid_async_request *req)
{
	int ret;

	if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	/* Serialize generation retirement with the nonblocking TinyUSB submit. */
	taskENTER_CRITICAL();
	if (req->generation != hid_async_device_generation[req->dev_addr])
		ret = -ENODEV;
	else
		ret = hid_async_submit(req);
	taskEXIT_CRITICAL();
	return ret;
}

static bool hid_async_request_is_preprobe(const struct hid_async_request *req)
{
	return req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR ||
	       req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR;
}

static int hid_async_submit_preprobe(struct hid_async_request *req)
{
	TickType_t start = xTaskGetTickCount();
	int ret;

	do {
		if (!hid_async_request_generation_current(req))
			return -ENODEV;
		ret = tuh_mounted(req->dev_addr) ?
		      hid_async_submit_current(req) : -EIO;
		if (!ret) {
			async_msg("DBG: HID_PRE_SUB_OK");
			return 0;
		}
		if (ret == -ENODEV)
			return ret;

		vTaskDelay(1);
	} while (xTaskGetTickCount() - start <
		 HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS);

	if (!hid_async_request_generation_current(req))
		return -ENODEV;
	async_msg("ERR: HID_PRE_SUB_TO");
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

	kind = req->reqtype == HID_REQ_GET_REPORT ?
	       HID_ASYNC_COMPLETE_GET : HID_ASYNC_COMPLETE_SET;

	return completion->kind == kind &&
	       completion->report_id == req->report_id &&
	       completion->report_type == req->report_type;
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
		active->actual_len = completion->len + active->data_offset;
		*status = completion->len ? 0 : -EIO;
	} else {
		active->actual_len = completion->len + active->data_offset;
		if (active->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR ||
		    active->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR)
			*status = completion->xfer_result == XFER_RESULT_SUCCESS ?
				  0 : -EIO;
		else
			*status = completion->len ? 0 : -EIO;
	}

	return true;
}

static void hid_async_drop_queued_device(u8 dev_addr, u8 instance)
{
	struct hid_async_request req;
	UBaseType_t count = uxQueueMessagesWaiting(hid_async_request_queue);

	for (UBaseType_t i = 0; i < count; i++) {
		BaseType_t queued;
		bool drop;

		/* Keep receive + rotate atomic against CORE0 FF/output producers. */
		taskENTER_CRITICAL();
		if (xQueueReceive(hid_async_request_queue, &req, 0) != pdPASS) {
			taskEXIT_CRITICAL();
			break;
		}
		drop = req.kind != HID_ASYNC_REQUEST_BARRIER && req.hid &&
		       req.dev_addr == dev_addr && req.instance == instance;
		if (!drop) {
			queued = xQueueSendToBack(hid_async_request_queue, &req, 0);
			(void)queued;
			configASSERT(queued == pdPASS);
		}
		taskEXIT_CRITICAL();

		if (drop && req.complete && req.hid)
			req.complete(&req, -ENODEV);
	}
}

static void hid_async_drop_queued_dev_addr(u8 dev_addr, u32 generation)
{
	struct hid_async_request req;
	UBaseType_t count = uxQueueMessagesWaiting(hid_async_request_queue);

	for (UBaseType_t i = 0; i < count; i++) {
		BaseType_t queued;
		bool drop;

		/* Keep receive + rotate atomic against CORE0 FF/output producers. */
		taskENTER_CRITICAL();
		if (xQueueReceive(hid_async_request_queue, &req, 0) != pdPASS) {
			taskEXIT_CRITICAL();
			break;
		}
		drop = req.kind != HID_ASYNC_REQUEST_BARRIER &&
		       req.dev_addr == dev_addr &&
		       req.generation == generation;
		if (!drop) {
			queued = xQueueSendToBack(hid_async_request_queue, &req, 0);
			(void)queued;
			configASSERT(queued == pdPASS);
		}
		taskEXIT_CRITICAL();

		if (drop && req.complete && req.hid)
			req.complete(&req, -ENODEV);
	}
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
		.abort = true,
	};
	struct hid_async_completion completion;
	bool transfer_completed = false;

	hid_async_stop_accepting_completion();
	hid_async_host_call_sync(&call);
	vTaskDelay(HID_ASYNC_ABORT_DRAIN_TICKS);
	call.abort = false;
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

	hid_async_drop_queued_device(dev_addr, instance);

	/* A per-interface stop must never cancel preprobe or a sibling interface. */
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
	if (!hid_async_epoch_mutex)
		return -ENODEV;
	if (dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	xSemaphoreTake(hid_async_epoch_mutex, portMAX_DELAY);
	taskENTER_CRITICAL();
	generation = hid_async_device_generation[dev_addr];
	hid_async_device_generation[dev_addr]++;
	taskEXIT_CRITICAL();

	hid_async_drop_queued_dev_addr(dev_addr, generation);
	/* A dequeue/publication gap is rejected by the generation check itself. */
	taskENTER_CRITICAL();
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
	xSemaphoreGive(hid_async_epoch_mutex);
	return ret;
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
		if (preprobe)
			status = hid_async_submit_preprobe(&active);
		else
			status = hid_async_submit_current(&active);
		if (status < 0) {
			hid_async_stop_accepting_completion();
			if (!active.report && active.reqtype == HID_REQ_SET_REPORT)
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

		if (!active.report && active.reqtype == HID_REQ_SET_REPORT) {
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

static bool hid_async_stamp_completion(struct hid_async_completion *completion)
{
	bool is_current;

	taskENTER_CRITICAL();
	is_current = hid_async_active && hid_async_accepting_completion &&
		  completion->dev_addr == hid_async_active_dev_addr &&
		  completion->instance == hid_async_active_instance &&
		  completion->kind == hid_async_active_kind &&
		  completion->dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX &&
		  hid_async_active_generation ==
			hid_async_device_generation[completion->dev_addr];
	if (is_current && completion->kind != HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR &&
	    completion->kind != HID_ASYNC_COMPLETE_STRING_DESCRIPTOR &&
	    completion->kind != HID_ASYNC_COMPLETE_OUTPUT)
		is_current = completion->report_id == hid_async_active_report_id &&
			  completion->report_type == hid_async_active_report_type;
	if (is_current) {
		completion->generation = hid_async_active_generation;
		completion->serial = hid_async_active_serial;
	}
	taskEXIT_CRITICAL();

	return is_current;
}

static void hid_async_complete(enum hid_async_completion_kind kind,
			       uint8_t dev_addr, uint8_t instance,
			       uint8_t report_id, uint8_t report_type,
			       uint16_t len, uint8_t xfer_result)
{
	struct hid_async_completion completion = {
		.kind = kind,
		.dev_addr = dev_addr,
		.instance = instance,
		.report_id = report_id,
		.report_type = report_type,
		.len = len,
		.xfer_result = xfer_result,
	};

	if (!hid_async_completion_queue)
		return;

	if (!hid_async_stamp_completion(&completion))
		return;

	if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS) {
		BaseType_t queued;

		/*
		 * There is only one active transfer. A full queue can therefore contain
		 * only stale records; never lose the real completion that owns its buffer.
		 */
		hid_async_drop_completions();
		queued = xQueueSendToBack(hid_async_completion_queue,
					  &completion, 0);
		(void)queued;
		configASSERT(queued == pdPASS);
	}
}

static void hid_async_device_descriptor_complete(tuh_xfer_t *xfer)
{
	async_msg("DBG: HID_DEV_DESC_CB");
	hid_async_complete(HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR, xfer->daddr,
			   0, 0, 0, xfer->actual_len, xfer->result);
}

static void hid_async_string_descriptor_complete(tuh_xfer_t *xfer)
{
	async_msg("DBG: HID_STR_DESC_CB");
	hid_async_complete(HID_ASYNC_COMPLETE_STRING_DESCRIPTOR, xfer->daddr,
			   0, 0, 0, xfer->actual_len, xfer->result);
}

void hid_async_backend_get_report_complete(uint8_t dev_addr, uint8_t instance,
					   uint8_t report_id,
					   uint8_t report_type,
					   uint16_t len)
{
	hid_async_complete(HID_ASYNC_COMPLETE_GET, dev_addr, instance,
			   report_id, report_type, len, XFER_RESULT_SUCCESS);
}

void hid_async_backend_set_report_complete(uint8_t dev_addr, uint8_t instance,
					   uint8_t report_id,
					   uint8_t report_type,
					   uint16_t len)
{
	hid_async_complete(HID_ASYNC_COMPLETE_SET, dev_addr, instance,
			   report_id, report_type, len, XFER_RESULT_SUCCESS);
}

void hid_async_backend_report_sent(uint8_t dev_addr, uint8_t instance,
				   uint8_t const *report, uint16_t len)
{
	(void)report;
	hid_async_complete(HID_ASYNC_COMPLETE_OUTPUT, dev_addr, instance,
			   0, 0, len, XFER_RESULT_SUCCESS);
}
