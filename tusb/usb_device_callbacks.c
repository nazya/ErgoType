#include "tusb.h"
#include "led/plain.h"
#include "led/ws2812.h"
#include "log.h"
#include "usb_descriptors.h"

extern uint8_t mode;

static const char *usb_mode_str(void)
{
    return mode == MSC ? "MSC" : "HID";
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    dbg("usb mounted (%s)", usb_mode_str());
    gpio_led_set_pattern(0);
    if (mode == MSC) {
        ws2812_set(WS2812_RED, 0xFFFFFFFFu);
    }
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    dbg("usb unmounted (%s)", usb_mode_str());
    gpio_led_set_pattern(LED_PATTERN_FAST);
    if (mode == MSC) {
        ws2812_set(WS2812_BLUE, 0xFFFFFFFFu);
    }
    // watchdog_reboot(0, 0, 0); does not work here
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    dbg("usb suspended (%s, remote_wakeup=%d)", usb_mode_str(), (int)remote_wakeup_en);
    (void) remote_wakeup_en;
    gpio_led_set_pattern(0x00000001u);
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    dbg("usb resumed (%s)", usb_mode_str());
    gpio_led_set_pattern(0);
}
