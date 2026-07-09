/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HID_VIVALDI_COMMON_H
#define _HID_VIVALDI_COMMON_H

#include <linux/input/vivaldi-fmap.h>
#include <linux/types.h>

struct hid_device;
struct hid_field;
struct hid_report;
struct hid_usage;

struct vivaldi_drvdata {
	/*
	 * Upstream drivers store struct vivaldi_data directly in drvdata.
	 * Keep it first so the upstream helper contract remains true while the
	 * port tracks async GET_REPORT state next to it.
	 */
	struct vivaldi_data data;
	struct hid_report *feature_report;
	bool feature_report_pending;
	bool feature_report_ready;
};

void vivaldi_feature_mapping(struct hid_device *hdev,
			     struct hid_field *field, struct hid_usage *usage);

extern const struct attribute_group *vivaldi_attribute_groups[];

#endif /* _HID_VIVALDI_COMMON_H */
