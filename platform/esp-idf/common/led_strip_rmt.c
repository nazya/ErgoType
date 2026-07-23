#include "platform/led_strip.h"

#include "esp_err.h"
#include "led_strip.h"

#if defined(ERGOTYPE_LED_STRIP_RGB) && ERGOTYPE_LED_STRIP_RGB
#define ERGOTYPE_LED_STRIP_COLOR_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_RGB
#else
#define ERGOTYPE_LED_STRIP_COLOR_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_GRB
#endif

static led_strip_handle_t ws_strip;

bool platform_led_strip_init(int8_t pin)
{
    const led_strip_config_t strip_config = {
        .strip_gpio_num = pin,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = ERGOTYPE_LED_STRIP_COLOR_FORMAT,
    };
    const led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10000000,
        .mem_block_symbols = 64,
    };

    if (led_strip_new_rmt_device(&strip_config, &rmt_config, &ws_strip) != ESP_OK)
        return false;

    return led_strip_clear(ws_strip) == ESP_OK;
}

void platform_led_strip_put(uint32_t color)
{
    if (!ws_strip)
        return;

    uint8_t green = (uint8_t)(color >> 16);
    uint8_t red = (uint8_t)(color >> 8);
    uint8_t blue = (uint8_t)color;

    if (led_strip_set_pixel(ws_strip, 0, red, green, blue) == ESP_OK)
        (void)led_strip_refresh(ws_strip);
}
