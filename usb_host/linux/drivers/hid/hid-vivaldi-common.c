// SPDX-License-Identifier: GPL-2.0
/*
 * Helpers for ChromeOS HID Vivaldi keyboards
 *
 * Copyright (C) 2022 Google, Inc
 */

#include <linux/export.h>
#include <linux/hid.h>
#include <linux/input/vivaldi-fmap.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>

#include "usb_host/hid_async.h"

#include "hid-vivaldi-common.h"

#define MIN_FN_ROW_KEY 1
#define MAX_FN_ROW_KEY VIVALDI_MAX_FUNCTION_ROW_KEYS
#define HID_VD_FN_ROW_PHYSMAP 0x00000001
#define HID_USAGE_FN_ROW_PHYSMAP (HID_UP_GOOGLEVENDOR | HID_VD_FN_ROW_PHYSMAP)

/*
 * Port async continuation helper: feature_mapping() returns before GET_REPORT
 * completion, so completion scans the whole report and fills the usages the
 * upstream stack frame would have filled one at a time.
 */
static void vivaldi_store_function_row_physmap(struct hid_device *hdev,
					       struct hid_report *report)
{
	struct vivaldi_data *data = hid_get_drvdata(hdev);
	int i, j;

	for (i = 0; i < report->maxfield; i++) {
		struct hid_field *field = report->field[i];

		for (j = 0; j < field->maxusage; j++) {
			struct hid_usage *usage = &field->usage[j];
			unsigned int fn_key;

			if (field->logical != HID_USAGE_FN_ROW_PHYSMAP ||
			    (usage->hid & HID_USAGE_PAGE) != HID_UP_ORDINAL)
				continue;

			fn_key = usage->hid & HID_USAGE;
			if (fn_key < MIN_FN_ROW_KEY || fn_key > MAX_FN_ROW_KEY)
				continue;

			if (fn_key > data->num_function_row_keys)
				data->num_function_row_keys = fn_key;

			data->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
				field->value[usage->usage_index];
		}
	}
}

static void vivaldi_feature_mapping_complete(const struct hid_async_request *req,
					     int status)
{
	struct vivaldi_data *data = hid_get_drvdata(req->hid);
	struct vivaldi_drvdata *drvdata =
		container_of(data, struct vivaldi_drvdata, data);
	u8 *report_data = (u8 *)req->data;
	u32 report_len = req->actual_len;
	int ret;

	drvdata->feature_report_pending = false;

	if (status < 0)
		return;

	if (!req->report->id) {
		report_data++;
		report_len--;
	}

	ret = hid_report_raw_event(req->hid, HID_FEATURE_REPORT, report_data,
				   report_len, report_len, 0);
	if (ret) {
		dev_warn(&req->hid->dev, "failed to report feature %d\n",
			 req->report->id);
		return;
	}
	vivaldi_store_function_row_physmap(req->hid, req->report);
	drvdata->feature_report = req->report;
	drvdata->feature_report_ready = true;
}

/**
 * vivaldi_feature_mapping - Fill out vivaldi keymap data exposed via HID
 * @hdev: HID device to parse
 * @field: HID field to parse
 * @usage: HID usage to parse
 *
 * Note: this function assumes that driver data attached to @hdev contains an
 * instance of &struct vivaldi_data at the very beginning.
 */
void vivaldi_feature_mapping(struct hid_device *hdev,
			     struct hid_field *field, struct hid_usage *usage)
{
	struct vivaldi_data *data = hid_get_drvdata(hdev);
	struct vivaldi_drvdata *drvdata =
		container_of(data, struct vivaldi_drvdata, data);
	struct hid_report *report = field->report;
	// u8 *report_data, *buf;
	// u32 report_len;
	// Port stores the transfer buffer in hid_async_request; keep only length.
	u32 report_len;
	unsigned int fn_key;
	int ret;

	if (field->logical != HID_USAGE_FN_ROW_PHYSMAP ||
	    (usage->hid & HID_USAGE_PAGE) != HID_UP_ORDINAL)
		return;

	fn_key = usage->hid & HID_USAGE;
	if (fn_key < MIN_FN_ROW_KEY || fn_key > MAX_FN_ROW_KEY)
		return;

	if (fn_key > data->num_function_row_keys)
		data->num_function_row_keys = fn_key;

	if (drvdata->feature_report_ready && drvdata->feature_report == report) {
		data->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
			field->value[usage->usage_index];
		return;
	}

	if (drvdata->feature_report_pending)
		return;

	// report_data = buf = hid_alloc_report_buf(report, GFP_KERNEL);
	// if (!report_data)
	// 	return;
	//
	// report_len = hid_report_len(report);
	// if (!report->id) {
	// 	/*
	// 	 * hid_hw_raw_request() will stuff report ID (which will be 0)
	// 	 * into the first byte of the buffer even for unnumbered
	// 	 * reports, so we need to account for this to avoid getting
	// 	 * -EOVERFLOW in return.
	// 	 * Note that hid_alloc_report_buf() adds 7 bytes to the size
	// 	 * so we can safely say that we have space for an extra byte.
	// 	 */
	// 	report_len++;
	// }
	//
	// ret = hid_hw_raw_request(hdev, report->id, report_data,
	// 			 report_len, HID_FEATURE_REPORT,
	// 			 HID_REQ_GET_REPORT);
	// if (ret < 0) {
	// 	dev_warn(&hdev->dev, "failed to fetch feature %d\n",
	// 		 field->report->id);
	// 	goto out;
	// }
	//
	// if (!report->id) {
	// 	/*
	// 	 * Undo the damage from hid_hw_raw_request() for unnumbered
	// 	 * reports.
	// 	 */
	// 	report_data++;
	// 	report_len--;
	// }
	//
	// ret = hid_report_raw_event(hdev, HID_FEATURE_REPORT, report_data,
	// 			   report_len, report_len, 0);
	// if (ret) {
	// 	dev_warn(&hdev->dev, "failed to report feature %d\n",
	// 		 field->report->id);
	// 	goto out;
	// }
	//
	// data->function_row_physmap[fn_key - MIN_FN_ROW_KEY] =
	// 	field->value[usage->usage_index];
	//
	// out:
	// kfree(buf);
	// hid_hw_raw_request() is synchronous. Queue the same raw GET_REPORT as
	// an async request and resume this feature parse from completion.
	report_len = hid_report_len(report);
	if (!report->id)
		report_len++;
	drvdata->feature_report = report;
	drvdata->feature_report_ready = false;
	drvdata->feature_report_pending = true;
	ret = hid_async_queue_raw_get_report(hdev, report, report_len,
					     vivaldi_feature_mapping_complete,
					     drvdata);
	if (ret < 0) {
		drvdata->feature_report = NULL;
		drvdata->feature_report_pending = false;
		dev_warn(&hdev->dev, "failed to queue feature %d\n",
			 field->report->id);
		return;
	}
}
EXPORT_SYMBOL_GPL(vivaldi_feature_mapping);

static ssize_t function_row_physmap_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct vivaldi_data *data = hid_get_drvdata(hdev);

	return vivaldi_function_row_physmap_show(data, buf);
}

static DEVICE_ATTR_RO(function_row_physmap);
static struct attribute *vivaldi_sysfs_attrs[] = {
	&dev_attr_function_row_physmap.attr,
	NULL
};

static umode_t vivaldi_is_visible(struct kobject *kobj, struct attribute *attr,
				  int n)
{
	struct hid_device *hdev = to_hid_device(kobj_to_dev(kobj));
	struct vivaldi_data *data = hid_get_drvdata(hdev);

	if (!data->num_function_row_keys)
		return 0;
	return attr->mode;
}

static const struct attribute_group vivaldi_attribute_group = {
	.attrs = vivaldi_sysfs_attrs,
	.is_visible = vivaldi_is_visible,
};

const struct attribute_group *vivaldi_attribute_groups[] = {
	&vivaldi_attribute_group,
	NULL,
};
EXPORT_SYMBOL_GPL(vivaldi_attribute_groups);

MODULE_DESCRIPTION("Helpers for ChromeOS HID Vivaldi keyboards");
MODULE_LICENSE("GPL");
