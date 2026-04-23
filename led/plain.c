#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "led/plain.h"

#define STEP_MS 250

// 32-step ring, advanced every STEP_MS. Bit 0 is output first.
static volatile uint32_t pattern = 0;

void gpio_led_set_pattern(uint32_t p) {
    pattern = p;
}

void gpio_led_task(void* pvParameters) {
    const int8_t pin = *(const int8_t*)pvParameters;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, false);

    uint32_t last_pattern = pattern;
    uint32_t ring = last_pattern;

    while (1) {
        uint32_t p = pattern;

        // Reset ring on change.
        if (p != last_pattern) {
            last_pattern = p;
            ring = p;
        }

        gpio_put(pin, ring & 0x01u);
        ring = (ring >> 1) | (ring << 31);

        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}
