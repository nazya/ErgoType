#include "bsp/board.h"
#include "FreeRTOS.h"
#include "task.h"

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

extern uint32_t blink_interval_ms;

void led_blinking_task(void) {
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if ( board_millis() - start_ms < blink_interval_ms) return; // Not enough time
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = !led_state; // Toggle
}

void led_task(void* pvParameters) {
    (void) pvParameters;

    while (1) {
        led_blinking_task();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}