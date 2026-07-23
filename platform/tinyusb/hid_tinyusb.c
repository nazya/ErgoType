#include "platform/hid.h"

#include "tusb.h"

bool platform_hid_ready(uint8_t instance)
{
    return tud_hid_n_ready(instance);
}

bool platform_hid_keyboard_report(uint8_t instance, uint8_t report_id,
                                  uint8_t modifier, const uint8_t keycode[6])
{
    return tud_hid_n_keyboard_report(instance, report_id, modifier, keycode);
}

bool platform_hid_report(uint8_t instance, uint8_t report_id,
                         const void *report, uint16_t len)
{
    return tud_hid_n_report(instance, report_id, report, len);
}

bool platform_hid_mouse_report(uint8_t instance, uint8_t report_id,
                               uint8_t buttons, int8_t x, int8_t y,
                               int8_t vertical, int8_t horizontal)
{
    return tud_hid_n_mouse_report(instance, report_id, buttons, x, y, vertical, horizontal);
}
