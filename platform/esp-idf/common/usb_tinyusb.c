#include "platform/cdc.h"
#include "platform/usb.h"

#include <stdio.h>

#include "platform/os.h"

#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"

#if BOARD_TUD_MAX_SPEED == OPT_MODE_HIGH_SPEED
#define ERGOTYPE_USB_PHY_TARGET USB_PHY_TARGET_UTMI
#define ERGOTYPE_USB_PHY_SPEED USB_PHY_SPEED_HIGH
#define ERGOTYPE_TUSB_SPEED TUSB_SPEED_HIGH
#else
#define ERGOTYPE_USB_PHY_TARGET USB_PHY_TARGET_INT
#define ERGOTYPE_USB_PHY_SPEED USB_PHY_SPEED_UNDEFINED
#define ERGOTYPE_TUSB_SPEED TUSB_SPEED_FULL
#endif

void platform_usb_device_task(void *pvParameters)
{
    (void)pvParameters;

    usb_phy_handle_t phy_handle = NULL;
    const usb_phy_config_t phy_config = {
        .controller = USB_PHY_CTRL_OTG,
        .target = ERGOTYPE_USB_PHY_TARGET,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = ERGOTYPE_USB_PHY_SPEED,
    };

    esp_err_t err = usb_new_phy(&phy_config, &phy_handle);
    if (err != ESP_OK) {
        printf("usb_new_phy failed: %s\n", esp_err_to_name(err));
        vTaskDelete(NULL);
    }

    const tusb_rhport_init_t rhport_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = ERGOTYPE_TUSB_SPEED,
    };

    if (!tusb_rhport_init(BOARD_TUD_RHPORT, &rhport_init)) {
        printf("tusb_rhport_init failed\n");
        vTaskDelete(NULL);
    }

    for (;;) {
        tud_task();
        platform_cdc_poll();
    }
}
