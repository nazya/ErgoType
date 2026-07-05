/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (c) 2007 Jiri Kosina
 */
#ifndef _HIDRAW_H
#define _HIDRAW_H

#include <uapi/linux/hidraw.h>

// Upstream hidraw is a char-device userspace ABI. Firmware keeps the
// report-buffering shape and exposes proxy handles instead of Linux files.

struct hidraw {
	unsigned int minor;
	int exist;
	int open;
	wait_queue_head_t wait;
	struct hid_device *hid;
	struct device *dev;
	spinlock_t list_lock;
	struct list_head list;
	unsigned int report_active;
};

struct hidraw_report {
	__u8 *value;
	int len;
	__u32 dropped_before;
};

struct hidraw_list {
	struct hidraw_report buffer[HIDRAW_BUFFER_SIZE];
	int head;
	int tail;
	struct fasync_struct *fasync;
	struct hidraw *hidraw;
	struct list_head node;
	struct mutex read_mutex;
	bool revoked;
	__u32 dropped;
};

typedef u32 hidraw_proxy_handle_t;

#define HIDRAW_PROXY_INVALID_HANDLE 0
#define HIDRAW_PROXY_ENUM_BEGIN ((unsigned int)-1)
#define HIDRAW_PROXY_NAME_STRING 128
#define HIDRAW_PROXY_STRING 64

struct hidraw_proxy_report_enum_snapshot {
	__u32 report_count;
	__u8 numbered;
};

struct hidraw_proxy_device_snapshot {
	__u32 minor;
	__u16 bus;
	__u16 group;
	__u32 vendor;
	__u32 product;
	__u32 version;
	__u32 country;
	__u32 type;
	__u32 claimed;
	__u32 quirks;
	__u32 initial_quirks;
	__u64 firmware_version;
	__u32 dev_rsize;
	__u32 bpf_rsize;
	__u32 rsize;
	__u32 ll_rsize;
	__u32 maxcollection;
	__u32 maxapplication;
	__u32 id;
	__u32 ll_generation;
	__u8 dev_addr;
	__u8 instance;
	__u8 exist;
	__u8 io_started;
	__u8 has_report_fixup;
	__u8 has_raw_event;
	char name[HIDRAW_PROXY_NAME_STRING];
	char phys[HIDRAW_PROXY_STRING];
	char uniq[HIDRAW_PROXY_STRING];
	struct hidraw_proxy_report_enum_snapshot report_enum[HID_REPORT_TYPES];
};

int hidraw_init(void);
void hidraw_exit(void);
int hidraw_report_event(struct hid_device *, u8 *, int);
int hidraw_connect(struct hid_device *);
void hidraw_disconnect(struct hid_device *);
int hidraw_open_minor(unsigned int, struct hidraw_list **);
int hidraw_open_list(struct hidraw *, struct hidraw_list **);
int hidraw_release_list(struct hidraw_list *);
unsigned int hidraw_poll(struct hidraw_list *);
int hidraw_read_report(struct hidraw_list *, u8 *, size_t);
int hidraw_read_report_with_drops(struct hidraw_list *, u8 *, size_t, __u32 *);
int hidraw_report_descriptor_size(struct hidraw *);
int hidraw_report_descriptor(struct hidraw *, struct hidraw_report_descriptor *);
int hidraw_devinfo(struct hidraw *, struct hidraw_devinfo *);
int hidraw_name(struct hidraw *, char *, size_t);
int hidraw_phys(struct hidraw *, char *, size_t);
int hidraw_uniq(struct hidraw *, char *, size_t);
int hidraw_write_report(struct hidraw_list *, const u8 *, size_t);
int hidraw_set_report(struct hidraw_list *, const u8 *, size_t, enum hid_report_type);
int hidraw_get_report(struct hidraw_list *, u8 *, size_t, enum hid_report_type);
int hidraw_revoke(struct hidraw_list *);
struct hidraw *hidraw_find_by_minor(unsigned int);
int hidraw_for_each(int (*fn)(struct hidraw *, void *), void *);
int hidraw_proxy_open_minor(unsigned int, hidraw_proxy_handle_t *);
int hidraw_proxy_release(hidraw_proxy_handle_t);
unsigned int hidraw_proxy_poll(hidraw_proxy_handle_t);
int hidraw_proxy_read_report(hidraw_proxy_handle_t, u8 *, size_t);
int hidraw_proxy_read_report_with_drops(hidraw_proxy_handle_t, u8 *, size_t,
					__u32 *);
int hidraw_proxy_report_descriptor_size(hidraw_proxy_handle_t);
int hidraw_proxy_report_descriptor(hidraw_proxy_handle_t,
				   struct hidraw_report_descriptor *);
int hidraw_proxy_get_report_descriptor_size(unsigned int);
int hidraw_proxy_get_report_descriptor(unsigned int,
				       struct hidraw_report_descriptor *);
int hidraw_proxy_devinfo(hidraw_proxy_handle_t, struct hidraw_devinfo *);
int hidraw_proxy_name(hidraw_proxy_handle_t, char *, size_t);
int hidraw_proxy_phys(hidraw_proxy_handle_t, char *, size_t);
int hidraw_proxy_uniq(hidraw_proxy_handle_t, char *, size_t);
int hidraw_proxy_write_report(hidraw_proxy_handle_t, const u8 *, size_t);
int hidraw_proxy_set_report(hidraw_proxy_handle_t, const u8 *, size_t,
			    enum hid_report_type);
int hidraw_proxy_get_report(hidraw_proxy_handle_t, u8 *, size_t,
			    enum hid_report_type);
int hidraw_proxy_revoke(hidraw_proxy_handle_t);
int hidraw_proxy_get_device_snapshot(unsigned int,
				     struct hidraw_proxy_device_snapshot *);
int hidraw_proxy_get_next_device_snapshot(unsigned int,
					  struct hidraw_proxy_device_snapshot *);

#endif
