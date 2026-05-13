#ifndef ERGOTYPE_UI_UI_H
#define ERGOTYPE_UI_UI_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

// WS2812 color packing is 0x00GGRRBB (GRB order).
#define WS2812_OFF   0x00000000u
#define WS2812_RED   0x00000200u
#define WS2812_GREEN 0x00020000u
#define WS2812_BLUE  0x00000002u

// 32-step ring, advanced every UI_STEP_MS in ui/ui.c. Bit 0 is output first.
// With UI_STEP_MS=100ms, these approximate the old 250ms-step patterns.
#define LED_PATTERN_FAST   0xCCCCCCCCu // 200ms toggle (2 on, 2 off)
#define LED_PATTERN_MEDIUM 0xFF00FF00u // 800ms toggle (8 on, 8 off)
#define LED_PATTERN_SLOW   0xFFFF0000u // 1600ms toggle (16 on, 16 off)

// Owned by ui/ui.c, assigned by main.c right after xTaskCreate*().
extern TaskHandle_t ui_handle;

void ui_task(void *pvParameters);

void ui_led_set_pattern(uint8_t led_idx, uint32_t pattern, bool loop);
void ui_ws2812_set(uint32_t color, uint32_t pattern, bool loop);

void ui_notify_cdc_drop(size_t bytes);
void ui_notify_warn(void);
void ui_notify_err(void);
void ui_notify_cdc_connected(void);
void ui_notify_cdc_disconnected(void);

#endif
