/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef USB_HOST_USBHID_PRIVATE_H
#define USB_HOST_USBHID_PRIVATE_H

#include "linux/include/linux/hid.h"

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
	// char *inbuf;                                                    /* Input buffer */
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
	// struct mutex mutex;                                             /* start/stop/open/close */
	// spinlock_t lock;                                                 /* fifo spinlock */
	// unsigned long iofl;                                             /* I/O flags (CTRL_RUNNING, OUT_RUNNING) */
	// struct timer_list io_retry;                                     /* Retry timer */
	// unsigned long stop_retry;                                       /* Time to give up, in jiffies */
	// unsigned int retry_delay;                                       /* Delay length in ms */
	// struct work_struct reset_work;                                  /* Task context for resets */
	// Linux URBs, DMA buffers, iofl, and work/timer primitives are not
	// available. TinyUSB owner tasks and the bounded port state below replace
	// them while retaining the upstream usbhid_device ownership boundary.
	wait_queue_head_t wait;                                           /* For sleeping */

	/*
	 * Upstream Linux gets descriptor ownership and I/O state from USB core,
	 * URBs, FIFOs, and iofl. This TinyUSB port keeps the corresponding
	 * transport-owned state here rather than extending generic hid_device.
	 */
	u8 *rdesc;
	unsigned int rsize;
	/* Fixed portion of the interface HID class descriptor. */
	u8 hid_descriptor[sizeof(struct hid_descriptor)];
	u8 dev_addr;
	u8 instance;
	u32 generation;
	u32 report_revision;
	u32 io_pending;
	/*
	 * Upstream ctrlhead/ctrltail and outhead/outtail index the arrays above.
	 * The bounded firmware transport keeps their one-based equivalents here;
	 * entries themselves live in hid_async's single startup-allocated pool.
	 */
	u8 async_ctrl_head;
	u8 async_ctrl_tail;
	u8 async_out_head;
	u8 async_out_tail;
	TaskHandle_t control_waiter;
	u16 report_bufsize;
	u8 report_owner;
	u8 report_slot;
	bool report_wanted;
	bool report_host_pending;
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

#endif
