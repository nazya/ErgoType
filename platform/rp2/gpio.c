#include "platform/gpio.h"

#include "hardware/gpio.h"

void platform_gpio_init(uint gpio)
{
    gpio_init(gpio);
}

void platform_gpio_set_dir(uint gpio, bool out)
{
    gpio_set_dir(gpio, out);
}

void platform_gpio_pull_up(uint gpio)
{
    gpio_pull_up(gpio);
}

void platform_gpio_pull_down(uint gpio)
{
    gpio_pull_down(gpio);
}

void platform_gpio_disable_pulls(uint gpio)
{
    gpio_disable_pulls(gpio);
}

void platform_gpio_set_function(uint gpio, int fn)
{
    gpio_function_t pico_fn = GPIO_FUNC_SIO;
    if (fn == PLATFORM_GPIO_FUNC_SPI)
        pico_fn = GPIO_FUNC_SPI;
    else if (fn == PLATFORM_GPIO_FUNC_I2C)
        pico_fn = GPIO_FUNC_I2C;

    gpio_set_function(gpio, pico_fn);
}

void platform_gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled)
{
    gpio_set_irq_enabled(gpio, events, enabled);
}

void platform_gpio_set_irq_enabled_with_callback(
    uint gpio,
    uint32_t events,
    bool enabled,
    platform_gpio_irq_callback_t callback)
{
    gpio_set_irq_enabled_with_callback(gpio, events, enabled, callback);
}
