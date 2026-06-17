#include "FreeRTOS.h"
#include "queue.h"

#include "forward.h"

void forward_key(struct device *dev, uint8_t code, uint8_t pressed)
{
	struct device_event ev = {0};

	ev.type = DEV_KEY;
	ev.code = code;
	ev.pressed = pressed;
	xQueueSendToBack(dev->ev_queue, &ev, 0);
}

void forward_mouse_move(struct device *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};

	ev.type = DEV_MOUSE_MOVE;
	ev.x = x;
	ev.y = y;
	xQueueSendToBack(dev->ev_queue, &ev, 0);
}

void forward_mouse_move_abs(struct device *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};

	ev.type = DEV_MOUSE_MOVE_ABS;
	ev.x = x;
	ev.y = y;
	xQueueSendToBack(dev->ev_queue, &ev, 0);
}

void forward_mouse_scroll(struct device *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};

	ev.type = DEV_MOUSE_SCROLL;
	ev.x = x;
	ev.y = y;
	xQueueSendToBack(dev->ev_queue, &ev, 0);
}

void forward_removed(struct device *dev)
{
	struct device_event ev = {0};

	ev.type = DEV_REMOVED;
	xQueueSendToBack(dev->ev_queue, &ev, 0);
}
