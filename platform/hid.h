#ifndef ERGOTYPE_PLATFORM_HID_H
#define ERGOTYPE_PLATFORM_HID_H

#include <stddef.h>
#include <stdint.h>

int platform_hid_init(void);
void platform_hid_disconnected(void);
int platform_hid_nkro_report(const uint8_t *keys, size_t key_count);
int platform_hid_mouse_report(uint8_t buttons, int16_t x, int16_t y, int16_t wheel, int16_t pan);
int platform_hid_consumer_report(const uint16_t *usages, size_t usage_count);
int platform_hid_release_all(void);
uint8_t platform_hid_keyboard_leds(void);

#endif
