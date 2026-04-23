#include "tusb.h"
#include "led/plain.h"
#include "led/ws2812.h"
#include "usb_descriptors.h"

extern uint8_t mode;

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    gpio_led_set_pattern(0);
    if (mode == MSC) {
        ws2812_set(WS2812_RED, 0xFFFFFFFFu);
    }
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    gpio_led_set_pattern(LED_PATTERN_FAST);
    if (mode == MSC) {
        ws2812_set(WS2812_BLUE, 0xFFFFFFFFu);
    }
    // watchdog_reboot(0, 0, 0); does not work here
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    gpio_led_set_pattern(0x00000001u);
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    gpio_led_set_pattern(0);
}

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) instance;
    (void) report;
    (void) len;
}

// Invoked when received GET_REPORT control request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    // Not implemented
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// Invoked when received SET_REPORT control request or data on OUT endpoint
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance;
    if (report_type != HID_REPORT_TYPE_OUTPUT || bufsize < 1)
        return;
    if (report_id != 0 && report_id != REPORT_ID_KEYBOARD)
        return;

    uint8_t leds = buffer[0];
    gpio_led_set_pattern((leds & 0x02u) ? 0xFFFFFFFFu : 0u);
}
