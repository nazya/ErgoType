#ifndef LED_H
#define LED_H

#include <stdbool.h>
#include <stdint.h>

void gpio_led_hw_init(int8_t pin);
void gpio_led_hw_put(int8_t pin, bool on);

#endif
