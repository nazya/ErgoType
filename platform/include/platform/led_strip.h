#ifndef ERGOTYPE_PLATFORM_LED_STRIP_H
#define ERGOTYPE_PLATFORM_LED_STRIP_H

#include <stdbool.h>
#include <stdint.h>

bool platform_led_strip_init(int8_t pin);
void platform_led_strip_put(uint32_t color);

#endif
