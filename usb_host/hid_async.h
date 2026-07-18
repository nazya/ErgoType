#ifndef USB_HOST_HID_ASYNC_H
#define USB_HOST_HID_ASYNC_H

#include "linux/include/linux/hid.h"

/*
 * Firmware-only HID async transport boundary. Upstream Linux call sites stay in
 * the imported HID files; this API is the TinyUSB/FreeRTOS replacement for the
 * blocking USB request machinery those call sites would normally use.
 */

#define HID_ASYNC_REPORT_MAX 257u
#define HID_ASYNC_DATA_MAX 257u

struct hid_async_request;

typedef void (*hid_async_complete_t)(const struct hid_async_request *req,
				     int status);

enum hid_async_request_kind {
	HID_ASYNC_REQUEST_REPORT,
	HID_ASYNC_REQUEST_OUTPUT_REPORT,
	HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR,
	HID_ASYNC_REQUEST_STRING_DESCRIPTOR,
#if 0
	/*
	 * Deferred: no currently linked HID driver needs async usb_control_msg(),
	 * usb_interrupt_msg(), or URB transport.
	 */
	HID_ASYNC_REQUEST_USB_CONTROL,
	HID_ASYNC_REQUEST_USB_INTERRUPT,
#endif
	HID_ASYNC_REQUEST_BARRIER,
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
	u8 ep_addr;
	u8 string_index;
#if 0
	/* Deferred with HID_ASYNC_REQUEST_USB_CONTROL. */
	u8 control_request;
	u8 control_requesttype;
#endif
	u16 len;
	u16 actual_len;
	u16 string_langid;
	u32 generation;
	u32 serial;
	u8 xfer_result;
#if 0
	/* Deferred with HID_ASYNC_REQUEST_USB_CONTROL. */
	u16 control_value;
	u16 control_index;
	tusb_control_request_t control_setup;
	u8 *heap_data;
#endif
	u8 data[HID_ASYNC_DATA_MAX];
#if 0
	/* Deferred with cancelable USB control requests. */
	bool complete_on_cancel;
#endif
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
int hid_async_queue_raw_get_report_id(struct hid_device *hid, u8 report_id,
				      enum hid_report_type report_type,
				      size_t len,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_queue_device_descriptor(u8 dev_addr,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_queue_string_descriptor(u8 dev_addr, u8 index, u16 langid,
				      hid_async_complete_t complete,
				      void *context);
#if 0
/*
 * Deferred: no currently linked HID driver needs async usb_control_msg(),
 * usb_interrupt_msg(), or URB transport. Keep the boundary for drivers that
 * will be re-enabled with matching hardware/emulator coverage.
 */
struct usb_device;

int hid_async_queue_usb_control_msg(struct hid_device *hid,
				    struct usb_device *dev,
				    unsigned int pipe,
				    u8 request, u8 requesttype,
				    u16 value, u16 index,
				    const void *data, u16 size,
				    int timeout,
				    hid_async_complete_t complete,
				    void *context);
int hid_async_queue_usb_control_msg_cancelable(struct hid_device *hid,
					       struct usb_device *dev,
					       unsigned int pipe,
					       u8 request, u8 requesttype,
					       u16 value, u16 index,
					       const void *data, u16 size,
					       int timeout,
					       hid_async_complete_t complete,
					       void *context);
int hid_async_queue_usb_interrupt_msg(struct hid_device *hid,
				      struct usb_device *dev,
				      unsigned int pipe,
				      const void *data, u16 size,
				      int timeout,
				      hid_async_complete_t complete,
				      void *context);
#endif
int hid_async_cancel_device(u8 dev_addr, u8 instance);
int hid_async_cancel_device_sync(u8 dev_addr, u8 instance);
int hid_async_cancel_dev_addr(u8 dev_addr);

/* Completion ingress called only by the TinyUSB callback facade. */
void hid_async_backend_get_report_complete(u8 dev_addr, u8 instance,
					   u8 report_id, u8 report_type,
					   u16 len);
void hid_async_backend_set_report_complete(u8 dev_addr, u8 instance,
					   u8 report_id, u8 report_type,
					   u16 len);
void hid_async_backend_report_sent(u8 dev_addr, u8 instance,
				   const u8 *report, u16 len);

#endif
