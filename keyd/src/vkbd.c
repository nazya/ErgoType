/*
 * ErgoType - vkbd backend implementation.
 *
 * This is "our unique" part vs upstream keyd:
 * instead of emitting OS input events, we enqueue events for the firmware
 * HID task(s) to consume.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "daemon.h"
#include "log.h"
#include "vkbd.h"

static const TickType_t vkbd_queue_send_timeout_ticks = pdMS_TO_TICKS(3);

static void enqueue_key_event(uint8_t code, uint8_t state)
{
	key_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_KEY;
	event.code = code;
	event.state = state;

	if (!keyd_queue)
		return;

	if (xQueueSendToBack(keyd_queue, &event, vkbd_queue_send_timeout_ticks) != pdPASS)
		warn("vkbd: enqueue failed (queue full)");
}

struct vkbd *vkbd_init(const char *name)
{
	(void)name;
	return NULL;
}

void vkbd_mouse_move(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;
	key_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_MOUSE_MOVE;
	event.x = (int16_t)x;
	event.y = (int16_t)y;

	if (!keyd_queue)
		return;

	if (xQueueSendToBack(keyd_queue, &event, vkbd_queue_send_timeout_ticks) != pdPASS)
		warn("vkbd: enqueue failed (queue full)");
}

void vkbd_mouse_move_abs(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;
	key_event_t event;
	memset(&event, 0, sizeof(event));
	event.type = KEY_EVENT_MOUSE_MOVE_ABS;
	event.x = (int16_t)x;
	event.y = (int16_t)y;

	if (!keyd_queue)
		return;

	if (xQueueSendToBack(keyd_queue, &event, vkbd_queue_send_timeout_ticks) != pdPASS)
		warn("vkbd: enqueue failed (queue full)");
}

void vkbd_mouse_scroll(const struct vkbd *vkbd, int x, int y)
{
	(void)vkbd;

	if (y < 0)
		enqueue_key_event(KEYD_SCROLL_DOWN, 1);
	else if (y > 0)
		enqueue_key_event(KEYD_SCROLL_UP, 1);

	if (x < 0)
		enqueue_key_event(KEYD_SCROLL_LEFT, 1);
	else if (x > 0)
		enqueue_key_event(KEYD_SCROLL_RIGHT, 1);
}

void vkbd_send_key(const struct vkbd *vkbd, uint8_t code, int state)
{
	(void)vkbd;
	enqueue_key_event(code, (uint8_t)!!state);
}

void free_vkbd(struct vkbd *vkbd)
{
	(void)vkbd;
}
