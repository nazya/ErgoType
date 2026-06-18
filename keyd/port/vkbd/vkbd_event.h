/*
 * ErgoType - vkbd event queue.
 *
 * This is a port-specific interface between keyd output (vkbd.c)
 * and the USB HID backend task (tusb_hid.c).
 */
#ifndef VKBD_EVENT_H
#define VKBD_EVENT_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

extern QueueHandle_t vkbd_event_queue;

typedef struct {
	uint8_t type;
	uint8_t code;   /* Key code */
	uint8_t state;  /* Key state: 1 for down, 0 for up */
	uint8_t buttons;
	int16_t x;
	int16_t y;
} vkbd_event_t;

enum vkbd_event_type {
	KEY_EVENT_KEY = 0,
	KEY_EVENT_MOUSE_MOVE = 1,
	KEY_EVENT_MOUSE_MOVE_ABS = 2,
	KEY_EVENT_MOUSE_BUTTON = 3,
	// Mouse wheel/pan (HID: wheel=vertical, pan=horizontal). Uses x=pan, y=wheel.
	KEY_EVENT_MOUSE_SCROLL = 4,
};

#endif
