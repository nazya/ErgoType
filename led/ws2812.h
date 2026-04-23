#ifndef LED_WS2812_H
#define LED_WS2812_H

#include <stdint.h>

// Color packing is 0x00GGRRBB (GRB order).
#define WS2812_OFF  0x00000000u
#define WS2812_RED  0x00000200u
#define WS2812_GREEN 0x00020000u
#define WS2812_BLUE 0x00000002u

void ws2812_set(uint32_t color, uint32_t pattern);

void ws2812_task(void* pvParameters);

#endif
