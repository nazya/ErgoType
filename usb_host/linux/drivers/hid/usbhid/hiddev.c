// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2001 Paul Stewart
 *  Copyright (c) 2001 Vojtech Pavlik
 *
 *  HID char devices, giving access to raw HID device events.
 */

/*
 * PORTING STATUS — DO NOT LINK.
 *
 * This file is an inactive firmware hiddev proxy rewrite, not a line-preserving
 * port of Linux hiddev.c. It intentionally replaces the fd/ioctl/fasync model
 * with an in-process bounded proxy and therefore does not satisfy
 * usb_host/upstream-porting-rules.md. Keep CONFIG_USB_HIDDEV/CMake disabled
 * until the proxy is moved to firmware glue and this Linux-derived file is
 * restored to an adjacent, reviewable upstream shape.
 */

/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to Paul Stewart <stewart@wetlogic.net>
 */

// #include <linux/poll.h>
// #include <linux/slab.h>
// #include <linux/sched/signal.h>
// #include <linux/module.h>
// #include <linux/init.h>
// #include <linux/input.h>
// #include <linux/usb.h>
// #include <linux/hid.h>
// #include <linux/hiddev.h>
// #include <linux/compat.h>
// #include <linux/vmalloc.h>
// #include <linux/nospec.h>
// #include "usbhid.h"
// Firmware builds this as a hiddev proxy boundary, not a Linux char device.
#include <string.h>

#include "../../../include/linux/hid.h"
#include "../../../include/linux/hiddev.h"
#include "../../../include/linux/slab.h"

// #ifdef CONFIG_USB_DYNAMIC_MINORS
// #define HIDDEV_MINOR_BASE	0
// #define HIDDEV_MINORS		256
// #else
#define HIDDEV_MINOR_BASE	96
#define HIDDEV_MINORS		16
// #endif
// #define HIDDEV_BUFFER_SIZE	2048
// Firmware keeps HIDDEV_PROXY_BUFFER_SIZE in include/linux/hiddev.h.

struct hiddev_proxy {
	hiddev_proxy_handle_t handle;
	struct hiddev *hiddev;
};

static LIST_HEAD(hiddev_table);
static struct hiddev_proxy hiddev_proxies[HIDDEV_MINORS];
static hiddev_proxy_handle_t hiddev_next_handle = HIDDEV_PROXY_INVALID_HANDLE + 1;

static int hiddev_report_type(unsigned int type)
{
	return (type == HID_INPUT_REPORT) ? HID_REPORT_TYPE_INPUT :
	  ((type == HID_OUTPUT_REPORT) ? HID_REPORT_TYPE_OUTPUT :
	   ((type == HID_FEATURE_REPORT) ? HID_REPORT_TYPE_FEATURE : 0));
}

static void hiddev_buffer_usage(struct hiddev *hiddev,
				const struct hiddev_usage_ref *uref)
{
	hiddev->buffer[hiddev->head] = *uref;
	hiddev->head = (hiddev->head + 1) &
		(HIDDEV_PROXY_BUFFER_SIZE - 1);
	if (hiddev->head == hiddev->tail) {
		hiddev->tail = (hiddev->tail + 1) &
			(HIDDEV_PROXY_BUFFER_SIZE - 1);
		hiddev->dropped++;
	}
}

static void hiddev_send_event(struct hid_device *hid,
			      struct hiddev_usage_ref *uref)
{
	struct hiddev *hiddev = hid->hiddev;
	unsigned long flags;

	// spin_lock_irqsave(&hiddev->list_lock, flags);
	// list_for_each_entry(list, &hiddev->list, node) {
	// 	if (uref->field_index != HID_FIELD_INDEX_NONE ||
	// 	    (list->flags & HIDDEV_FLAG_REPORT) != 0) {
	// 		list->buffer[list->head] = *uref;
	// 		list->head = (list->head + 1) &
	// 			(HIDDEV_BUFFER_SIZE - 1);
	// 		kill_fasync(&list->fasync, SIGIO, POLL_IN);
	// 	}
	// }
	// spin_unlock_irqrestore(&hiddev->list_lock, flags);
	// Firmware has no per-open fd list/fasync. Keep a single bounded proxy
	// ring and wake any future proxy reader without blocking HID callbacks.
	spin_lock_irqsave(&hiddev->list_lock, flags);
	hiddev_buffer_usage(hiddev, uref);
	spin_unlock_irqrestore(&hiddev->list_lock, flags);

	wake_up_interruptible(&hiddev->wait);
}

/*
 * This is where hid.c calls into hiddev to pass an event that occurred over
 * the interrupt pipe
 */
void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value)
{
	unsigned type = field->report_type;
	struct hiddev_usage_ref uref;

	uref.report_type = hiddev_report_type(type);
	uref.report_id = field->report->id;
	uref.field_index = field->index;
	uref.usage_index = (usage - field->usage);
	uref.usage_code = usage->hid;
	uref.value = value;

	hiddev_send_event(hid, &uref);
}
EXPORT_SYMBOL_GPL(hiddev_hid_event);

void hiddev_report_event(struct hid_device *hid, struct hid_report *report)
{
	unsigned type = report->type;
	struct hiddev_usage_ref uref;

	memset(&uref, 0, sizeof(uref));
	uref.report_type = hiddev_report_type(type);
	uref.report_id = report->id;
	uref.field_index = HID_FIELD_INDEX_NONE;

	hiddev_send_event(hid, &uref);
}

static int hiddev_alloc_minor(struct hiddev *hiddev)
{
	for (int i = 0; i < HIDDEV_MINORS; i++) {
		unsigned int minor = HIDDEV_MINOR_BASE + (unsigned int)i;
		struct hiddev *old;

		list_for_each_entry(old, &hiddev_table, list) {
			if ((unsigned int)old->minor == minor)
				goto next_minor;
		}

		hiddev->minor = (int)minor;
		return 0;

next_minor:
		continue;
	}

	return -ENOSPC;
}

/*
 * This is where hid.c calls us to connect a hid device to the hiddev driver
 */
int hiddev_connect(struct hid_device *hid, unsigned int force)
{
	struct hiddev *hiddev;
	int retval;

	if (!force) {
		unsigned int i;
		for (i = 0; i < hid->maxcollection; i++)
			if (hid->collection[i].type ==
			    HID_COLLECTION_APPLICATION &&
			    !IS_INPUT_APPLICATION(hid->collection[i].usage))
				break;

		if (i == hid->maxcollection)
			return -EINVAL;
	}

	if (!(hiddev = kzalloc_obj(struct hiddev)))
		return -ENOMEM;

	init_waitqueue_head(&hiddev->wait);
	INIT_LIST_HEAD(&hiddev->list);
	spin_lock_init(&hiddev->list_lock);
	mutex_init(&hiddev->existancelock);
	hid->hiddev = hiddev;
	hiddev->hid = hid;
	hiddev->exist = 1;
	retval = hiddev_alloc_minor(hiddev);
	if (retval) {
		// hid_err(hid, "Not able to get a minor for this device\n");
		// Firmware proxy has no USB minor allocator; it uses a small local
		// hiddev minor table instead.
		hid->hiddev = NULL;
		kfree(hiddev);
		return retval;
	}
	list_add_tail(&hiddev->list, &hiddev_table);

	/*
	 * If HID_QUIRK_NO_INIT_REPORTS is set, make sure we don't initialize
	 * the reports.
	 */
	hiddev->initialized = hid->quirks & HID_QUIRK_NO_INIT_REPORTS;

	// hiddev->minor = usbhid->intf->minor;
	// Minor was assigned by hiddev_alloc_minor() because there is no Linux
	// usb_register_dev() char-device minor in firmware.

	return 0;
}

/*
 * This is where hid.c calls us to disconnect a hiddev device from the
 * corresponding hid device (usually because the usb device has disconnected)
 */
// static struct usb_class_driver hiddev_class;
void hiddev_disconnect(struct hid_device *hid)
{
	struct hiddev *hiddev = hid->hiddev;
	struct hiddev_proxy *proxy;

	// usb_deregister_dev(usbhid->intf, &hiddev_class);
	// Firmware proxy has no Linux USB char device to deregister.

	mutex_lock(&hiddev->existancelock);
	hiddev->exist = 0;
	list_del(&hiddev->list);

	for (size_t i = 0; i < ARRAY_SIZE(hiddev_proxies); i++) {
		proxy = &hiddev_proxies[i];
		if (proxy->hiddev == hiddev) {
			proxy->hiddev = NULL;
			proxy->handle = HIDDEV_PROXY_INVALID_HANDLE;
		}
	}

	// if (hiddev->open) {
	// 	hid_hw_close(hiddev->hid);
	// 	wake_up_interruptible(&hiddev->wait);
	// 	mutex_unlock(&hiddev->existancelock);
	// } else {
	// 	mutex_unlock(&hiddev->existancelock);
	// 	kfree(hiddev);
	// }
	// No Linux file open count exists in firmware; proxy handles are revoked
	// above and the hiddev object can be released immediately.
	wake_up_interruptible(&hiddev->wait);
	mutex_unlock(&hiddev->existancelock);
	hid->hiddev = NULL;
	kfree(hiddev);
}

/*
 * Upstream exposes the remaining hiddev operations through file_operations and
 * ioctl(). Firmware has no userspace fd table, so in-process callers use this
 * bounded proxy API directly.
 */
struct hiddev *hiddev_find_by_minor(unsigned int minor)
{
	struct hiddev *hiddev;

	list_for_each_entry(hiddev, &hiddev_table, list) {
		if ((unsigned int)hiddev->minor == minor)
			return hiddev;
	}

	return NULL;
}

int hiddev_proxy_open_minor(unsigned int minor, hiddev_proxy_handle_t *handle)
{
	struct hiddev *hiddev = hiddev_find_by_minor(minor);
	struct hiddev_proxy *free_proxy = NULL;

	if (!hiddev)
		return -ENODEV;

	for (size_t i = 0; i < ARRAY_SIZE(hiddev_proxies); i++) {
		struct hiddev_proxy *proxy = &hiddev_proxies[i];

		if (!proxy->handle) {
			free_proxy = proxy;
			break;
		}
	}

	if (!free_proxy)
		return -ENOSPC;

	free_proxy->handle = hiddev_next_handle++;
	if (hiddev_next_handle == HIDDEV_PROXY_INVALID_HANDLE)
		hiddev_next_handle++;
	free_proxy->hiddev = hiddev;
	hiddev->open++;
	*handle = free_proxy->handle;
	return 0;
}

int hiddev_proxy_release(hiddev_proxy_handle_t handle)
{
	for (size_t i = 0; i < ARRAY_SIZE(hiddev_proxies); i++) {
		struct hiddev_proxy *proxy = &hiddev_proxies[i];

		if (proxy->handle == handle) {
			if (proxy->hiddev)
				proxy->hiddev->open--;
			proxy->handle = HIDDEV_PROXY_INVALID_HANDLE;
			proxy->hiddev = NULL;
			return 0;
		}
	}

	return -ENODEV;
}

int hiddev_proxy_read_usage(hiddev_proxy_handle_t handle,
			    struct hiddev_usage_ref *uref, __u32 *dropped)
{
	struct hiddev *hiddev = NULL;
	unsigned long flags;

	for (size_t i = 0; i < ARRAY_SIZE(hiddev_proxies); i++) {
		struct hiddev_proxy *proxy = &hiddev_proxies[i];

		if (proxy->handle == handle) {
			hiddev = proxy->hiddev;
			break;
		}
	}

	if (!hiddev)
		return -ENODEV;

	spin_lock_irqsave(&hiddev->list_lock, flags);
	if (hiddev->tail == hiddev->head) {
		spin_unlock_irqrestore(&hiddev->list_lock, flags);
		return -EAGAIN;
	}

	*uref = hiddev->buffer[hiddev->tail];
	hiddev->tail = (hiddev->tail + 1) &
		(HIDDEV_PROXY_BUFFER_SIZE - 1);
	if (dropped) {
		*dropped = hiddev->dropped;
		hiddev->dropped = 0;
	}
	spin_unlock_irqrestore(&hiddev->list_lock, flags);

	return 0;
}

int hiddev_proxy_get_next_minor(unsigned int after_minor, unsigned int *minor)
{
	struct hiddev *hiddev;
	unsigned int best = 0;

	list_for_each_entry(hiddev, &hiddev_table, list) {
		unsigned int minor_value = (unsigned int)hiddev->minor;

		if (after_minor != HIDDEV_PROXY_ENUM_BEGIN &&
		    minor_value <= after_minor)
			continue;
		if (!best || minor_value < best)
			best = minor_value;
	}

	if (!best)
		return -ENOENT;

	*minor = best;
	return 0;
}
