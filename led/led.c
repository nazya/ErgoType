#include "hardware/gpio.h"

#include "led/led.h"

void gpio_led_hw_init(int8_t pin)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, false);
}

void gpio_led_hw_put(int8_t pin, bool on)
{
    gpio_put(pin, on);
}
