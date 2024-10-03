#include "tusb.h"
// #include "FreeRTOS.h"
// #include "task.h"

#include "bsp/board.h"
#include "led.h"


uint32_t blink_interval_ms = BLINK_NOT_MOUNTED; // blink_patterh.c

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when USB bus is resumed
void tud_resume_cb(void)
{
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

void usb_device_task(void* pvParameters) {
    (void) pvParameters;

    while (1) {
        tud_task(); // TinyUSB device task
        // vTaskDelay(pdMS_TO_TICKS(1)); // Delay for context switching
    }
}
