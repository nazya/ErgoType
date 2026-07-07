#pragma once

#include <stdint.h>

struct hid_device;
struct input_dev;

// Upstream Linux: no equivalent. KeyD-facing capability/stat ABI after Linux input flow.
#define EVDEV_CAP_MOUSE	0x1
#define EVDEV_CAP_MOUSE_ABS	0x2
#define EVDEV_CAP_KEYBOARD	0x4
#define EVDEV_CAP_KEY	0x8

struct evdev_stats {
	uint32_t key_dropped;
	uint32_t key_release_dropped;
	uint32_t key_reset_queued;
	uint32_t key_reset_dropped;
	uint32_t rel_dropped;
	uint32_t abs_dropped;
	uint32_t other_dropped;
};

void *evdev_register_device(uint16_t vendor, uint16_t product, uint8_t caps);
void evdev_update_device_caps(void *dev, uint8_t caps);
void evdev_add_device_caps(void *dev, uint8_t caps);
void evdev_configure_abs(void *dev, int32_t minx, int32_t maxx, int32_t miny, int32_t maxy);
void evdev_unregister_device(void *dev);
void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value);
void evdev_get_stats(void *dev, struct evdev_stats *stats);
int evdev_init(void);
int evdev_activate_hid(struct hid_device *hid);
void evdev_deactivate_hid(struct hid_device *hid);
