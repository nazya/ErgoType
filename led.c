#include "hardware/gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "jconfig.h"

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

extern uint32_t blink_interval_ms;

static void led_init(int pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);  // Set as output
    gpio_put(pin, false);         // Start with the LED off
}

void led_task(void* pvParameters) {
    config_t* config = (config_t*)pvParameters;  // Cast parameter to config_t pointer

    led_init(config->led_pin);

    bool led_state = false;  // Initial LED state

    while (1) {
        gpio_put(config->led_pin, led_state);  // Set LED state
        led_state = !led_state;                // Toggle state
        vTaskDelay(pdMS_TO_TICKS(blink_interval_ms));
    }
}