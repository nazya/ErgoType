#include "tusb.h"

#include "devmon.h"
#include "ui/ui.h"
#include "usb_descriptors.h"
#include "usb_webhid.h"

void vkbd_hid_notify_recheck(void); // keyd/port/vkbd/tusb_hid.c

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) report;
    (void) len;

    if (instance == HID_KEYBOARD_INSTANCE || instance == HID_MOUSE_INSTANCE)
        vkbd_hid_notify_recheck();
}

// Invoked when an IN REPORT transfer completes unsuccessfully.
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t report_type,
                              uint8_t const* report, uint16_t xferred_bytes)
{
    (void) report;
    (void) xferred_bytes;

    if (report_type == HID_REPORT_TYPE_INPUT &&
        (instance == HID_KEYBOARD_INSTANCE || instance == HID_MOUSE_INSTANCE))
        vkbd_hid_notify_recheck();
}

// Invoked when received GET_REPORT control request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    if (webhid_is_instance(instance))
        return webhid_get_report(report_id, report_type, buffer, reqlen);

    return 0;
}

// Invoked when received SET_REPORT control request or data on OUT endpoint
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    if (webhid_is_instance(instance)) {
        webhid_set_report(report_id, report_type, buffer, bufsize);
        return;
    }

    if (report_type != HID_REPORT_TYPE_OUTPUT || bufsize < 1)
        return;
    if (report_id != 0 && report_id != REPORT_ID_KEYBOARD)
        return;

    uint8_t leds = buffer[0];
    ui_led_set_pattern(0, (leds & 0x02u) ? 0xFFFFFFFFu : 0u, true);

    // KeyD owns the physical device table, so route host LED state through its queue.
    struct devmon_event event = {
        .type = DEVMON_LED,
        .code = LED_CAPSL,
        .value = !!(leds & 0x02u),
        .is_virtual = true,
    };
    xQueueSendToBack(devmon_queue, &event, portMAX_DELAY);
}
