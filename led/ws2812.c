#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "FreeRTOS.h"
#include "task.h"

#include "ws2812.pio.h"

#include "led/ws2812.h"

#define FREQ 800000
#define STEP_MS 250

#ifndef WS2812_USE_WHITE_CHANNEL
#define WS2812_USE_WHITE_CHANNEL 0
#endif

#if WS2812_USE_WHITE_CHANNEL
#define IS_RGBW true
#else
#define IS_RGBW false
#endif

// 32-step ring, advanced every STEP_MS. Bit 0 is output first.
static volatile uint32_t pattern = 0;
static volatile uint32_t color = WS2812_OFF;

void ws2812_set(uint32_t c, uint32_t p) {
    // Keep this minimal: just publish new values for the task loop.
    color = c;
    pattern = p;
}

static inline void put(PIO pio, uint sm, uint32_t grb) {
    if (IS_RGBW) {
        pio_sm_put_blocking(pio, sm, grb);
    } else {
        pio_sm_put_blocking(pio, sm, grb << 8u);
    }
}

static inline void init_sm(PIO pio, uint sm, uint off, uint pin, float freq, bool is_rgbw) {
    pio_sm_config c = ws2812_program_get_default_config(off);
    sm_config_set_sideset_pins(&c, pin);
    sm_config_set_out_shift(&c, false, true, is_rgbw ? 32 : 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    float div = (float)clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);

    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    pio_sm_init(pio, sm, off, &c);
    pio_sm_set_enabled(pio, sm, true);
}

void ws2812_task(void* pvParameters) {
    const int8_t pin = *(const int8_t*)pvParameters;
    const uint pin_value = (uint)(uint8_t)pin;

    const PIO pio = pio0;
    const uint sm = 0;
    bool ready = false;

    if (pio_can_add_program(pio, &ws2812_program)) {
        uint off = pio_add_program(pio, &ws2812_program);
        init_sm(pio, sm, off, pin_value, FREQ, IS_RGBW);
        ready = true;
    }

    uint32_t last_pattern = pattern;
    uint32_t ring = last_pattern;
    uint32_t marker = 1u;

    while (1) {
        uint32_t p = pattern;
        uint32_t c = color;

        // Restart the ring on pattern change so updates take effect cleanly.
        if (p != last_pattern) {
            last_pattern = p;
            ring = p;
            marker = 1u;
        }

        if (ready) {
            put(pio, sm, (ring & 0x01u) ? c : WS2812_OFF);
        }
        ring = (ring >> 1) | (ring << 31);
        marker = (marker >> 1) | (marker << 31);

        // One-shot: for any pattern that's not all-off or all-on, stop after 32 steps.
        if (marker == 1u && p != 0u && p != 0xFFFFFFFFu && pattern == p) {
            pattern = 0;
            last_pattern = 0;
            ring = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(STEP_MS));
    }
}
