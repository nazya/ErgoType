#pragma once

#include <stdint.h>

struct input_dev;

// Upstream Linux: no equivalent. KeyD-facing capability/stat ABI after Linux input flow.
#define FORWARD_CAP_MOUSE	0x1
#define FORWARD_CAP_MOUSE_ABS	0x2
#define FORWARD_CAP_KEYBOARD	0x4
#define FORWARD_CAP_KEY	0x8

struct forward_stats {
    uint32_t key_dropped;
    uint32_t key_release_dropped;
    uint32_t key_reset_queued;
    uint32_t key_reset_dropped;
    uint32_t mouse_move_dropped;
    uint32_t mouse_move_abs_dropped;
    uint32_t mouse_scroll_dropped;
};

void *forward_register_device(uint16_t vendor, uint16_t product, uint8_t caps);
void forward_update_device_caps(void *dev, uint8_t caps);
void forward_add_device_caps(void *dev, uint8_t caps);
void forward_unregister_device(void *dev);
void forward_key(void *dev, uint16_t code, uint8_t pressed);
void forward_mouse_move(void *dev, int32_t x, int32_t y);
void forward_mouse_move_abs(void *dev, int32_t x, int32_t y);
void forward_mouse_scroll(void *dev, int32_t x, int32_t y);
void forward_get_stats(void *dev, struct forward_stats *stats);
