#ifndef LED_PLAIN_H
#define LED_PLAIN_H

#include <stdint.h>

// 32-step ring, advanced every STEP_MS in led/plain.c. Bit 0 is output first.
#define LED_PATTERN_FAST   0xAAAAAAAAu // 250ms toggle (STEP_MS=250)
#define LED_PATTERN_MEDIUM 0x0F0F0F0Fu // 1000ms toggle (4 on, 4 off)
// 2500ms isn't representable exactly as a clean 32-bit ring at STEP_MS=250.
#define LED_PATTERN_SLOW   0x00FF00FFu // ~2000ms toggle (8 on, 8 off)

void gpio_led_set_pattern(uint32_t p);

void gpio_led_task(void* pvParameters);

#endif
