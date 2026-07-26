/*
 * ErgoType - vkbd backend implementation.
 *
 * This is "our unique" part vs upstream keyd:
 * instead of emitting OS input events, we enqueue events for the firmware
 * HID task(s) to consume.
 */

#include <string.h>
#include <stdio.h>

#include "platform/time.h"
#include "platform/vkbd_events.h"

#include "log.h"
#include "keyd.h"
#include "vkbd_event.h"
#include "vkbd.h"
#include "ui/ui.h"

#define QUEUE_SEND_TIMEOUT_MS 3

struct vkbd {
	uint8_t unused;
};

static void enqueue_event(const vkbd_event_t *event)
{
	if (platform_vkbd_event_send(event, QUEUE_SEND_TIMEOUT_MS) != 0)
		warn("vkbd: enqueue failed (queue full)");
}

struct vkbd *vkbd_init(const char *name)
{
	static struct vkbd vkbd;

	(void)name;

	return &vkbd;
}

void vkbd_mouse_move(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;
	vkbd_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_MOUSE_MOVE;
	event.x = (int16_t)x;
	event.y = (int16_t)y;

	enqueue_event(&event);
}

void vkbd_mouse_move_abs(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;
	vkbd_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_MOUSE_MOVE_ABS;
	event.x = (int16_t)x;
	event.y = (int16_t)y;

	enqueue_event(&event);
}

void vkbd_mouse_scroll(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;

	vkbd_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_MOUSE_SCROLL;
	event.x = (int16_t)x; // horizontal (pan)
	event.y = (int16_t)y; // vertical (wheel)

	enqueue_event(&event);
}

static void write_key_event(const struct vkbd *vkbd, uint8_t code, int state)
{
	(void)vkbd;

	uint8_t btn = 0;
	int is_btn = 1;

	switch (code) {
	case KEYD_LEFT_MOUSE:
		btn = 1u << 0;
		break;
	case KEYD_RIGHT_MOUSE:
		btn = 1u << 1;
		break;
	case KEYD_MIDDLE_MOUSE:
		btn = 1u << 2;
		break;
	case KEYD_MOUSE_1:
	case KEYD_MOUSE_BACK:
		btn = 1u << 3;
		break;
	case KEYD_MOUSE_2:
	case KEYD_MOUSE_FORWARD:
		btn = 1u << 4;
		break;
	default:
		is_btn = 0;
		break;
	}

	vkbd_event_t event;
	memset(&event, 0, sizeof(event));

	if (is_btn) {
		event.type = KEY_EVENT_MOUSE_BUTTON;
		event.buttons = btn;
		event.state = (uint8_t)!!state;

		/*
		 * Give key events preceding a mouse click
		 * a chance to propagate to avoid event
		 * order transposition. A bit kludegy,
		 * but better than waiting for all events
		 * to propagate and then checking them
		 * on re-entry.
		 *
		 * TODO: fixme (maybe)
		 */
		platform_sleep_ms(1);
	} else {
		event.type = KEY_EVENT_KEY;
		event.code = code;
		event.state = (uint8_t)!!state;
	}

	enqueue_event(&event);
}

void vkbd_send_key(const struct vkbd *vkbd, uint8_t code, int state)
{
	char line[UI_OUTPUT_EVENT_LINE_LEN + 1u];

	dbg("output %s %s", KEY_NAME(code), state == 1 ? "down" : "up");
	(void)snprintf(line, sizeof(line), "%s %s", KEY_NAME(code), state == 1 ? "down" : "up");
	ui_notify_output_event(line);

	write_key_event(vkbd, code, state);
}

void free_vkbd(struct vkbd *vkbd)
{
	(void)vkbd;
}
