#include "ui/ui.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "jconfig.h"
#include "usb_descriptors.h"

#include "led/led.h"
#include "led/ws2812.h"
#include "display/px6x8_font.h"
#include "display/ssd1306.h"

extern uint8_t mode; // owned by main.c

#define UI_STEP_MS 100u
#define UI_TITLE_LEN (sizeof(ui_title) - 1u)
#define UI_TITLE_WIDTH_PX (UI_TITLE_LEN * 6u)
#define UI_STATUS_MODE_LEN 3u
#define UI_STATUS_MODE_COL (UI_TITLE_LEN + 1u)

enum {
    UI_EVT_LED0            = 1u << 0,
    UI_EVT_LED1            = 1u << 1,
    UI_EVT_WS2812          = 1u << 2,
    UI_EVT_SCREEN          = 1u << 3,
    UI_EVT_STATUS          = 1u << 4,
    UI_EVT_TICK            = 1u << 5,
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

static const char ui_title[] = "ErgoType";

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
}

void ui_notify_err(void)
{
    ui_err_count++;
}

void ui_notify_cdc_drop(size_t bytes)
{
    ui_cdc_drop_writes++;
    ui_cdc_drop_bytes += (uint32_t)bytes;
}

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_STATUS = 1,
} ui_screen_t;

typedef struct {
    const ssd1306_cfg_t *disp;
    ui_screen_t screen;
    uint32_t err_count;
    uint32_t warn_count;
    uint32_t cdc_drop_writes;
    uint32_t cdc_drop_bytes;
    uint8_t title_span[UI_TITLE_WIDTH_PX];
} ui_state_t;

static void ui_render_boot(ui_state_t *ui)
{
    const char *line0 = "ErgoType";
    const char *line1 = "firmware";
    const uint8_t cols4x8 = ui->disp->width / 4u;
    const uint8_t cols6x8 = ui->disp->width / 6u;
    uint8_t col0 = (cols6x8 - strlen(line0)) / 2u;
    uint8_t col1 = (cols4x8 - strlen(line1)) / 2u;

    ssd1306_clear(ui->disp);
    ssd1306_putn6x8(ui->disp, 2, col0, line0, strlen(line0));
    ssd1306_putn4x8(ui->disp, 4, col1, line1, strlen(line1));
}

static void ui_read_stats(ui_state_t *ui)
{
    ui->err_count = ui_err_count;
    ui->warn_count = ui_warn_count;
    ui->cdc_drop_writes = ui_cdc_drop_writes;
    ui->cdc_drop_bytes = ui_cdc_drop_bytes;
}

static void ui_draw_status_counts(ui_state_t *ui)
{
    char line[22];

    ssd1306_clear_page(ui->disp, 1);
    (void)snprintf(line, sizeof(line), "err=%lu warn=%lu",
                   (unsigned long)ui->err_count, (unsigned long)ui->warn_count);
    ssd1306_putn4x8(ui->disp, 1, 0, line, strlen(line));

    ssd1306_clear_page(ui->disp, 3);
    (void)snprintf(line, sizeof(line), "lines=%lu", (unsigned long)ui->cdc_drop_writes);
    ssd1306_putn4x8(ui->disp, 3, 0, line, strlen(line));

    ssd1306_clear_page(ui->disp, 4);
    (void)snprintf(line, sizeof(line), "bytes=%lu", (unsigned long)ui->cdc_drop_bytes);
    ssd1306_putn4x8(ui->disp, 4, 0, line, strlen(line));
}

static void ui_update_status_screen(ui_state_t *ui)
{
    if (ui->err_count == ui_err_count &&
        ui->warn_count == ui_warn_count &&
        ui->cdc_drop_writes == ui_cdc_drop_writes &&
        ui->cdc_drop_bytes == ui_cdc_drop_bytes)
        return;

    ui_read_stats(ui);
    ui_draw_status_counts(ui);
}

static void ui_render_status_screen(ui_state_t *ui)
{
    ui_read_stats(ui);

    ssd1306_clear(ui->disp);
    ssd1306_putn6x8(ui->disp, 0, 0, ui_title, UI_TITLE_LEN);
    ssd1306_putn6x8(ui->disp, 0, UI_STATUS_MODE_COL, mode == MSC ? "MSC" : "HID", UI_STATUS_MODE_LEN);
    ssd1306_putn6x8(ui->disp, 2, 0, "CDC drops:", sizeof("CDC drops:") - 1u);

    for (size_t i = 0; i < UI_TITLE_LEN; ++i)
        memcpy(&ui->title_span[i * 6u], px6x8_glyphs[(uint8_t)ui_title[i]], 6u);

    ui_draw_status_counts(ui);
}

static void ui_spin_status_title(ui_state_t *ui)
{
    const uint8_t first = ui->title_span[0];
    memmove(ui->title_span, ui->title_span + 1u, UI_TITLE_WIDTH_PX - 1u);
    ui->title_span[UI_TITLE_WIDTH_PX - 1u] = first;
    ssd1306_write_page_span(ui->disp, 0, 0, ui->title_span, UI_TITLE_WIDTH_PX);
}

static void ui_tick_status_screen(ui_state_t *ui)
{
    ui_update_status_screen(ui);
    ui_spin_status_title(ui);
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
        for (uint8_t bus = 0; bus < MAX_I2C; ++bus) {
            if (!(config->i2c_mask & (uint8_t)(1u << bus)))
                continue;
            i2c_inst_t *i2c = i2c_by_idx[bus];
            const i2c_cfg_t *bus_cfg = &config->i2c[bus];
            gpio_set_function((uint)bus_cfg->sda, GPIO_FUNC_I2C);
            gpio_set_function((uint)bus_cfg->scl, GPIO_FUNC_I2C);
            gpio_pull_up((uint)bus_cfg->sda);
            gpio_pull_up((uint)bus_cfg->scl);
            (void)i2c_init(i2c, bus_cfg->baud);
        }

        ui.disp = &config->ssd1306;
        ssd1306_init(ui.disp);
        ui.screen = UI_SCREEN_BOOT;
        ui_render_boot(&ui);
        vTaskDelay(pdMS_TO_TICKS(300u));
        ui.screen = UI_SCREEN_STATUS;
        ui_render_status_screen(&ui);
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
        if (config->ssd1306.i2c_idx != -1) {
            if (events & UI_EVT_SCREEN) {
                switch (ui.screen) {
                case UI_SCREEN_STATUS:
                    ui_render_status_screen(&ui);
                    break;
                }
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

        if (config->ssd1306.i2c_idx != -1 && ui.screen == UI_SCREEN_STATUS)
            ui_tick_status_screen(&ui);
    }
}
