/* Upstream TinyUSB/Linux: no equivalent; firmware's sole host-owner task. */
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
int linux_module_initcalls_init(void);
int hid_builtin_drivers_init(void);

enum {
    USBH_PORT_ENUM_EVENT_NONE = 0,
    USBH_PORT_ENUM_EVENT_FAILED = 1,
    USBH_PORT_ENUM_EVENT_TIMEOUT = 2,
    USBH_PORT_ENUM_EVENT_ATTACH_OVERFLOW = 3,
};

/* Current linked call graph reaches this after every TinyUSB callback unwinds. */
void tuh_port_enum_event_cb(uint8_t event)
{
    if (event == USBH_PORT_ENUM_EVENT_FAILED)
        async_msg("ERR: HID_ENUM_FAIL");
    else if (event == USBH_PORT_ENUM_EVENT_TIMEOUT)
        async_msg("ERR: HID_ENUM_TIMEOUT");
    else if (event == USBH_PORT_ENUM_EVENT_ATTACH_OVERFLOW)
        async_msg("ERR: HID_ATTACH_OVERFLOW");
}

uint32_t tusb_time_millis_api(void)
{
    return (uint32_t)xTaskGetTickCount() * (uint32_t)portTICK_PERIOD_MS;
}

void tusb_host_task(void *pvParameters)
{
    int ret;

    (void)pvParameters;

    /* TinyUSB callbacks and tuh_task() are owned by this task exclusively. */
    hid_async_host_task_register();

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

    /*
     * Upstream Linux runs module/initcall registration before HID devices bind.
     * Firmware has no module loader, so run the collected initcalls explicitly.
     */
    ret = linux_module_initcalls_init();
    if (ret) {
        async_msg("ERR: HID_INITCALL_FAIL");
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

    /*
     * USB reset selects Report protocol and Linux usbhid emits no generic
     * SET_PROTOCOL. Keep TinyUSB's class metadata aligned with that default;
     * task-side usbhid_parse() owns the probe-time wire-level HID class setup.
     */
    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
    ret = tusb_init(BOARD_TUH_RHPORT, &host_init) ? 0 : -EIO;
    if (ret) {
        async_msg("ERR: TUSB_HOST_FAIL");
        while (1)
            vTaskDelay(portMAX_DELAY);
    }

    while (1)
        tuh_task();
}
