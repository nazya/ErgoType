#pragma once

#include <stdint.h>

uint8_t hid_keyboard_to_keyd(uint16_t usage);
uint8_t hid_modifier_to_keyd(uint8_t bit);
uint8_t hid_usage_to_keyd(uint16_t usage_page, uint16_t usage);
