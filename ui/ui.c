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

/*
 * Status row 0 layout (6x8 text unless noted otherwise):
 *
 * chars:  [0..3] title window ("Ergo" view)
 *         [5..7] mode
 *         [9..11] CDC
 *
 * pixels: [0..23]   title window (4 * 6)
 *         [30..47]  mode (3 * 6)
 *         [54..71]  CDC (3 * 6)
 *         [72..73]  CDC warn '!'
 *         [74..127] free tail after CDC warn
 *
 * title window:
 *   start x = 0
 *   width   = UI_TITLE_WIN_PX
 *   chars   = UI_TITLE_WIN_COLS
 *
 * mode:
 *   start col = UI_STATUS_MODE_COL
 *   len chars = UI_STATUS_MODE_LEN
 *   start x   = UI_STATUS_MODE_COL * 6
 *   end x     = UI_STATUS_MODE_COL * 6 + UI_STATUS_MODE_LEN * 6 - 1
 *
 * CDC:
 *   start col = UI_STATUS_CDC_COL
 *   len chars = UI_STATUS_CDC_LEN
 *   start x   = UI_STATUS_CDC_COL * 6
 *   end x     = UI_STATUS_CDC_COL * 6 + UI_STATUS_CDC_LEN * 6 - 1
 *
 * CDC warn '!':
 *   start x = UI_STATUS_CDC_WARN_X
 *   width   = UI_STATUS_CDC_WARN_PX
 *
 * free tail after CDC warn:
 *   start x   = UI_STATUS_AFTER_CDC_WARN_X
 *   width px  = UI_STATUS_AFTER_CDC_WARN_PX
 *   len 6x8   = UI_STATUS_AFTER_CDC_WARN_LEN6X8
 */
#define UI_STEP_MS 100u
#define UI_TITLE_LEN (sizeof(ui_title) - 1u)
#define UI_TITLE_WIDTH_PX (UI_TITLE_LEN * 6u)
#define UI_TITLE_WIN_COLS 4u
#define UI_TITLE_WIN_PX (UI_TITLE_WIN_COLS * 6u)
#define UI_STATUS_MODE_LEN 3u
#define UI_STATUS_MODE_COL (UI_TITLE_WIN_COLS + 1u)
#define UI_STATUS_CDC_LEN 3u
#define UI_STATUS_CDC_COL (UI_STATUS_MODE_COL + UI_STATUS_MODE_LEN + 1u)
#define UI_STATUS_CDC_WARN_PX 2u
#define UI_STATUS_CDC_WARN_X ((UI_STATUS_CDC_COL + UI_STATUS_CDC_LEN) * 6u)
#define UI_STATUS_AFTER_CDC_WARN_X (UI_STATUS_CDC_WARN_X + UI_STATUS_CDC_WARN_PX)
#define UI_STATUS_AFTER_CDC_WARN_PX (128u - UI_STATUS_AFTER_CDC_WARN_X)
#define UI_STATUS_AFTER_CDC_WARN_LEN6X8 (UI_STATUS_AFTER_CDC_WARN_PX / 6u)
#define UI_KEYLOG_ROW0_WITH_STATUS 2u
#define UI_KEYLOG_ROWS_WITH_STATUS 6u
#define UI_KEYLOG_ROW0_NO_STATUS 1u
#define UI_KEYLOG_ROWS_NO_STATUS 7u
#define UI_KEYLOG_MAX_ROWS UI_KEYLOG_ROWS_NO_STATUS

enum {
    UI_EVT_LED0            = 1u << 0,
    UI_EVT_LED1            = 1u << 1,
    UI_EVT_WS2812          = 1u << 2,
    UI_EVT_SCREEN          = 1u << 3,
    UI_EVT_STATUS          = 1u << 4,
    UI_EVT_TICK            = 1u << 5,
    UI_EVT_KEYLOG          = 1u << 6,
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
static volatile bool ui_cdc_connected;
static char ui_keylog[UI_KEYLOG_MAX_ROWS][UI_KEYLOG_LINE_LEN + 1u];
static volatile uint8_t ui_keylog_head;
static volatile uint8_t ui_keylog_count;

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

void ui_notify_cdc_connected(void)
{
    ui_cdc_connected = true;
}

void ui_notify_cdc_disconnected(void)
{
    ui_cdc_connected = false;
}

void ui_notify_keyd(const char *s)
{
    char line[UI_KEYLOG_LINE_LEN + 1u] = {0};

    (void)snprintf(line, sizeof(line), "%s", s);

    memcpy(ui_keylog[ui_keylog_head], line, sizeof(line));
    ui_keylog_head++;
    if (ui_keylog_head == UI_KEYLOG_MAX_ROWS)
        ui_keylog_head = 0;
    if (ui_keylog_count != UI_KEYLOG_MAX_ROWS)
        ui_keylog_count++;

    xTaskNotify(ui_handle, UI_EVT_KEYLOG, eSetBits);
}

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_STATUS = 1,
} ui_screen_t;

typedef struct {
    const ssd1306_cfg_t *disp;
    ui_screen_t screen;
    bool cdc_connected;
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
    if (ui->err_count || ui->warn_count || ui_keylog_count == 0) {
        (void)snprintf(line, sizeof(line), "err=%lu warn=%lu",
                       (unsigned long)ui->err_count, (unsigned long)ui->warn_count);
        ssd1306_putn4x8(ui->disp, 1, 0, line, strlen(line));
    }
}

static void ui_draw_keylog(ui_state_t *ui)
{
    char lines[UI_KEYLOG_MAX_ROWS][UI_KEYLOG_LINE_LEN + 1u] = {0};
    const uint8_t row0 = (ui->err_count || ui->warn_count || ui_keylog_count == 0) ? UI_KEYLOG_ROW0_WITH_STATUS
                                                                                     : UI_KEYLOG_ROW0_NO_STATUS;
    const uint8_t rows = (ui->err_count || ui->warn_count || ui_keylog_count == 0) ? UI_KEYLOG_ROWS_WITH_STATUS
                                                                                     : UI_KEYLOG_ROWS_NO_STATUS;
    uint8_t head = 0;
    uint8_t count = 0;

    head = ui_keylog_head;
    count = ui_keylog_count;
    if (count > rows) {
        count = rows;
        ui_keylog_count = rows;
    }
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t idx = (uint8_t)(head + UI_KEYLOG_MAX_ROWS - count + i);
        if (idx >= UI_KEYLOG_MAX_ROWS)
            idx -= UI_KEYLOG_MAX_ROWS;
        memcpy(lines[i], ui_keylog[idx], sizeof(lines[i]));
    }

    for (uint8_t i = 0; i < rows; ++i) {
        ssd1306_clear_page(ui->disp, row0 + i);
        ssd1306_putn4x8(ui->disp, row0 + i, 0, lines[i], strlen(lines[i]));
    }
}

static void ui_update_status_screen(ui_state_t *ui)
{
    const uint8_t warn_span[UI_STATUS_CDC_WARN_PX] = {0x5Fu, 0x00u};
    const uint8_t clear_span[UI_STATUS_CDC_WARN_PX] = {0x00u, 0x00u};

    if (ui->cdc_connected != ui_cdc_connected) {
        ui->cdc_connected = ui_cdc_connected;
        const char *s = ui->cdc_connected ? "CDC" : "   ";
        ssd1306_putn6x8(ui->disp, 0, UI_STATUS_CDC_COL, s, UI_STATUS_CDC_LEN);
        ssd1306_write_page_span(ui->disp, 0, UI_STATUS_CDC_WARN_X,
                                (ui->cdc_connected && ui_cdc_drop_writes) ? warn_span : clear_span,
                                UI_STATUS_CDC_WARN_PX);
    }

    if (ui->err_count != ui_err_count ||
        ui->warn_count != ui_warn_count ||
        ui->cdc_drop_writes != ui_cdc_drop_writes ||
        ui->cdc_drop_bytes != ui_cdc_drop_bytes) {
        ui_read_stats(ui);
        ui_draw_status_counts(ui);
        ui_draw_keylog(ui);
        ssd1306_write_page_span(ui->disp, 0, UI_STATUS_CDC_WARN_X,
                                (ui->cdc_connected && ui->cdc_drop_writes) ? warn_span : clear_span,
                                UI_STATUS_CDC_WARN_PX);
    }
}

static void ui_render_status_screen(ui_state_t *ui)
{
    ui_read_stats(ui);

    ssd1306_clear(ui->disp);
    ssd1306_putn6x8(ui->disp, 0, UI_STATUS_MODE_COL, mode == MSC ? "MSC" : "HID", UI_STATUS_MODE_LEN);
    ui->cdc_connected = ui_cdc_connected;
    ssd1306_putn6x8(ui->disp, 0, UI_STATUS_CDC_COL, ui->cdc_connected ? "CDC" : "   ", UI_STATUS_CDC_LEN);
    const uint8_t warn_span[UI_STATUS_CDC_WARN_PX] = {0x5Fu, 0x00u};
    const uint8_t clear_span[UI_STATUS_CDC_WARN_PX] = {0x00u, 0x00u};
    ssd1306_write_page_span(ui->disp, 0, UI_STATUS_CDC_WARN_X,
                            (ui->cdc_connected && ui->cdc_drop_writes) ? warn_span : clear_span,
                            UI_STATUS_CDC_WARN_PX);

    for (size_t i = 0; i < UI_TITLE_LEN; ++i)
        memcpy(&ui->title_span[i * 6u], px6x8_glyphs[(uint8_t)ui_title[i]], 6u);
    for (uint8_t x = 0; x < UI_TITLE_WIN_PX; x += 4u)
        ssd1306_write_page_span(ui->disp, 0, x, &ui->title_span[x], 4u);

    ui_draw_status_counts(ui);
    ui_draw_keylog(ui);
}

static void ui_spin_status_title(ui_state_t *ui)
{
    const uint8_t first = ui->title_span[0];
    memmove(ui->title_span, ui->title_span + 1u, UI_TITLE_WIDTH_PX - 1u);
    ui->title_span[UI_TITLE_WIDTH_PX - 1u] = first;
    for (uint8_t x = 0; x < UI_TITLE_WIN_PX; x += 4u)
        ssd1306_write_page_span(ui->disp, 0, x, &ui->title_span[x], 4u);
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
            if ((events & UI_EVT_KEYLOG) && ui.screen == UI_SCREEN_STATUS)
                ui_draw_keylog(&ui);
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
