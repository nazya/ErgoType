#include "tusb.h"
#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_webhid.h"
#include "ui/ui.h"

extern uint8_t mode;

/*
 * Bus state as TinyUSB callbacks deliver it, in tud_task() order.
 * tud_suspended() flips earlier, in the IRQ; acting on it could send a
 * wakeup that the still-queued tud_suspend_cb() would then re-arm.
 */
static bool bus_suspended;
static bool wakeup_sent; /* at most one remote wakeup per suspend */

bool usb_device_is_suspended(void)
{
    return bus_suspended;
}

void usb_device_request_wakeup(void)
{
    if (!wakeup_sent)
        wakeup_sent = tud_remote_wakeup();
}

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
    /* Bus reset/configuration change aborts any prior feature transaction. */
    webhid_reset();
    ui_led_set_pattern(0, 0, true);
    bus_suspended = false;
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
    bus_suspended = true;
    wakeup_sent = false;
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    ui_led_set_pattern(0, 0, true);
    bus_suspended = false;
}
