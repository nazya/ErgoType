#include "platform/gpio.h"

#include "led/led.h"

void gpio_led_hw_init(int8_t pin)
{
    platform_gpio_init(pin);
    platform_gpio_set_dir(pin, PLATFORM_GPIO_OUT);
    platform_gpio_put(pin, false);
}

void gpio_led_hw_put(int8_t pin, bool on)
{
    platform_gpio_put(pin, on);
}
