#include <stdbool.h>
#include <stdint.h>

#include "pio_usb.h"
#include "tusb.h"

#include "stdio_tusb_cdc.h"
#include "evdev.h"
#include "hid_async.h"

#include "FreeRTOS.h"
#include "task.h"

int hid_core_init(void);
int hid_builtin_drivers_init(void);

void tusb_host_task(void *pvParameters)
{
    int ret;

    (void)pvParameters;

    tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;

    ret = hid_core_init();
    if (ret) {
        async_msg("ERR: HID_CORE_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    ret = evdev_init();
    if (ret) {
        async_msg("ERR: HID_EVDEV_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    ret = hid_builtin_drivers_init();
    if (ret) {
        async_msg("ERR: HID_DRIVER_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
    ret = tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg) ? 0 : -EIO;
    if (ret) {
        async_msg("ERR: TUH_CONFIG_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
    ret = tusb_init(BOARD_TUH_RHPORT, &host_init) ? 0 : -EIO;
    if (ret) {
        async_msg("ERR: TUSB_HOST_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    while (1) {
        tuh_task();
    }
}
