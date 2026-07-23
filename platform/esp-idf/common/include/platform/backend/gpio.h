#ifndef ERGOTYPE_ESP_IDF_PLATFORM_BACKEND_GPIO_H
#define ERGOTYPE_ESP_IDF_PLATFORM_BACKEND_GPIO_H

#include "driver/gpio.h"

static inline void platform_gpio_put(uint gpio, bool value)
{
    (void)gpio_set_level((gpio_num_t)gpio, value ? 1 : 0);
}

static inline int platform_gpio_get(uint gpio)
{
    return gpio_get_level((gpio_num_t)gpio);
}

#endif
