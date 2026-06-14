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

struct device {
	QueueHandle_t events;
	uint8_t capabilities;
	char id[16];
	char name[32];
	void *data;
};

struct device_event {
	enum {
		DEV_KEY,
		DEV_LED,

		DEV_MOUSE_MOVE,
		/* All absolute values are relative to a resolution of 1024x1024. */
		DEV_MOUSE_MOVE_ABS,
		DEV_MOUSE_SCROLL,

		DEV_REMOVED,
	} type;

	uint8_t code;
	uint8_t pressed;

	int32_t x;
	int32_t y;
};

extern QueueHandle_t devmon_queue;
extern struct device device_table[MAX_DEVICES];
extern size_t device_table_sz;

void devmon_init(void);
void device_add(const struct device *dev);
void device_delete(struct device *dev);
struct device_event *device_read_event(struct device *dev);
QueueSetMemberHandle_t device_select(int timeout);

#endif
