/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USB_HOST_USBHID_PRIVATE_H
#define USB_HOST_USBHID_PRIVATE_H

#include "linux/include/linux/hid.h"

struct usbhid_control_input;
struct usbhid_io_waiter;
struct usbhid_report_request;

/* TinyUSB replacement for upstream HID_OPENED + HID_RESUME_RUNNING bits. */
enum usbhid_report_open_state {
	USBHID_REPORT_CLOSED,
	USBHID_REPORT_RESUMING,
	USBHID_REPORT_OPEN,
};

enum usbhid_report_rebuild_state {
	USBHID_REPORT_REBUILD_IDLE,
	USBHID_REPORT_REBUILD_DRAINING,
	USBHID_REPORT_REBUILD_RESUME_PENDING,
};

/*
 * USB-specific HID struct, to be pointed to
 * from struct hid_device->driver_data
 *
 * Upstream keeps this in usbhid.h. The firmware's public usbhid.h is also
 * included by main.c beside KeyD headers, so keep the Linux USB-private type
 * here to avoid leaking the compatibility struct device into that boundary.
 */
struct usbhid_device {
	struct hid_device *hid;				/* pointer to corresponding HID dev */

	struct usb_interface *intf;			/* USB interface */
	int ifnum;					/* USB interface number */

	unsigned int bufsize;                                           /* URB buffer size */

	// struct urb *urbin;                                              /* Input URB */
	char *inbuf;                                                       /* Input buffer */
	// dma_addr_t inbuf_dma;                                           /* Input buffer dma */
	//
	// struct urb *urbctrl;                                            /* Control URB */
	// struct usb_ctrlrequest *cr;                                     /* Control request struct */
	// struct hid_control_fifo ctrl[HID_CONTROL_FIFO_SIZE];            /* Control fifo */
	// unsigned char ctrlhead, ctrltail;                               /* Control fifo head & tail */
	// char *ctrlbuf;                                                   /* Control buffer */
	// dma_addr_t ctrlbuf_dma;                                         /* Control buffer dma */
	// unsigned long last_ctrl;                                        /* record of last output for timeouts */
	//
	// struct urb *urbout;                                             /* Output URB */
	// struct hid_output_fifo out[HID_CONTROL_FIFO_SIZE];              /* Output pipe fifo */
	// unsigned char outhead, outtail;                                 /* Output pipe fifo head & tail */
	// char *outbuf;                                                    /* Output buffer */
	// dma_addr_t outbuf_dma;                                          /* Output buffer dma */
	// unsigned long last_out;                                         /* record of last output for timeouts */
	//
	struct mutex mutex;                                                /* start/stop/open/close */
	// spinlock_t lock;                                                 /* fifo spinlock */
	// unsigned long iofl;                                             /* I/O flags (CTRL_RUNNING, OUT_RUNNING) */
	// struct timer_list io_retry;                                     /* Retry timer */
	// unsigned long stop_retry;                                       /* Time to give up, in jiffies */
	// unsigned int retry_delay;                                       /* Delay length in ms */
	// struct work_struct reset_work;                                  /* Task context for resets */
	// Linux URBs, DMA buffers, iofl, and work/timer primitives are not
	// available. The upstream lifecycle mutex remains above; TinyUSB callbacks
	// and task continuations use the shared transport mutex in place of the
	// IRQ-side FIFO spinlock without masking interrupts.
	// Embedding Linux's rings would permanently cost about 5 KiB per HID. The
	// port instead keeps compact dynamically allocated logical entries below,
	// with the same per-interface CTRL/OUT ownership and one physical head per
	// lane. hid_async remains only the bounded physical-endpoint executor.
	// wait_queue_head_t wait;                                        /* For sleeping */
	// FreeRTOS notifications are wake edges, while this intrusive list keeps
	// every concurrent hid_hw_wait()/teardown predicate owner durable.
	struct usbhid_io_waiter *io_waiters;
	/*
	 * Upstream USB core wakes driver protocol waits when disconnect makes
	 * their answers impossible. HID++ wait heads are linked here without
	 * allocation and remain bound to this exact interface generation.
	 */
	wait_queue_head_t *protocol_waits;
	struct usbhid_report_request *ctrl_head;
	struct usbhid_report_request *ctrl_tail;
	struct usbhid_report_request *out_head;
	struct usbhid_report_request *out_tail;
	u16 ctrl_count;
	u8 out_count;

	/*
	 * Upstream Linux gets descriptor ownership and I/O state from USB core,
	 * URBs, FIFOs, and iofl. This TinyUSB port keeps the corresponding
	 * transport-owned state here rather than extending generic hid_device.
	 */
	/* Fixed portion of the interface HID class descriptor. */
	u8 hid_descriptor[sizeof(struct hid_descriptor)];
	u8 dev_addr;
	u8 instance;
	u32 generation;
	u32 report_revision;
	u32 io_pending;
	/* Upstream Linux: no equivalent; direct probe-GET completion for .wait(). */
	struct usbhid_control_input *owned_control_input;
	/* Stack-owned waiter for non-ALWAYS usb_kill_urb() close semantics. */
	TaskHandle_t report_close_waiter;
	u16 report_bufsize;
	u8 report_owner;
	u8 report_slot;
	bool report_wanted;
	/* Keep the enum byte-sized: one logical rebuild spans stop/start. */
	u8 report_rebuild_state;
	/* Keep the enum byte-sized: this object is allocated once per interface. */
	u8 report_open_state;
	bool report_host_pending;
	/* Publish parsers/readers only after post-probe evdev activation completes. */
	bool driver_ready;
	/* Driver deliberately opened raw_event-only ingress during probe/remove. */
	bool probe_raw_event;
	bool transport_stopping;
	bool disconnect_queued;

	/*
	 * Upstream Linux USB core owns the physical usb_device. The firmware's
	 * shared device cache has the same lifetime; only interface-local shims
	 * remain embedded here.
	 */
	struct usb_host_interface usb_altsetting;
	struct usb_interface usb_intf;
};

/* Upstream Linux: no equivalent; shared TinyUSB/FreeRTOS transport lease. */
void usbhid_io_put(struct hid_device *hid);
/* Caller holds hid_transport_lock(); the returned registry owner is lock-bound. */
struct hid_device *usbhid_report_owner_lookup_locked(unsigned int slot_index);
/*
 * Logical usbhid FIFO hooks used by the physical async executor. The _locked
 * variants never allocate, free, invoke completions, or acquire the mutex.
 */
void usbhid_request_capacity_available_locked(void);
void usbhid_request_cancel_device_locked(struct hid_device *hid);
void usbhid_request_cancel_dev_addr_locked(u8 dev_addr);
bool usbhid_request_queue_idle_locked(struct hid_device *hid);
bool usbhid_request_process(void);
/* Caller holds hid_transport_lock(); notification is only a coalesced edge. */
void usbhid_wait_wake_locked(struct hid_device *hid);

#endif
