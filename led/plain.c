#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "led/plain.h"

#define STEP_MS 250

// 32-step ring, advanced every STEP_MS. Bit 0 is output first.
static volatile uint32_t pattern = 0;
static volatile bool pattern_loop = true;
static TaskHandle_t led_task_handle;

static inline uint32_t rot_r32(uint32_t v)
{
    return (v >> 1) | (v << 31);
}

void gpio_led_set_pattern(uint32_t p, bool loop) {
    pattern = p;
    pattern_loop = loop;

    // Wake the LED task so it applies the new pattern immediately (even if the value is unchanged).
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return;
    TaskHandle_t h = led_task_handle;
    if (h)
        xTaskNotifyGive(h);
}

void gpio_led_task(void* pvParameters) {
    const int8_t pin = *(const int8_t*)pvParameters;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, false);

    led_task_handle = xTaskGetCurrentTaskHandle();

    bool loop = pattern_loop;
    uint32_t ring = pattern;

    for (;;) {
        gpio_put(pin, (ring & 0x01u) != 0);
        ring = loop ? rot_r32(ring) : (ring >> 1);

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STEP_MS))) {
            loop = pattern_loop;
            ring = pattern;
        }
    }
}
