/*
 * ErgoType - TinyUSB HID backend for keyd vkbd events.
 *
 * This file intentionally contains all HID report packing + tud_* calls.
 * daemon.c stays close to upstream keyd and only talks to vkbd_*.
 */

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "daemon.h"
#include "log.h"

#define HID_READY_WAIT_TICKS 2

static uint8_t mods;
static uint8_t keys[6];

static int8_t clamp_i16_to_i8(int16_t v)
{
	/*
	 * Our current HID mouse report (and TinyUSB's `tud_hid_mouse_report()`) uses
	 * 8-bit relative X/Y deltas. Clamp int16_t deltas to avoid wrap-around when
	 * the sensor reports large movements.
	 */
	if (v > 127)
		return 127;
	if (v < -127)
		return -127;
	return (int8_t)v;
}

static void send_hid_report(void)
{
	struct hid_report {
		uint8_t hid_mods;
		uint8_t reserved;
		uint8_t hid_code[6];
	};
	struct hid_report report;

	for (int i = 0; i < 6; i++)
		report.hid_code[i] = keys[i];

	report.hid_mods = mods;

	while (!tud_hid_ready())
		vTaskDelay(HID_READY_WAIT_TICKS);

	tud_hid_keyboard_report(REPORT_ID_KEYBOARD, report.hid_mods, report.hid_code);
}

static uint8_t get_modifier(int code)
{
	switch (code) {
	case KEYD_LEFTSHIFT:
		return HID_SHIFT;
	case KEYD_RIGHTSHIFT:
		return HID_RIGHTSHIFT;
	case KEYD_LEFTCTRL:
		return HID_CTRL;
	case KEYD_RIGHTCTRL:
		return HID_RIGHTCTRL;
	case KEYD_LEFTALT:
		return HID_ALT;
	case KEYD_RIGHTALT:
		return HID_ALT_GR;
	case KEYD_LEFTMETA:
		return HID_SUPER;
	case KEYD_RIGHTMETA:
		return HID_RIGHTSUPER;
	default:
		return 0;
	}
}

static void update_key_state(uint16_t code, int state)
{
	int set = 0;
	uint8_t hid_code = hid_table[code];

	for (int i = 0; i < 6; i++) {
		if (keys[i] == hid_code) {
			set = 1;
			if (state == 0)
				keys[i] = 0;
		}
	}

	if (state && !set) {
		for (int i = 0; i < 6; i++) {
			if (keys[i] == 0) {
				keys[i] = hid_code;
				break;
			}
		}
	}
}

static int update_modifier_state(int code, int state)
{
	uint16_t mod = get_modifier(code);

	if (!mod)
		return -1;

	if (state)
		mods |= mod;
	else
		mods &= ~mod;

	return 0;
}

static void backend_send_key(uint8_t code, int state)
{
	if (update_modifier_state(code, state) < 0)
		update_key_state(code, state);

	send_hid_report();
}

static void backend_mouse_scroll(int x, int y)
{
	while (!tud_hid_ready())
		vTaskDelay(HID_READY_WAIT_TICKS);

	/* TinyUSB: wheel=vertical, pan=horizontal. */
	tud_hid_mouse_report(REPORT_ID_MOUSE, 0, 0, 0, (int8_t)y, (int8_t)x);
}

void key_event_hid_task(void *pvParameters)
{
	(void)pvParameters;

	/*
	 * keyd_queue is created by keyd_task() in daemon.c.
	 * This task can be started earlier, so wait for the queue to exist.
	 */
	while (!keyd_queue)
		vTaskDelay(1);

	key_event_t event;

	while (1) {
		if (xQueueReceive(keyd_queue, &event, portMAX_DELAY) != pdPASS)
			continue;

		if (event.type == KEY_EVENT_MOUSE_MOVE) {
			while (!tud_hid_ready())
				vTaskDelay(HID_READY_WAIT_TICKS);

			tud_hid_mouse_report(
				REPORT_ID_MOUSE,
				event.buttons,
				clamp_i16_to_i8(event.x),
				clamp_i16_to_i8(event.y),
				0,
				0
			);
			continue;
		}

		if (event.type == KEY_EVENT_MOUSE_MOVE_ABS) {
			/*
			 * TODO: absolute mouse mode backend.
			 * For now treat as relative deltas.
			 */
			while (!tud_hid_ready())
				vTaskDelay(HID_READY_WAIT_TICKS);

			tud_hid_mouse_report(
				REPORT_ID_MOUSE,
				event.buttons,
				clamp_i16_to_i8(event.x),
				clamp_i16_to_i8(event.y),
				0,
				0
			);
			continue;
		}

		switch (event.code) {
		case KEYD_SCROLL_DOWN:
			if (event.state)
				backend_mouse_scroll(0, -1);
			break;
		case KEYD_SCROLL_UP:
			if (event.state)
				backend_mouse_scroll(0, 1);
			break;
		case KEYD_SCROLL_RIGHT:
			if (event.state)
				backend_mouse_scroll(1, 0);
			break;
		case KEYD_SCROLL_LEFT:
			if (event.state)
				backend_mouse_scroll(-1, 0);
			break;
		default:
			backend_send_key(event.code, event.state);
			break;
		}
	}
}
