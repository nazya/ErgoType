#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

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

enum hid_async_completion_kind {
	HID_ASYNC_COMPLETE_GET,
	HID_ASYNC_COMPLETE_SET,
	HID_ASYNC_COMPLETE_OUTPUT,
	HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR,
	HID_ASYNC_COMPLETE_STRING_DESCRIPTOR,
	HID_ASYNC_COMPLETE_CANCEL,
};

struct hid_async_completion {
	enum hid_async_completion_kind kind;
	u8 dev_addr;
	u8 instance;
	u8 report_id;
	u8 report_type;
	u16 len;
	u8 xfer_result;
};

static QueueHandle_t hid_async_request_queue;
static QueueHandle_t hid_async_completion_queue;
/*
 * TinyUSB unplug closes the HID class and does not deliver a get/set report
 * completion for an active EP0 transfer. Track the one active async request so
 * unmount can wake only the matching device without disturbing another one.
 */
static volatile bool hid_async_active;
static volatile enum hid_async_completion_kind hid_async_active_kind;
static volatile u8 hid_async_active_dev_addr;
static volatile u8 hid_async_active_instance;
static volatile u8 hid_async_active_report_id;
static volatile u8 hid_async_active_report_type;

static void hid_async_device_descriptor_complete(tuh_xfer_t *xfer);
static void hid_async_string_descriptor_complete(tuh_xfer_t *xfer);

static u8 hid_async_tinyusb_report_type(enum hid_report_type type)
{
	return (u8)type + 1;
}

int hid_async_init(void)
{
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

int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   hid_async_complete_t complete, void *context)
{
	struct hid_async_request req;
	u32 len;

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

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

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

	if (len > HID_ASYNC_REPORT_MAX + 1)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
	req.hid = hid;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
	req.report_id = buf[0];
	req.len = (u16)(len - 1);
	req.complete = complete;
	req.context = context;
	memcpy(req.data, buf + 1, req.len);

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
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

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
}

static int hid_async_queue_raw_get(struct hid_device *hid,
				   struct hid_report *report, u8 report_id,
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
	req.report = report;
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

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
}

int hid_async_queue_raw_get_report(struct hid_device *hid,
				   struct hid_report *report, size_t len,
				   hid_async_complete_t complete,
				   void *context)
{
	return hid_async_queue_raw_get(hid, report, report->id, report->type,
				       len, complete, context);
}

int hid_async_queue_raw_get_report_id(struct hid_device *hid, u8 report_id,
				      enum hid_report_type report_type,
				      size_t len,
				      hid_async_complete_t complete,
				      void *context)
{
	return hid_async_queue_raw_get(hid, NULL, report_id, report_type, len,
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

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
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

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
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

int hid_async_queue_input_report(struct hid_device *hid,
				 enum hid_report_type type, const u8 *data,
				 size_t bufsize, u32 size, int interrupt)
{
	struct hid_async_request req;

	if (!hid_async_request_queue)
		return -ENODEV;

	if (bufsize > HID_ASYNC_REPORT_MAX)
		return -EIO;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_INPUT_REPORT;
	req.hid = hid;
	req.dev_addr = hid->dev_addr;
	req.instance = hid->instance;
	req.report_type = type;
	req.len = (u16)size;
	req.bufsize = (u16)bufsize;
	req.interrupt = interrupt;
	memcpy(req.data, data, bufsize);

	if (xQueueSendToBack(hid_async_request_queue, &req, 0) != pdPASS)
		return -EBUSY;

	return 0;
}

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

static bool hid_async_request_is_preprobe(const struct hid_async_request *req)
{
	return req->kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR ||
	       req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR;
}

static int hid_async_submit_preprobe(struct hid_async_request *req)
{
	TickType_t start = xTaskGetTickCount();

	do {
		if (tuh_mounted(req->dev_addr) && hid_async_submit(req) == 0) {
			async_msg("DBG: HID_PRE_SUB_OK");
			return 0;
		}

		vTaskDelay(1);
	} while (xTaskGetTickCount() - start <
		 HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS);

	async_msg("ERR: HID_PRE_SUB_TO");
	return -ETIMEDOUT;
}

static bool hid_async_completion_matches(const struct hid_async_request *req,
					 const struct hid_async_completion *completion)
{
	enum hid_async_completion_kind kind;

	if (completion->dev_addr != req->dev_addr ||
	    completion->instance != req->instance)
		return false;

	if (completion->kind == HID_ASYNC_COMPLETE_CANCEL)
		return true;

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

static void hid_async_drop_queued_device(u8 dev_addr, u8 instance)
{
	struct hid_async_request keep[HID_ASYNC_QUEUE_LEN];
	struct hid_async_request req;
	UBaseType_t keep_count = 0;

	while (keep_count < HID_ASYNC_QUEUE_LEN &&
	       xQueueReceive(hid_async_request_queue, &req, 0) == pdPASS) {
		if (req.dev_addr == dev_addr && req.instance == instance)
			continue;

		keep[keep_count++] = req;
	}

	for (UBaseType_t i = 0; i < keep_count; i++)
		xQueueSendToBack(hid_async_request_queue, &keep[i], 0);
}

static void hid_async_drop_queued_dev_addr(u8 dev_addr)
{
	struct hid_async_request keep[HID_ASYNC_QUEUE_LEN];
	struct hid_async_request req;
	UBaseType_t keep_count = 0;

	while (keep_count < HID_ASYNC_QUEUE_LEN &&
	       xQueueReceive(hid_async_request_queue, &req, 0) == pdPASS) {
		if (req.dev_addr == dev_addr)
			continue;

		keep[keep_count++] = req;
	}

	for (UBaseType_t i = 0; i < keep_count; i++)
		xQueueSendToBack(hid_async_request_queue, &keep[i], 0);
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

static void hid_async_drop_pre_submit_completions(void)
{
	struct hid_async_completion completion;

	while (xQueueReceive(hid_async_completion_queue, &completion, 0) == pdPASS) {
	}
}

int hid_async_cancel_device(u8 dev_addr, u8 instance)
{
	struct hid_async_completion completion = {
		.kind = HID_ASYNC_COMPLETE_CANCEL,
		.dev_addr = dev_addr,
		.instance = instance,
	};

	if (!hid_async_request_queue || !hid_async_completion_queue)
		return -ENODEV;

	hid_async_drop_queued_device(dev_addr, instance);
	if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS) {
		if (!hid_async_active ||
		    hid_async_active_dev_addr != dev_addr ||
		    hid_async_active_instance != instance)
			return -EBUSY;

		hid_async_drop_completions();
		if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS)
			return -EBUSY;
	}

	return 0;
}

int hid_async_cancel_dev_addr(u8 dev_addr)
{
	if (!hid_async_request_queue || !hid_async_completion_queue)
		return -ENODEV;

	hid_async_drop_queued_dev_addr(dev_addr);
	if (!hid_async_active || hid_async_active_dev_addr != dev_addr)
		return 0;

	struct hid_async_completion completion = {
		.kind = HID_ASYNC_COMPLETE_CANCEL,
		.dev_addr = dev_addr,
		.instance = hid_async_active_instance,
	};

	if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS) {
		hid_async_drop_completions();
		if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS)
			return -EBUSY;
	}

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
		int status;

		if (xQueueReceive(hid_async_request_queue, &active, portMAX_DELAY) != pdPASS)
			continue;

		if (active.kind == HID_ASYNC_REQUEST_INPUT_REPORT) {
			hid_deferred_input_report(active.hid,
						  (enum hid_report_type)active.report_type,
						  active.data, active.bufsize,
						  active.len, active.interrupt);
			continue;
		}

		hid_async_drop_pre_submit_completions();

		if (active.kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR)
			hid_async_active_kind = HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR;
		else if (active.kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR)
			hid_async_active_kind = HID_ASYNC_COMPLETE_STRING_DESCRIPTOR;
		else if (active.kind == HID_ASYNC_REQUEST_OUTPUT_REPORT)
			hid_async_active_kind = HID_ASYNC_COMPLETE_OUTPUT;
		else if (active.reqtype == HID_REQ_GET_REPORT)
			hid_async_active_kind = HID_ASYNC_COMPLETE_GET;
		else
			hid_async_active_kind = HID_ASYNC_COMPLETE_SET;
		hid_async_active_dev_addr = active.dev_addr;
		hid_async_active_instance = active.instance;
		hid_async_active_report_id = active.report_id;
		hid_async_active_report_type = active.report_type;
		active.xfer_result = XFER_RESULT_INVALID;
		active.actual_len = 0;
		hid_async_active = true;

		preprobe = hid_async_request_is_preprobe(&active);
		if (preprobe)
			status = hid_async_submit_preprobe(&active);
		else
			status = hid_async_submit(&active);
		if (status < 0) {
			hid_async_active = false;
			if (!active.report && active.reqtype == HID_REQ_SET_REPORT)
				async_msg("ERR: HID_RAW_SET_SUB");
			if (active.complete)
				active.complete(&active, status);
			continue;
		}

		while (1) {
			if (xQueueReceive(hid_async_completion_queue, &completion,
					  preprobe ?
					  HID_ASYNC_PREPROBE_SUBMIT_TIMEOUT_TICKS :
					  portMAX_DELAY) != pdPASS) {
				if (preprobe) {
					async_msg("ERR: HID_PRE_WAIT_TO");
					status = -ETIMEDOUT;
					break;
				}
				continue;
			}

			if (hid_async_completion_matches(&active, &completion)) {
				active.xfer_result = completion.xfer_result;
				if (completion.kind == HID_ASYNC_COMPLETE_CANCEL) {
					status = -ENODEV;
					canceled = true;
				} else if (completion.kind == HID_ASYNC_COMPLETE_OUTPUT) {
					active.actual_len = completion.len + active.data_offset;
					status = 0;
				} else {
					active.actual_len = completion.len + active.data_offset;
					if (active.kind == HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR ||
					    active.kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR)
						status = completion.xfer_result == XFER_RESULT_SUCCESS ?
							 0 : -EIO;
					else
						status = completion.len ? 0 : -EIO;
				}
				break;
			}
		}

		hid_async_active = false;
		if (canceled)
			continue;

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

		if (active.complete)
			active.complete(&active, status);
		else if (status >= 0 && active.reqtype == HID_REQ_GET_REPORT &&
			 active.report)
			hid_input_report(active.hid, active.report->type,
					 active.data, active.actual_len, 0);
	}
}

static bool hid_async_completion_is_current(enum hid_async_completion_kind kind,
					    uint8_t dev_addr, uint8_t instance,
					    uint8_t report_id,
					    uint8_t report_type)
{
	if (!hid_async_active ||
	    dev_addr != hid_async_active_dev_addr ||
	    instance != hid_async_active_instance ||
	    kind != hid_async_active_kind)
		return false;

	if (kind == HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR)
		return true;

	if (kind == HID_ASYNC_COMPLETE_STRING_DESCRIPTOR)
		return true;

	if (kind == HID_ASYNC_COMPLETE_OUTPUT)
		return true;

	return report_id == hid_async_active_report_id &&
	       report_type == hid_async_active_report_type;
}

static void hid_async_complete(enum hid_async_completion_kind kind,
			       uint8_t dev_addr, uint8_t instance,
			       uint8_t report_id, uint8_t report_type,
			       uint16_t len, uint8_t xfer_result)
{
	bool preprobe = kind == HID_ASYNC_COMPLETE_DEVICE_DESCRIPTOR ||
			kind == HID_ASYNC_COMPLETE_STRING_DESCRIPTOR;
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

	if (!hid_async_completion_is_current(kind, dev_addr, instance,
					     report_id, report_type)) {
		if (preprobe)
			async_msg("ERR: HID_PRE_CB_DROP");
		return;
	}

	if (xQueueSendToBack(hid_async_completion_queue, &completion, 0) != pdPASS &&
	    preprobe)
		async_msg("ERR: HID_PRE_CB_Q_FULL");
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

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance,
				    uint8_t report_id, uint8_t report_type,
				    uint16_t len)
{
	hid_async_complete(HID_ASYNC_COMPLETE_GET, dev_addr, instance,
			   report_id, report_type, len, XFER_RESULT_SUCCESS);
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance,
				    uint8_t report_id, uint8_t report_type,
				    uint16_t len)
{
	hid_async_complete(HID_ASYNC_COMPLETE_SET, dev_addr, instance,
			   report_id, report_type, len, XFER_RESULT_SUCCESS);
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t instance,
			    uint8_t const *report, uint16_t len)
{
	(void)report;
	hid_async_complete(HID_ASYNC_COMPLETE_OUTPUT, dev_addr, instance,
			   0, 0, len, XFER_RESULT_SUCCESS);
}
