#include "platform/board.h"
#include "platform/cdc.h"
#include "platform/usb.h"

#include "tusb.h"

void platform_usb_device_task(void *pvParameters)
{
    (void)pvParameters;

    // With CFG_TUSB_OS=OPT_OS_FREERTOS this must run after the scheduler starts,
    // since the USB IRQ uses FreeRTOS queue APIs.
    (void)tusb_init();

    platform_board_init_after_usb();

    while (1) {
        tud_task();
        platform_cdc_poll();
    }
}
