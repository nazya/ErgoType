#include "bsp/board.h"
#include "stdio_tusb_cdc.h"
#include "tusb.h"

#include "jconfig.h"
#include "usb_descriptors.h"

#include "FreeRTOS.h"
#include "task.h"

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
    (void)tusb_init();

    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    while (1) {
        // Wait for USB events (or a deferred "kick" from stdio_tusb_cdc_write()).
        tud_task();
        stdio_tusb_cdc_poll();
    }
}
