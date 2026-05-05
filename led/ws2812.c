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
static volatile bool pattern_loop = true;
static TaskHandle_t ws2812_task_handle;

static inline uint32_t rot_r32(uint32_t v)
{
    return (v >> 1) | (v << 31);
}

void ws2812_set(uint32_t c, uint32_t p, bool loop) {
    color = c;
    pattern = p;
    pattern_loop = loop;

    // Wake the WS2812 task so it applies the new pattern immediately (even if unchanged).
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return;
    TaskHandle_t h = ws2812_task_handle;
    if (h)
        xTaskNotifyGive(h);
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

    ws2812_task_handle = xTaskGetCurrentTaskHandle();

    uint32_t cur_pattern = pattern;
    uint32_t cur_color = color;
    bool loop = pattern_loop;

    uint32_t ring = cur_pattern;

    for (;;) {
        if (ready)
            put(pio, sm, (ring & 0x01u) ? cur_color : WS2812_OFF);

        ring = loop ? rot_r32(ring) : (ring >> 1);

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(STEP_MS))) {
            cur_pattern = pattern;
            cur_color = color;
            loop = pattern_loop;
            ring = cur_pattern;
        }
    }
}
