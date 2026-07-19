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

#include "devmon.h"

#define CAP_MOUSE	0x1
#define CAP_MOUSE_ABS	0x2
#define CAP_KEYBOARD	0x4
#define CAP_KEY		0x8 // Can emit keys, but is not necessarily a keyboard

enum haptic_effect_index {
	HAPTIC_EFFECT_CLICK,
	HAPTIC_EFFECT_BUZZ,
	HAPTIC_EFFECT_RUMBLE,
	HAPTIC_EFFECT_PRESS,
	HAPTIC_EFFECT_RELEASE,
	HAPTIC_EFFECT_COUNT,
};

struct haptic_state;

struct device {
	QueueHandle_t ev_queue;
	struct evdev_writer writer;
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
	// Port: optional device-local FF effect IDs initialized from the add snapshot.
	struct haptic_state *haptic;
	void *data;
};

struct device_event {
	enum device_event_type type;

	uint8_t code;
	uint8_t pressed;

	int32_t x;
	int32_t y;
};

extern struct device *device_table[MAX_DEVICES];
extern size_t device_table_sz;

int device_init(const struct port_input_dev *port_dev, struct device *dev);
struct device_event *device_read_event(struct device *dev);
void device_set_led(const struct device *dev, int led, int state);
void haptic_init(struct device *dev);
void haptic_cleanup(struct device *dev);
int device_haptic_upload(struct device *dev, enum haptic_effect_index effect,
			 const struct ff_effect *upload);
int device_haptic_play(const struct device *dev,
		       enum haptic_effect_index effect, int value);
int device_haptic_erase(struct device *dev, enum haptic_effect_index effect);

#endif
