#include "ui/ui.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "jconfig.h"
#include "usb_descriptors.h"

#include "led/led.h"
#include "led/ws2812.h"
#include "display/ssd1306.h"

extern uint8_t mode; // owned by main.c

#define UI_STEP_MS 100u

enum {
    UI_EVT_PLAIN    = 1u << 0,
    UI_EVT_WS2812   = 1u << 1,
    UI_EVT_WARN     = 1u << 3,
    UI_EVT_ERR      = 1u << 4,
    UI_EVT_CDC_DROP = 1u << 5,
};

TaskHandle_t ui_handle;

static volatile uint32_t ui_plain_pattern;
static volatile bool ui_plain_loop;

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

void ui_led_set_pattern(uint32_t pattern, bool loop)
{
    ui_plain_pattern = pattern;
    ui_plain_loop = loop;
    xTaskNotify(ui_handle, UI_EVT_PLAIN, eSetBits);
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
    uint8_t boot_ticks_left;
} ui_state_t;

static const char *ui_mode_str(void)
{
    return mode == MSC ? "MSC" : "HID";
}

static void ui_render_boot(ui_state_t *ui)
{
    ssd1306_clear(&ui->disp);
    ssd1306_puts(&ui->disp, 3, 2, "ERGOTYPE");
    ssd1306_puts(&ui->disp, 6, 4, "BOOT");
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

    (void)snprintf(line0, sizeof(line0), "ERGOTYPE %s", ui_mode_str());
    (void)snprintf(line1, sizeof(line1), "E=%lu W=%lu",
                   (unsigned long)err_count, (unsigned long)warn_count);
    (void)snprintf(line2, sizeof(line2), "CDC W=%lu B=%lu",
                   (unsigned long)drop_writes, (unsigned long)drop_bytes);

    ssd1306_clear(&ui->disp);
    ssd1306_puts(&ui->disp, 0, 0, line0);
    ssd1306_puts(&ui->disp, 0, 1, line1);
    ssd1306_puts(&ui->disp, 0, 2, line2);
    ssd1306_flush(&ui->disp);
}

void ui_task(void *pvParameters)
{
    const config_t *config = (const config_t *)pvParameters;

    ui_state_t ui = {0};

    if (config->led_pin != -1)
        gpio_led_hw_init(config->led_pin);

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
        ui.boot_ticks_left = (uint8_t)(500u / UI_STEP_MS);
        ui_render_boot(&ui);
    }

    uint32_t plain_ring = ui_plain_pattern;
    bool plain_loop = ui_plain_loop;

    uint32_t ws_ring = ui_ws2812_pattern;
    bool ws_loop = ui_ws2812_loop;
    uint32_t ws_color = ui_ws2812_color;

    for (;;) {
        uint32_t events = 0;
        BaseType_t got = xTaskNotifyWait(0, UINT32_MAX, &events, pdMS_TO_TICKS(UI_STEP_MS));
        bool tick = (got == pdFAIL);

        if (events & UI_EVT_PLAIN) {
            plain_ring = ui_plain_pattern;
            plain_loop = ui_plain_loop;
        }
        if (events & UI_EVT_WS2812) {
            ws_ring = ui_ws2812_pattern;
            ws_loop = ui_ws2812_loop;
            ws_color = ui_ws2812_color;
        }

        if (tick) {
            if (config->led_pin != -1) {
                gpio_led_hw_put(config->led_pin, (plain_ring & 0x01u) != 0);
                plain_ring = plain_loop ? rot_r32(plain_ring) : (plain_ring >> 1);
            }

            if (config->ws2812_pin != -1) {
                ws2812_hw_put((ws_ring & 0x01u) ? ws_color : WS2812_OFF);
                ws_ring = ws_loop ? rot_r32(ws_ring) : (ws_ring >> 1);
            }

            if (config->ssd1306.i2c_idx != -1 && ui.screen == UI_SCREEN_BOOT) {
                if (ui.boot_ticks_left)
                    ui.boot_ticks_left--;
                if (!ui.boot_ticks_left) {
                    ui.screen = UI_SCREEN_STATUS;
                    ui_render_status(&ui,
                                     ui_err_count,
                                     ui_warn_count,
                                     ui_cdc_drop_writes,
                                     ui_cdc_drop_bytes);
                }
            }
            continue;
        }

        if (config->ssd1306.i2c_idx != -1 && ui.screen == UI_SCREEN_STATUS) {
            if (events & (UI_EVT_WARN | UI_EVT_ERR | UI_EVT_CDC_DROP))
                ui_render_status(&ui,
                                 ui_err_count,
                                 ui_warn_count,
                                 ui_cdc_drop_writes,
                                 ui_cdc_drop_bytes);
        }
    }
}
