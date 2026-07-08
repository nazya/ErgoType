#pragma once

#include <stdint.h>

#include "linux/include/uapi/linux/input-event-codes.h"

#define EVDEV_KEYMASK_WORDS (BTN_LEFT / 32 + 1)

struct evdev_stats {
	uint32_t key_dropped;
	uint32_t key_release_dropped;
	uint32_t key_reset_queued;
	uint32_t key_reset_dropped;
	uint32_t rel_dropped;
	uint32_t abs_dropped;
	uint32_t other_dropped;
};

struct evdev_input_info {
	uint32_t keymask[EVDEV_KEYMASK_WORDS];
	uint32_t num_keys;
	uint8_t relmask;
	uint8_t absmask;
	int32_t minx;
	int32_t maxx;
	int32_t miny;
	int32_t maxy;
};

void *evdev_register_device(uint16_t vendor, uint16_t product,
			    const struct evdev_input_info *info);
void evdev_unregister_device(void *dev);
void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value);
void evdev_get_stats(void *dev, struct evdev_stats *stats);
int evdev_init(void);
