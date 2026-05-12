#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "ws2812.pio.h"

#include "led/ws2812.h"

#define FREQ 800000u

#ifndef WS2812_USE_WHITE_CHANNEL
#define WS2812_USE_WHITE_CHANNEL 0
#endif

#if WS2812_USE_WHITE_CHANNEL
#define IS_RGBW true
#else
#define IS_RGBW false
#endif

static bool ws_ready;
static PIO ws_pio;
static uint ws_sm;

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

bool ws2812_hw_init(int8_t pin)
{
    ws_pio = pio0;
    ws_sm = 0;
    ws_ready = false;

    const uint pin_value = (uint)(uint8_t)pin;
    if (pio_can_add_program(ws_pio, &ws2812_program)) {
        uint off = pio_add_program(ws_pio, &ws2812_program);
        init_sm(ws_pio, ws_sm, off, pin_value, (float)FREQ, IS_RGBW);
        ws_ready = true;
    }

    return ws_ready;
}

void ws2812_hw_put(uint32_t color)
{
    if (!ws_ready)
        return;
    put(ws_pio, ws_sm, color);
}
