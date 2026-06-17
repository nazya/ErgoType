#pragma once

#include <stdint.h>

#include "device.h"

void forward_key(struct device *dev, uint8_t code, uint8_t pressed);
void forward_mouse_move(struct device *dev, int32_t x, int32_t y);
void forward_mouse_move_abs(struct device *dev, int32_t x, int32_t y);
void forward_mouse_scroll(struct device *dev, int32_t x, int32_t y);
void forward_removed(struct device *dev);
