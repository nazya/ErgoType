/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

struct device_event {
	enum {
		DEV_KEY,
		DEV_LED,

		DEV_MOUSE_MOVE,
		/* All absolute values are relative to a resolution of 1024x1024. */
		DEV_MOUSE_MOVE_ABS,
		DEV_MOUSE_SCROLL,

		// DEV_REMOVED,
	} type;

	uint8_t code;
	uint8_t pressed;

	int32_t x;
	int32_t y;
};

#endif

