/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */
/*
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */
#ifndef _HIDDEV_H
#define _HIDDEV_H

#include <uapi/linux/hiddev.h>

/*
 * In-kernel definitions.
 */

/*
 * #define HIDDEV_BUFFER_SIZE	2048
 * Firmware has no Linux hiddev file descriptors. Keep one bounded proxy ring
 * per hiddev instance instead of allocating one 2048-entry list per opener.
 */
#define HIDDEV_PROXY_BUFFER_SIZE 64

struct hiddev {
	int minor;
	int exist;
	int open;
	struct mutex existancelock;
	wait_queue_head_t wait;
	struct hid_device *hid;
	struct list_head list;
	spinlock_t list_lock;
	bool initialized;

	/*
	 * Upstream events are buffered in struct hiddev_list for each open file.
	 * The firmware proxy has no file table, so events are kept here until a
	 * future proxy reader drains them.
	 */
	struct hiddev_usage_ref buffer[HIDDEV_PROXY_BUFFER_SIZE];
	int head;
	int tail;
	__u32 dropped;
};

struct hid_device;
struct hid_usage;
struct hid_field;
struct hid_report;

/*
 * Upstream hiddev callers identify open files by struct file. Firmware readers
 * use opaque handles into the bounded proxy table instead.
 */
typedef u32 hiddev_proxy_handle_t;

#define HIDDEV_PROXY_INVALID_HANDLE 0
#define HIDDEV_PROXY_ENUM_BEGIN ((unsigned int)-1)

#ifdef CONFIG_USB_HIDDEV
int hiddev_connect(struct hid_device *hid, unsigned int force);
void hiddev_disconnect(struct hid_device *);
void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value);
void hiddev_report_event(struct hid_device *hid, struct hid_report *report);
struct hiddev *hiddev_find_by_minor(unsigned int minor);
int hiddev_proxy_open_minor(unsigned int minor, hiddev_proxy_handle_t *handle);
int hiddev_proxy_release(hiddev_proxy_handle_t handle);
int hiddev_proxy_read_usage(hiddev_proxy_handle_t handle,
			    struct hiddev_usage_ref *uref, __u32 *dropped);
int hiddev_proxy_get_next_minor(unsigned int after_minor, unsigned int *minor);
#else
static inline int hiddev_connect(struct hid_device *hid,
		unsigned int force)
{ return -1; }
static inline void hiddev_disconnect(struct hid_device *hid) { }
static inline void hiddev_hid_event(struct hid_device *hid, struct hid_field *field,
		      struct hid_usage *usage, __s32 value) { }
static inline void hiddev_report_event(struct hid_device *hid, struct hid_report *report) { }
#endif

#endif
