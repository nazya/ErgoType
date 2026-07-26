#ifndef LED_WS2812_H
#define LED_WS2812_H

#include <stdbool.h>
#include <stdint.h>

bool ws2812_hw_init(int8_t pin);
void ws2812_hw_put(uint32_t color);

#endif
