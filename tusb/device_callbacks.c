#include "tusb.h"
#include "device.h"
#include "descriptors.h"
#include "webhid.h"
#include "ui.h"
#include "device/usbd_pvt.h"
#include "cdc.h"
#include "usb_host/usbhid.h"

extern uint8_t mode;

#ifndef USBHID_PM_FIXTURE
static void usb_device_wakeup(void *context)
{
    (void)context;
    if (usb_device_is_suspended())
        usb_device_request_wakeup();
}

void usbhid_pm_wakeup_cb(void)
{
    usbd_defer_func(usb_device_wakeup, NULL, false);
}

void usbhid_pm_state_changed_cb(enum usbhid_pm_state state, int status)
{
    if (status) {
        ui_notify_err();
        if (!tud_cdc_connected())
            return;
        char message[ASYNC_MSG_BUFSIZE];

        snprintf(message, sizeof(message), "ERR: HID_PM_%s e%d",
                 state == USBHID_PM_SUSPENDED ? "SUSPEND" : "RESUME", status);
        _async_msg(message);
        return;
    }
    if (!tud_cdc_connected())
        return;
    if (state == USBHID_PM_SUSPENDED)
        async_msg("INFO: HID_PM_SUSPENDED");
    else
        async_msg("INFO: HID_PM_ACTIVE");
}
#endif

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
#ifndef USBHID_PM_FIXTURE
    if (mode == HID)
        usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
#endif
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    webhid_reset();
    ui_led_set_pattern(0, LED_PATTERN_FAST, true);
#ifndef USBHID_PM_FIXTURE
    if (mode == HID)
        usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
#endif
    // watchdog_reboot(0, 0, 0); does not work here
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    ui_led_set_pattern(0, remote_wakeup_en ? 0x00000001u : 0x00000005u, true);
    usb_device_on_suspend();
#ifndef USBHID_PM_FIXTURE
    if (mode == HID)
        usbhid_pm_set_target(USBHID_PM_SUSPENDED, remote_wakeup_en);
#endif
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    ui_led_set_pattern(0, 0, true);
    usb_device_on_resume();
#ifndef USBHID_PM_FIXTURE
    if (mode == HID)
        usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
#endif
}
