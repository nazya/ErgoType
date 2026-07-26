// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reduced firmware HIDRAW lifecycle.
 *
 * The upstream HID core still owns the call sites and HID_CLAIMED_HIDRAW.
 * There is deliberately no VFS, /dev node, fd/ioctl emulation, global
 * 64-device table, or report allocation. No report is retained until a real
 * internal consumer defines the bounded subscription it needs.
 */
#include "linux/include/linux/hidraw.h"

static unsigned int hidraw_next_minor;

int hidraw_init(void)
{
	return 0;
}

void hidraw_exit(void)
{
	/* Firmware has no module unload or HIDRAW class state. */
}

int hidraw_connect(struct hid_device *hid)
{
	struct hidraw *raw;

	raw = kzalloc_obj(struct hidraw);
	if (!raw)
		return -ENOMEM;

	raw->minor = hidraw_next_minor++;
	hid->hidraw = raw;
	return 0;
}

int hidraw_report_event(struct hid_device *hid, u8 *data, int len)
{
	/*
	 * No subscriber exists in this stage. The driver raw_event path and parsed
	 * input path have already received the report; retaining another copy would
	 * add heap/report-time cost without a consumer.
	 */
	(void)hid;
	(void)data;
	(void)len;
	return 0;
}

void hidraw_disconnect(struct hid_device *hid)
{
	struct hidraw *raw = hid->hidraw;

	hid->hidraw = NULL;
	kfree(raw);
}
