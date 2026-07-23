#ifndef ERGOTYPE_PLATFORM_HID_H
#define ERGOTYPE_PLATFORM_HID_H

#include <stdbool.h>
#include <stdint.h>

bool platform_hid_ready(uint8_t instance);
bool platform_hid_keyboard_report(uint8_t instance, uint8_t report_id,
                                  uint8_t modifier, const uint8_t keycode[6]);
bool platform_hid_report(uint8_t instance, uint8_t report_id,
                         const void *report, uint16_t len);
bool platform_hid_mouse_report(uint8_t instance, uint8_t report_id,
                               uint8_t buttons, int8_t x, int8_t y,
                               int8_t vertical, int8_t horizontal);

#endif
