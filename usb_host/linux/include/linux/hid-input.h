#ifndef USB_HOST_LINUX_HID_INPUT_H
#define USB_HOST_LINUX_HID_INPUT_H

#include "hid.h"
#include "input.h"

void hidinput_hid_event(struct hid_device *hid, struct hid_field *field, struct hid_usage *usage, __s32 value);
void hidinput_report_event(struct hid_device *hid, struct hid_report *report);

#endif
