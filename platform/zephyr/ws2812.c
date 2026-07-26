#include "led/ws2812.h"

#include <hal/nrf_gpio.h>
#include <nrfx_pwm.h>

#define WS2812_BITS 24u
#define WS2812_RESET_SLOTS 48u
#define WS2812_PWM_SLOTS (WS2812_BITS + WS2812_RESET_SLOTS)
#define WS2812_TOP 20u
#define WS2812_T0H 6u
#define WS2812_T1H 13u

static nrfx_pwm_t ws_pwm = NRFX_PWM_INSTANCE(NRF_PWM0);
static nrf_pwm_values_common_t ws_values[WS2812_PWM_SLOTS];
static bool ws_ready;

static uint32_t nrf_pin(int pin)
{
    if (pin < 32)
        return (uint32_t)pin;

    return NRF_GPIO_PIN_MAP(1, (uint32_t)(pin - 32));
}

bool ws2812_hw_init(int8_t pin)
{
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG(
        nrf_pin(pin),
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED
    );
    config.base_clock = NRF_PWM_CLK_16MHz;
    config.top_value = WS2812_TOP;
    config.load_mode = NRF_PWM_LOAD_COMMON;
    config.count_mode = NRF_PWM_MODE_UP;
    config.step_mode = NRF_PWM_STEP_AUTO;

    if (ws_ready)
        nrfx_pwm_uninit(&ws_pwm);

    ws_ready = nrfx_pwm_init(&ws_pwm, &config, NULL, NULL) == 0;
    return ws_ready;
}

void ws2812_hw_put(uint32_t color)
{
    if (!ws_ready)
        return;

    for (uint8_t i = 0; i < WS2812_BITS; ++i) {
        uint8_t bit = (uint8_t)(WS2812_BITS - 1u - i);
        ws_values[i] = (color & (1u << bit)) ? WS2812_T1H : WS2812_T0H;
    }
    for (uint8_t i = WS2812_BITS; i < WS2812_PWM_SLOTS; ++i)
        ws_values[i] = 0;

    nrf_pwm_sequence_t sequence = {
        .values.p_common = ws_values,
        .length = WS2812_PWM_SLOTS,
        .repeats = 0,
        .end_delay = 0,
    };

    nrfx_pwm_stop(&ws_pwm, true);
    (void)nrfx_pwm_simple_playback(&ws_pwm, &sequence, 1, NRFX_PWM_FLAG_STOP);
}
