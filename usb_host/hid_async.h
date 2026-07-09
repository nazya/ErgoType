#ifndef USB_HOST_HID_ASYNC_H
#define USB_HOST_HID_ASYNC_H

#include "linux/include/linux/hid.h"

#define HID_ASYNC_REPORT_MAX 96u

struct hid_async_request;

typedef void (*hid_async_complete_t)(const struct hid_async_request *req,
				     int status);

enum hid_async_request_kind {
	HID_ASYNC_REQUEST_REPORT,
	HID_ASYNC_REQUEST_OUTPUT_REPORT,
};

struct hid_async_request {
	enum hid_async_request_kind kind;
	struct hid_device *hid;
	struct hid_report *report;
	enum hid_class_request reqtype;
	u8 dev_addr;
	u8 instance;
	u8 report_id;
	u8 report_type;
	u8 data_offset;
	u16 len;
	u16 actual_len;
	u8 data[HID_ASYNC_REPORT_MAX];
	hid_async_complete_t complete;
	void *context;
};

int hid_async_init(void);
void hid_async_task(void *pvParameters);
int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   hid_async_complete_t complete, void *context);
int hid_async_queue_output_report(struct hid_device *hid, const __u8 *buf,
				  size_t len, hid_async_complete_t complete,
				  void *context);
int hid_async_queue_raw_set_report(struct hid_device *hid, u8 report_id,
				   enum hid_report_type report_type,
				   const __u8 *buf, size_t len,
				   hid_async_complete_t complete,
				   void *context);
int hid_async_queue_raw_get_report(struct hid_device *hid,
				   struct hid_report *report, size_t len,
				   hid_async_complete_t complete,
				   void *context);
int hid_async_queue_raw_get_report_id(struct hid_device *hid, u8 report_id,
				      enum hid_report_type report_type,
				      size_t len,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_cancel_device(u8 dev_addr, u8 instance);

#endif
