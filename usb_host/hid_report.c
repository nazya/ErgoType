#include <stdint.h>
#include <string.h>

#include "tusb.h"

#include "keys.h"
#include "forward.h"
#include "hid_keyd.h"
#include "hid.h"

static uint8_t has_key(const hid_keyboard_report_t *report, uint8_t key)
{
	for (uint8_t i = 0; i < 6; i++)
		if (report->keycode[i] == key)
			return 1;

	return 0;
}

static struct hid_report_state *report_state(struct hid *hid, uint8_t report_id)
{
	for (uint8_t i = 0; i < HOST_HID_MAX_REPORTS; i++)
		if (hid->reports[i].used && hid->reports[i].report_id == report_id)
			return &hid->reports[i];

	for (uint8_t i = 0; i < HOST_HID_MAX_REPORTS; i++)
		if (!hid->reports[i].used) {
			hid->reports[i].used = 1;
			hid->reports[i].report_id = report_id;
			hid->reports[i].len = 0;
			memset(hid->reports[i].data, 0, sizeof(hid->reports[i].data));
			return &hid->reports[i];
		}

	return NULL;
}

static uint32_t read_bits(const uint8_t *data, uint16_t len, uint16_t bit_offset, uint8_t bit_size)
{
	uint32_t value = 0;
	uint16_t end = bit_offset + bit_size;

	if (bit_size > 32 || end > len * 8u)
		return 0;

	for (uint8_t i = 0; i < bit_size; i++)
		if (data[(bit_offset + i) / 8u] & (1u << ((bit_offset + i) % 8u)))
			value |= 1u << i;

	return value;
}

static uint16_t hid_field_usage(const struct hid_field *field, uint16_t index)
{
	uint32_t usage;

	if (index < field->usage_count)
		return field->usages[index];

	if (!field->has_usage_min || !field->has_usage_max)
		return 0;

	usage = field->usage_min + index;
	if (usage > field->usage_max)
		return 0;

	return (uint16_t)usage;
}

static uint8_t array_value_valid(const struct hid_field *field, uint16_t usage)
{
	if (!usage)
		return 0;

	if (field->has_usage_min && usage < field->usage_min)
		return 0;

	if (field->has_usage_max && usage > field->usage_max)
		return 0;

	return !!hid_usage_to_keyd(field->usage_page, usage);
}

static uint16_t array_value_to_usage(const struct hid_field *field, uint16_t value)
{
	int32_t offset;
	uint32_t usage;

	if (!value)
		return 0;

	if (!field->has_usage_min || !field->has_usage_max)
		return value;

	if (value >= field->usage_min && value <= field->usage_max)
		return value;

	offset = (int32_t)value - field->logical_min;
	if (offset < 0)
		return value;

	usage = field->usage_min + offset;
	if (usage > field->usage_max)
		return value;

	return (uint16_t)usage;
}

static uint8_t array_has_usage(const struct hid_field *field, const uint8_t *report, uint16_t len, uint16_t usage)
{
	for (uint16_t i = 0; i < field->report_count; i++) {
		uint16_t bit_offset = field->bit_offset + i * field->report_size;
		uint16_t value = read_bits(report, len, bit_offset, field->report_size);

		if (array_value_to_usage(field, value) == usage)
			return 1;
	}

	return 0;
}

static int32_t scaled_abs(const struct hid_field *field, int32_t value)
{
	int32_t min = field->logical_min;
	int32_t max = field->logical_max;

	if (max <= min)
		return value;

	return (int32_t)(((int64_t)value - min) * 1024 / (max - min));
}

uint8_t hid_field_caps(const struct hid_field *field)
{
	uint8_t caps = 0;

	switch (field->usage_page) {
	case HID_USAGE_PAGE_KEYBOARD:
		return CAP_KEY | CAP_KEYBOARD;
	case HID_USAGE_PAGE_BUTTON:
		return CAP_KEY | CAP_MOUSE;
	case HID_USAGE_PAGE_CONSUMER:
		caps |= CAP_KEY;
		for (uint16_t i = 0; i < field->report_count; i++)
			if (hid_field_usage(field, i) == HID_USAGE_CONSUMER_AC_PAN)
				caps |= CAP_MOUSE;
		return caps;
	case HID_USAGE_PAGE_DESKTOP:
		for (uint16_t i = 0; i < field->report_count; i++) {
			uint16_t usage = hid_field_usage(field, i);

			if (usage == HID_USAGE_DESKTOP_X ||
			    usage == HID_USAGE_DESKTOP_Y ||
			    usage == HID_USAGE_DESKTOP_WHEEL)
				caps |= CAP_MOUSE;
			if (hid_usage_to_keyd(field->usage_page, usage))
				caps |= CAP_KEY;
			if (!(field->flags & HID_RELATIVE) &&
			    (usage == HID_USAGE_DESKTOP_X || usage == HID_USAGE_DESKTOP_Y))
				caps |= CAP_MOUSE_ABS;
		}
		return caps;
	default:
		/* Parsed only. Add a CAP_* and event mapping here to support this HID page. */
		return 0;
	}
}

void handle_keyboard_report(struct hid *hid, const hid_keyboard_report_t *report)
{
	uint8_t changed_mods = hid->prev_mods ^ report->modifier;

	for (uint8_t i = 0; i < 8; i++)
		if (changed_mods & (1u << i))
			forward_key(&hid->device, hid_modifier_to_keyd(i), !!(report->modifier & (1u << i)));

	for (uint8_t i = 0; i < 6; i++) {
		uint8_t key = hid->prev_keyboard.keycode[i];
		uint8_t code = hid_keyboard_to_keyd(key);

		if (code && !has_key(report, key))
			forward_key(&hid->device, code, 0);
	}

	for (uint8_t i = 0; i < 6; i++) {
		uint8_t key = report->keycode[i];
		uint8_t code = hid_keyboard_to_keyd(key);

		if (code && !has_key(&hid->prev_keyboard, key))
			forward_key(&hid->device, code, 1);
	}

	hid->prev_mods = report->modifier;
	hid->prev_keyboard = *report;
}

void handle_mouse_report(struct hid *hid, const hid_mouse_report_t *report)
{
	uint8_t changed = hid->prev_buttons ^ report->buttons;

	if (changed & MOUSE_BUTTON_LEFT)
		forward_key(&hid->device, KEYD_LEFT_MOUSE, !!(report->buttons & MOUSE_BUTTON_LEFT));
	if (changed & MOUSE_BUTTON_RIGHT)
		forward_key(&hid->device, KEYD_RIGHT_MOUSE, !!(report->buttons & MOUSE_BUTTON_RIGHT));
	if (changed & MOUSE_BUTTON_MIDDLE)
		forward_key(&hid->device, KEYD_MIDDLE_MOUSE, !!(report->buttons & MOUSE_BUTTON_MIDDLE));
	if (changed & MOUSE_BUTTON_BACKWARD)
		forward_key(&hid->device, KEYD_MOUSE_BACK, !!(report->buttons & MOUSE_BUTTON_BACKWARD));
	if (changed & MOUSE_BUTTON_FORWARD)
		forward_key(&hid->device, KEYD_MOUSE_FORWARD, !!(report->buttons & MOUSE_BUTTON_FORWARD));

	if (report->x || report->y)
		forward_mouse_move(&hid->device, report->x, report->y);

	if (report->wheel || report->pan)
		forward_mouse_scroll(&hid->device, report->pan, report->wheel);

	hid->prev_buttons = report->buttons;
}

static void handle_array_field(struct hid *hid,
			       const struct hid_field *field,
			       const uint8_t *report,
			       uint16_t report_len,
			       const uint8_t *prev,
			       uint16_t prev_len)
{
	for (uint16_t i = 0; i < field->report_count; i++) {
		uint16_t bit_offset = field->bit_offset + i * field->report_size;
		uint16_t value = read_bits(prev, prev_len, bit_offset, field->report_size);
		uint16_t usage = array_value_to_usage(field, value);
		uint8_t code;

		if (!array_value_valid(field, usage))
			continue;
		if (array_has_usage(field, report, report_len, usage))
			continue;

		code = hid_usage_to_keyd(field->usage_page, usage);
		if (code)
			forward_key(&hid->device, code, 0);
	}

	for (uint16_t i = 0; i < field->report_count; i++) {
		uint16_t bit_offset = field->bit_offset + i * field->report_size;
		uint16_t value = read_bits(report, report_len, bit_offset, field->report_size);
		uint16_t usage = array_value_to_usage(field, value);
		uint8_t code;

		if (!array_value_valid(field, usage))
			continue;
		if (array_has_usage(field, prev, prev_len, usage))
			continue;

		code = hid_usage_to_keyd(field->usage_page, usage);
		if (code)
			forward_key(&hid->device, code, 1);
	}
}

static void handle_variable_field(struct hid *hid,
				  const struct hid_field *field,
				  const uint8_t *report,
				  uint16_t report_len,
				  const uint8_t *prev,
				  uint16_t prev_len,
				  int32_t *rel_x,
				  int32_t *rel_y,
				  int32_t *wheel,
				  int32_t *pan,
				  uint8_t *abs_changed)
{
	for (uint16_t i = 0; i < field->report_count; i++) {
		uint16_t bit_offset = field->bit_offset + i * field->report_size;
		uint16_t usage = hid_field_usage(field, i);
		uint32_t raw = read_bits(report, report_len, bit_offset, field->report_size);
		uint32_t prev_raw = read_bits(prev, prev_len, bit_offset, field->report_size);
		uint8_t code;

		if (!usage)
			continue;

		if (field->flags & HID_RELATIVE) {
			int32_t value = sign_extend(raw, field->report_size);

			if (!value)
				continue;
			if (field->usage_page == HID_USAGE_PAGE_DESKTOP) {
				if (usage == HID_USAGE_DESKTOP_X)
					*rel_x += value;
				else if (usage == HID_USAGE_DESKTOP_Y)
					*rel_y += value;
				else if (usage == HID_USAGE_DESKTOP_WHEEL)
					*wheel += value;
			} else if (field->usage_page == HID_USAGE_PAGE_CONSUMER && usage == HID_USAGE_CONSUMER_AC_PAN) {
				*pan += value;
			}
			continue;
		}

		if (field->usage_page == HID_USAGE_PAGE_DESKTOP &&
		    (usage == HID_USAGE_DESKTOP_X || usage == HID_USAGE_DESKTOP_Y)) {
			int32_t value = scaled_abs(field, sign_extend(raw, field->report_size));

			if (usage == HID_USAGE_DESKTOP_X) {
				hid->abs_x = value;
				hid->has_abs_x = 1;
			} else {
				hid->abs_y = value;
				hid->has_abs_y = 1;
			}
			if (raw != prev_raw)
				*abs_changed = 1;
			continue;
		}

		code = hid_usage_to_keyd(field->usage_page, usage);
		if (code && !!raw != !!prev_raw)
			forward_key(&hid->device, code, !!raw);
	}
}

void handle_hid_report(struct hid *hid,
		       uint8_t report_id,
		       const uint8_t *report,
		       uint16_t report_len)
{
	static const uint8_t zero[HOST_HID_REPORT_LEN];
	struct hid_report_state *state = report_state(hid, report_id);
	const uint8_t *prev = state ? state->data : zero;
	uint16_t prev_len = state ? state->len : 0;
	uint16_t len = report_len > HOST_HID_REPORT_LEN ? HOST_HID_REPORT_LEN : report_len;
	int32_t rel_x = 0;
	int32_t rel_y = 0;
	int32_t wheel = 0;
	int32_t pan = 0;
	uint8_t abs_changed = 0;

	for (uint8_t i = 0; i < hid->field_count; i++) {
		const struct hid_field *field = &hid->fields[i];

		if (field->report_id != report_id)
			continue;

		if (field->flags & HID_VARIABLE)
			handle_variable_field(hid, field, report, len, prev, prev_len, &rel_x, &rel_y, &wheel, &pan, &abs_changed);
		else
			handle_array_field(hid, field, report, len, prev, prev_len);
	}

	if (rel_x || rel_y)
		forward_mouse_move(&hid->device, rel_x, rel_y);
	if (wheel || pan)
		forward_mouse_scroll(&hid->device, pan, wheel);
	if (abs_changed && hid->has_abs_x && hid->has_abs_y)
		forward_mouse_move_abs(&hid->device, hid->abs_x, hid->abs_y);

	if (state) {
		memcpy(state->data, report, len);
		state->len = len;
	}
}
