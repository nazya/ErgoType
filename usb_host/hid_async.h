#ifndef USB_HOST_HID_ASYNC_H
#define USB_HOST_HID_ASYNC_H

#include "linux/include/linux/hid.h"

/*
 * Firmware-only HID async transport boundary. Upstream Linux call sites stay in
 * the imported HID files; this API is the TinyUSB/FreeRTOS replacement for the
 * blocking USB request machinery those call sites would normally use.
 */

/* Bounded queued-work budget; B4 stores it in pool slots, not a FreeRTOS queue. */
#define HID_ASYNC_REQUEST_QUEUE_LEN 4u

struct hid_async_request;

typedef void (*hid_async_complete_t)(const struct hid_async_request *req,
				     int status);

enum hid_async_request_kind {
	HID_ASYNC_REQUEST_REPORT,
	HID_ASYNC_REQUEST_OUTPUT_REPORT,
	HID_ASYNC_REQUEST_DEVICE_DESCRIPTOR,
	HID_ASYNC_REQUEST_STRING_DESCRIPTOR,
	HID_ASYNC_REQUEST_USB_CONTROL,
	HID_ASYNC_REQUEST_USB_INTERRUPT,
};

struct hid_async_request {
	enum hid_async_request_kind kind;
	struct hid_device *hid;
	struct hid_report *report;
	u8 *data;
	enum hid_class_request reqtype;
	u8 dev_addr;
	u8 instance;
	u8 report_id;
	u8 report_type;
	u8 ep_addr;
	u8 string_index;
	u8 control_request;
	u8 control_requesttype;
	u16 len;
	u16 actual_len;
	u16 string_langid;
	u16 control_value;
	u16 control_index;
	u32 timeout_ticks;
	u32 generation;
	u32 serial;
	u8 xfer_result;
	/* SET_REPORT snapshots are slot-owned; all other buffers are borrowed. */
	bool data_owned;
	bool complete_on_cancel;
	hid_async_complete_t complete;
	void *context;
};

int hid_async_init(void);
void hid_async_task(void *pvParameters);
int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   u8 *data, u16 data_size,
			   hid_async_complete_t complete, void *context);
int hid_async_control_report_hold(const struct hid_async_request *req);
void hid_async_control_report_release(struct hid_device *hid, u32 serial);
int hid_async_queue_device_descriptor(u8 dev_addr,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_queue_string_descriptor(u8 dev_addr, u8 index, u16 langid,
				      u32 generation,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_device_epoch_snapshot(u8 dev_addr, u32 *generation);
void hid_async_host_task_register(void);
bool hid_async_sync_call_allowed(void);
int hid_async_queue_usb_control_msg(struct hid_device *owner, u8 dev_addr,
				    u32 generation,
				    u8 request, u8 requesttype,
				    u16 value, u16 index,
				    void *data, u16 size,
				    int timeout,
				    hid_async_complete_t complete,
				    void *context);
int hid_async_queue_usb_interrupt_out(struct hid_device *owner, u8 dev_addr,
				      u32 generation,
				      u8 ep_addr, void *data,
				      u16 size, int timeout,
				      hid_async_complete_t complete,
				      void *context);
int hid_async_cancel_device(u8 dev_addr, u8 instance);
int hid_async_cancel_device_sync(u8 dev_addr, u8 instance);
int hid_async_cancel_dev_addr(u8 dev_addr);
int hid_async_synchronize_preprobe(void);

#endif
