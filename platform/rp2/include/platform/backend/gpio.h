#ifndef ERGOTYPE_RP2_PLATFORM_BACKEND_GPIO_H
#define ERGOTYPE_RP2_PLATFORM_BACKEND_GPIO_H

#include "hardware/gpio.h"

static inline void platform_gpio_put(uint gpio, bool value)
{
    gpio_put(gpio, value);
}

static inline int platform_gpio_get(uint gpio)
{
    return gpio_get(gpio);
}

#endif
