/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef DEVICE_H
#define DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

#define MAX_DEVICES 8
#define DEVICE_EVENT_QUEUE_LEN 16
#define DEVICE_EVENT_SET_LEN (MAX_DEVICES * DEVICE_EVENT_QUEUE_LEN + MAX_DEVICES)

#define CAP_MOUSE	0x1
#define CAP_MOUSE_ABS	0x2
#define CAP_KEYBOARD	0x4
#define CAP_KEY		0x8

#define DEVICE_INPUT_REMOVED	0xffffu
#define DEVICE_INPUT_RESET	0xfffeu

/*
 * Upstream keyd reads Linux struct input_event from an evdev fd. Firmware
 * queues keep only the fields keyd consumes here; timestamping stays in
 * evloop.c.
 */
struct input_event {
	uint16_t type;
	uint16_t code;
	int32_t value;
};

struct device {
	QueueHandle_t ev_queue;
	uint8_t capabilities;
	char id[16];
	char name[32];
	int32_t _maxx;
	int32_t _maxy;
	int32_t _minx;
	int32_t _miny;
	int32_t _pending_rel_x;
	int32_t _pending_rel_y;
	int32_t _pending_abs_x;
	int32_t _pending_abs_y;
	uint8_t _pending_abs;
	void *data;
	void (*destroy)(struct device *dev);
};

struct device_event {
	enum {
		DEV_KEY,
		DEV_LED,

		DEV_MOUSE_MOVE,
		/* All absolute values are relative to a resolution of 1024x1024. */
		DEV_MOUSE_MOVE_ABS,
		DEV_MOUSE_SCROLL,
		DEV_RESET,

		DEV_REMOVED,
	} type;

	uint8_t code;
	uint8_t pressed;

	int32_t x;
	int32_t y;
};

extern QueueHandle_t devmon_queue;
extern struct device *device_table[MAX_DEVICES];
extern size_t device_table_sz;

void devmon_init(void);
int device_add(struct device *dev);
void device_delete(struct device *dev);
struct device_event *device_read_event(struct device *dev);
QueueSetMemberHandle_t device_select(int timeout);

#endif
