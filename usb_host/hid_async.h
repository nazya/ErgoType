#ifndef USB_HOST_HID_ASYNC_H
#define USB_HOST_HID_ASYNC_H

#include "linux/include/linux/hid.h"

/*
 * Firmware-only HID async transport boundary. Upstream Linux call sites stay in
 * the imported HID files; this API is the TinyUSB/FreeRTOS replacement for the
 * blocking USB request machinery those call sites would normally use.
 */

/* Base queued-work budget used to size fixed async and report slot pools. */
#define HID_ASYNC_REQUEST_QUEUE_LEN 4u

struct hid_async_request;

/*
 * Firmware-only equivalent of usbcore endpoint admission. Logical usbhid FIFO
 * heads and sleeping raw callers share one ticket-ordered list; a node is
 * linked only while it is ready to enter the bounded physical slot pool.
 */
struct hid_async_admission_node {
	struct hid_async_admission_node *next;
	TaskHandle_t task;
	u32 order;
	u32 generation;
	u8 dev_addr;
	u8 ep_addr;
	bool output_lane;
	bool linked;
};

typedef void (*hid_async_complete_t)(const struct hid_async_request *req,
				     int status);

enum hid_async_request_kind {
	HID_ASYNC_REQUEST_REPORT,
	HID_ASYNC_REQUEST_OUTPUT_REPORT,
	HID_ASYNC_REQUEST_USB_CONTROL,
	HID_ASYNC_REQUEST_USB_INTERRUPT,
	HID_ASYNC_REQUEST_HUB_RESET,
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
	u8 control_request;
	u8 control_requesttype;
	u8 hub_port;
	u16 len;
	u16 actual_len;
	u16 control_value;
	u16 control_index;
	u32 timeout_ticks;
	u32 generation;
	u32 serial;
	/* True only after TinyUSB accepted the physical transfer submission. */
	bool wire_started;
	hid_async_complete_t complete;
	void *context;
};

int hid_async_init(void);
void hid_async_task(void *pvParameters);
/*
 * Admit one already prepared logical usbhid FIFO head to the physical pool.
 * The caller retains data/context ownership through completion or rejection.
 */
int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   u32 generation,
			   u8 *data, u16 data_size,
			   struct hid_async_admission_node *admission,
			   hid_async_complete_t complete, void *context);
void hid_async_report_queue_kick(void);
/* A logical head was promoted or removed; wake physical admission predicates. */
void hid_async_logical_queue_changed(void);
/* Caller holds hid_transport_lock(); these never allocate or block. */
void hid_async_admission_logical_link_locked(
		struct hid_async_admission_node *node, u8 dev_addr,
		u32 generation, bool output_lane, u8 ep_addr);
void hid_async_admission_unlink_locked(
		struct hid_async_admission_node *node);
bool hid_async_admission_can_enter_locked(
		const struct hid_async_admission_node *node);
int hid_async_control_report_hold(const struct hid_async_request *req);
void hid_async_control_report_release(struct hid_device *hid, u32 serial);
int hid_async_device_epoch_snapshot(u8 dev_addr, u32 *generation);
bool hid_async_host_task_register(void);
bool hid_async_sync_call_allowed(void);
/* TinyUSB host-owner idle edges release durable submit-resource waiters. */
void hid_async_host_control_ready(void);
void hid_async_host_endpoint_ready(u8 dev_addr, u8 ep_addr);
/*
 * Nonblocking reset-work caller with a stable Linux-waitqueue-style ticket.
 * -EBUSY leaves @admission linked; every other result consumes it. The caller
 * must explicitly unlink a still-linked node before its storage can disappear.
 */
int hid_async_queue_usb_control_msg_admitted(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout,
		struct hid_async_admission_node *admission,
		hid_async_complete_t complete, void *context);
/* Task-side Linux synchronous USB glue waits for fixed-pool admission. */
int hid_async_wait_queue_usb_control_msg(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout,
		hid_async_complete_t complete, void *context);
int hid_async_wait_queue_usb_interrupt_out(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 ep_addr, void *data, u16 size, int timeout,
		hid_async_complete_t complete, void *context);
int hid_async_control_gate_acquire(void);
bool hid_async_control_gate_idle(void);
void hid_async_control_gate_release(u32 paused_ticks);
int hid_async_queue_hub_port_reset(u8 hub_addr, u32 generation, u8 hub_port,
				   hid_async_complete_t complete, void *context);
int hid_async_cancel_device(struct hid_device *hid);
bool hid_async_device_idle(struct hid_device *hid);
int hid_async_cancel_dev_addr(u8 dev_addr);

#endif
