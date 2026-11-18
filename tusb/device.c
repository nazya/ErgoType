#include "device.h"

#include "tusb.h"

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
