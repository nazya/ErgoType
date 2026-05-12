#include "tusb.h"
#include "usb_descriptors.h"
#include "ui/ui.h"

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
    ui_led_set_pattern(0, true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    ui_led_set_pattern(LED_PATTERN_FAST, true);
    // watchdog_reboot(0, 0, 0); does not work here
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    ui_led_set_pattern(0x00000001u, true);
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    ui_led_set_pattern(0, true);
}
