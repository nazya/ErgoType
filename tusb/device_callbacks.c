#include "tusb.h"
#include "device.h"
#include "webhid.h"
#include "ui/ui.h"

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    /* Bus reset/configuration change aborts any prior feature transaction. */
    webhid_reset();
    ui_led_set_pattern(0, 0, true);
    usb_device_on_resume();
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    webhid_reset();
    ui_led_set_pattern(0, LED_PATTERN_FAST, true);
    // watchdog_reboot(0, 0, 0); does not work here
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    ui_led_set_pattern(0, 0x00000001u, true);
    usb_device_on_suspend();
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    ui_led_set_pattern(0, 0, true);
    usb_device_on_resume();
}
