#ifndef ERGOTYPE_PLATFORM_GPIO_H
#define ERGOTYPE_PLATFORM_GPIO_H

#include <stdbool.h>
#include <stdint.h>

typedef int platform_gpio_t;
typedef void (*platform_gpio_irq_cb_t)(platform_gpio_t pin, void *user);

bool platform_gpio_is_valid(platform_gpio_t pin);
int platform_gpio_input_pullup(platform_gpio_t pin);
int platform_gpio_input_pulldown(platform_gpio_t pin);
int platform_gpio_read(platform_gpio_t pin);
int platform_gpio_output(platform_gpio_t pin, bool value);
int platform_gpio_write(platform_gpio_t pin, bool value);
int platform_gpio_irq_falling(platform_gpio_t pin, platform_gpio_irq_cb_t cb, void *user);

#endif
