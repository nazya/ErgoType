/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Firmware boundary for Linux HIDRAW lifecycle.
 *
 * Linux exposes a character device, file operations, ioctls, and one report
 * ring per open file. Firmware currently has no raw-report consumer, so it
 * retains only the upstream connect/claim/report/disconnect contract. A later
 * real consumer may add one bounded subscription without changing hid-core.
 */
#ifndef _HIDRAW_H
#define _HIDRAW_H

#include <linux/hid.h>

struct hidraw {
	unsigned int minor;
};

int hidraw_init(void);
void hidraw_exit(void);
int hidraw_report_event(struct hid_device *hid, u8 *data, int len);
int hidraw_connect(struct hid_device *hid);
void hidraw_disconnect(struct hid_device *hid);

#endif
