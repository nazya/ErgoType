#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "jconfig.h"
#include "usb_descriptors.h"

#include "led/led.h"
#include "led/ws2812.h"
#include "display/ssd1306.h"

extern uint8_t mode; // owned by main.c

#define UI_STEP_MS 100u

enum {
    UI_EVT_LED0     = 1u << 0,
    UI_EVT_LED1     = 1u << 1,
    UI_EVT_WS2812   = 1u << 2,
    UI_EVT_WARN     = 1u << 3,
    UI_EVT_ERR      = 1u << 4,
    UI_EVT_CDC_DROP = 1u << 5,
    UI_EVT_TICK     = 1u << 6,
};

TaskHandle_t ui_handle;

static volatile uint32_t ui_led_pattern[MAX_LED];
static volatile bool ui_led_loop[MAX_LED];

static volatile uint32_t ui_ws2812_pattern;
static volatile bool ui_ws2812_loop;
static volatile uint32_t ui_ws2812_color;

static volatile uint32_t ui_warn_count;
static volatile uint32_t ui_err_count;
static volatile uint32_t ui_cdc_drop_writes;
static volatile uint32_t ui_cdc_drop_bytes;

static inline uint32_t rot_r32(uint32_t v)
{
    return (v >> 1) | (v << 31);
}

static void ui_tick_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    xTaskNotify(ui_handle, UI_EVT_TICK, eSetBits);
}

void ui_led_set_pattern(uint8_t led_idx, uint32_t pattern, bool loop)
{
    ui_led_pattern[led_idx] = pattern;
    ui_led_loop[led_idx] = loop;
    xTaskNotify(ui_handle, led_idx ? UI_EVT_LED1 : UI_EVT_LED0, eSetBits);
}

void ui_ws2812_set(uint32_t color, uint32_t pattern, bool loop)
{
    ui_ws2812_color = color;
    ui_ws2812_pattern = pattern;
    ui_ws2812_loop = loop;
    xTaskNotify(ui_handle, UI_EVT_WS2812, eSetBits);
}

void ui_notify_warn(void)
{
    ui_warn_count++;
    xTaskNotify(ui_handle, UI_EVT_WARN, eSetBits);
}

void ui_notify_err(void)
{
    ui_err_count++;
    xTaskNotify(ui_handle, UI_EVT_ERR, eSetBits);
}

void ui_notify_cdc_drop(size_t bytes)
{
    ui_cdc_drop_writes++;
    ui_cdc_drop_bytes += (uint32_t)bytes;
    ui_ws2812_color = WS2812_RED;
    ui_ws2812_pattern = 0x01u;
    ui_ws2812_loop = false;
    xTaskNotify(ui_handle, UI_EVT_WS2812 | UI_EVT_CDC_DROP, eSetBits);
}

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_STATUS = 1,
} ui_screen_t;

typedef struct {
    ssd1306_t disp;
    ui_screen_t screen;
    uint8_t spinner_y;
} ui_state_t;

static const char *ui_mode_str(void)
{
    return mode == MSC ? "MSC" : "HID";
}

static void ui_spinner_draw(ui_state_t *ui)
{
    ssd1306_t *d = &ui->disp;
    // Use the last 2 pixel columns (normally unused by the 6px text grid) as an activity spinner.
    const uint8_t x0 = (uint8_t)(d->width - 2u);
    const uint8_t x1 = (uint8_t)(d->width - 1u);

    for (uint8_t page = 0; page < d->pages; ++page) {
        const size_t off = (size_t)page * (size_t)d->width + (size_t)x0;
        d->fb[off] = 0u;
        d->fb[off + 1] = 0u;
    }

    for (uint8_t t = 0; t < 3u; ++t) {
        uint8_t y = (uint8_t)(ui->spinner_y + t);
        if (y >= d->height)
            y = (uint8_t)(y - (uint8_t)d->height);
        const uint8_t page = (uint8_t)(y >> 3);
        const uint8_t bit = (uint8_t)(1u << (y & 7u));
        const size_t off = (size_t)page * (size_t)d->width + (size_t)x0;
        d->fb[off] |= bit;
        d->fb[off + 1] |= bit;
    }

    ssd1306_flush_cols(d, x0, x1);
}

static void ui_render_boot(ui_state_t *ui)
{
    const char *line0 = "ErgoType";
    const char *line1 = "firmware";
    uint8_t col0 = (uint8_t)((ui->disp.cols - (uint8_t)strlen(line0)) / 2u);
    uint8_t col1 = (uint8_t)((ui->disp.cols - (uint8_t)strlen(line1)) / 2u);

    ssd1306_clear(&ui->disp);
    ssd1306_puts(&ui->disp, 2, col0, line0);
    ssd1306_puts(&ui->disp, 4, col1, line1);
    ssd1306_flush(&ui->disp);
}

static void ui_render_status(ui_state_t *ui,
                             uint32_t err_count,
                             uint32_t warn_count,
                             uint32_t drop_writes,
                             uint32_t drop_bytes)
{
    char line0[22];
    char line1[22];
    char line2[22];
    char line3[22];
    char line4[22];

    (void)snprintf(line0, sizeof(line0), "ErgoType %s", ui_mode_str());
    (void)snprintf(line1, sizeof(line1), "err=%lu warn=%lu",
                   (unsigned long)err_count, (unsigned long)warn_count);
    (void)snprintf(line2, sizeof(line2), "CDC drops:");
    (void)snprintf(line3, sizeof(line3), "lines=%lu", (unsigned long)drop_writes);
    (void)snprintf(line4, sizeof(line4), "bytes=%lu", (unsigned long)drop_bytes);

    ssd1306_clear(&ui->disp);
    ssd1306_puts(&ui->disp, 0, 0, line0);
    ssd1306_puts(&ui->disp, 1, 0, line1);
    ssd1306_puts(&ui->disp, 2, 0, line2);
    ssd1306_puts(&ui->disp, 3, 0, line3);
    ssd1306_puts(&ui->disp, 4, 0, line4);
    ssd1306_flush(&ui->disp);
}

void ui_task(void *pvParameters)
{
    const config_t *config = (const config_t *)pvParameters;

    ui_state_t ui = {0};

    for (uint8_t i = 0; i < config->nr_leds; ++i)
        gpio_led_hw_init(config->led_pins[i]);

    if (config->ws2812_pin != -1)
        ws2812_hw_init(config->ws2812_pin);

    if (config->ssd1306.i2c_idx != -1) {
        static i2c_inst_t *const i2c_by_idx[MAX_I2C] = { i2c0, i2c1 };
        const uint8_t bus = (uint8_t)config->ssd1306.i2c_idx;
        i2c_inst_t *i2c = i2c_by_idx[bus];
        ssd1306_init(&ui.disp,
                     i2c,
                     (uint8_t)config->ssd1306.addr,
                     config->ssd1306.width,
                     config->ssd1306.height,
                     config->i2c[bus].baud,
                     config->ssd1306.contrast,
                     config->ssd1306.precharge,
                     config->ssd1306.vcomh,
                     config->i2c[bus].sda,
                     config->i2c[bus].scl);
        ui.screen = UI_SCREEN_BOOT;
        ui_render_boot(&ui);
        vTaskDelay(pdMS_TO_TICKS(300u));
        ui.screen = UI_SCREEN_STATUS;
        ui_render_status(&ui,
                         ui_err_count,
                         ui_warn_count,
                         ui_cdc_drop_writes,
                         ui_cdc_drop_bytes);
        ui_spinner_draw(&ui);
    }

    uint32_t led_ring[MAX_LED] = {0};
    bool led_loop[MAX_LED] = {0};
    for (uint8_t i = 0; i < config->nr_leds; ++i) {
        led_ring[i] = ui_led_pattern[i];
        led_loop[i] = ui_led_loop[i];
    }

    uint32_t ws_ring = ui_ws2812_pattern;
    bool ws_loop = ui_ws2812_loop;
    uint32_t ws_color = ui_ws2812_color;

    const TickType_t step_ticks = pdMS_TO_TICKS(UI_STEP_MS);
    TimerHandle_t tick_timer = xTimerCreate("ui", step_ticks, pdTRUE, NULL, ui_tick_timer_cb);
    configASSERT(tick_timer);
    (void)xTimerStart(tick_timer, 0);

    for (;;) {
        uint32_t events = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &events, portMAX_DELAY);

        if (events & UI_EVT_LED0) {
            led_ring[0] = ui_led_pattern[0];
            led_loop[0] = ui_led_loop[0];
        }
        if (events & UI_EVT_LED1) {
            led_ring[1] = ui_led_pattern[1];
            led_loop[1] = ui_led_loop[1];
        }
        if (events & UI_EVT_WS2812) {
            ws_ring = ui_ws2812_pattern;
            ws_loop = ui_ws2812_loop;
            ws_color = ui_ws2812_color;
        }

        if (config->ssd1306.i2c_idx != -1 && ui.screen == UI_SCREEN_STATUS) {
            if (events & (UI_EVT_WARN | UI_EVT_ERR | UI_EVT_CDC_DROP)) {
                ui_render_status(&ui,
                                 ui_err_count,
                                 ui_warn_count,
                                 ui_cdc_drop_writes,
                                 ui_cdc_drop_bytes);
                ui_spinner_draw(&ui);
            }
        }

        if (!(events & UI_EVT_TICK))
            continue;

        for (uint8_t i = 0; i < config->nr_leds; ++i) {
            gpio_led_hw_put(config->led_pins[i], led_ring[i] & 0x01u);
            led_ring[i] = led_loop[i] ? rot_r32(led_ring[i]) : (led_ring[i] >> 1);
        }

        if (config->ws2812_pin != -1) {
            ws2812_hw_put((ws_ring & 0x01u) ? ws_color : WS2812_OFF);
            ws_ring = ws_loop ? rot_r32(ws_ring) : (ws_ring >> 1);
        }

        if (config->ssd1306.i2c_idx != -1 && ui.screen == UI_SCREEN_STATUS) {
            ui_spinner_draw(&ui);
            ui.spinner_y++;
            if (ui.spinner_y >= ui.disp.height)
                ui.spinner_y = 0;
        }

    }
}
