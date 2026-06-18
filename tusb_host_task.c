#include "bsp/board.h"
#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

void tusb_host_task(void *pvParameters)
{
    (void)pvParameters;

    tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };

    (void)tusb_init(BOARD_TUH_RHPORT, &rh_init);

    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    while (1) {
        tuh_task();
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)dev_addr;
    (void)instance;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)report;
    (void)len;

    tuh_hid_receive_report(dev_addr, instance);
}
