#include "device.h"

#include "bsp/board.h"
#include "stdio_cdc.h"
#include "hid.h"
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * Bus state as TinyUSB callbacks deliver it, in tud_task() order.
 * tud_suspended() flips earlier, in the IRQ; acting on it could send a
 * wakeup that the still-queued tud_suspend_cb() would then re-arm.
 */
static bool bus_suspended;
static bool wakeup_sent; /* at most one remote wakeup per suspend */

void usb_device_on_suspend(void)
{
    bus_suspended = true;
    wakeup_sent = false;
}

void usb_device_on_resume(void)
{
    bus_suspended = false;
}

bool usb_device_is_suspended(void)
{
    return bus_suspended;
}

void usb_device_request_wakeup(void)
{
    if (!wakeup_sent)
        wakeup_sent = tud_remote_wakeup();
}

void tusb_device_task(void *pvParameters)
{
    (void)pvParameters;

    // Init device stack on configured roothub port.
    // With CFG_TUSB_OS=OPT_OS_FREERTOS this must run after the scheduler starts,
    // since the USB IRQ uses FreeRTOS queue APIs.
    //
    // NOTE: TinyUSB init API differs across pico-sdk/tinyusb versions:
    // - old:  bool tusb_init(void);
    // - new:  tusb_init(rhport, tusb_rhport_init_t*), with tusb_init(void) kept via macro compat.
    //
    // Use the 0-arg form to compile on both.
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    (void)tusb_init(BOARD_TUD_RHPORT, &dev_init);
#if 0
    (void)tusb_init();
#endif

    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    while (1) {
        // Wait for USB events or deferred HID/CDC work.
        tud_task();
        /* Also covers clear-halt, which has no HID completion callback. */
        hid_handle();
        stdio_cdc_handle();
    }
}
