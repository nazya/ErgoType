#pragma once

#include <stdint.h>

#include "tusb.h"

#include "device.h"
#include "hid_parser.h"

struct hid_report_state {
	uint8_t used;
	uint8_t report_id;
	uint8_t len;
	uint8_t data[HOST_HID_REPORT_LEN];
};

struct hid {
	uint8_t dev_addr;
	uint8_t instance;
	uint8_t field_count;
	uint8_t has_report_id;
	uint8_t prev_mods;
	uint8_t prev_buttons;
	uint8_t has_abs_x;
	uint8_t has_abs_y;
	int32_t abs_x;
	int32_t abs_y;
	struct hid_field *fields;
	struct hid_report_state *reports;
	hid_keyboard_report_t prev_keyboard;
	struct device device;
};

uint8_t hid_field_caps(const struct hid_field *field);
void handle_keyboard_report(struct hid *hid, const hid_keyboard_report_t *report);
void handle_mouse_report(struct hid *hid, const hid_mouse_report_t *report);
void handle_hid_report(struct hid *hid,
		       uint8_t report_id,
		       const uint8_t *report,
		       uint16_t report_len);
