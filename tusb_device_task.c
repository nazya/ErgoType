#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"

void tusb_device_task(void *pvParameters)
{
    TickType_t delay_ticks = pdMS_TO_TICKS(2);
    if (pvParameters)
        delay_ticks = *(const TickType_t *)pvParameters;

    while (1) {
        tud_task(); // TinyUSB device task
        vTaskDelay(delay_ticks);
    }
}
