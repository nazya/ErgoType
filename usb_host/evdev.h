#pragma once

#include <stdint.h>

#include "devmon.h"

struct evdev_stats {
	uint32_t key_dropped;
	uint32_t key_release_dropped;
	uint32_t key_reset_queued;
	uint32_t key_reset_dropped;
	uint32_t rel_dropped;
	uint32_t abs_dropped;
	uint32_t other_dropped;
};

void *evdev_register_device(const struct port_input_dev *port_dev);
void evdev_unregister_device(void *dev);
void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value);
void evdev_get_stats(void *dev, struct evdev_stats *stats);
int evdev_init(void);
