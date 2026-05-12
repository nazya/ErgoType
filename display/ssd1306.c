#include "display/ssd1306.h"

#include <string.h>
#include <stdio.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "FreeRTOS.h"
#include "task.h"

#include "jconfig.h"

#define SSD1306_MAX_WIDTH 128u
#define SSD1306_MAX_HEIGHT 64u
#define SSD1306_FB_SIZE (SSD1306_MAX_WIDTH * (SSD1306_MAX_HEIGHT / 8u))

#define SSD1306_I2C_BAUD 400000u

#define SSD1306_CTRL_CMD_STREAM 0x00u
#define SSD1306_CTRL_DATA_STREAM 0x40u

#define SSD1306_CMD_SET_COLUMN_ADDR 0x21u
#define SSD1306_CMD_SET_PAGE_ADDR 0x22u
#define SSD1306_CMD_SET_MEM_MODE 0x20u

static TaskHandle_t ssd1306_task_handle;
static volatile uint32_t cdc_drop_writes;
static volatile uint32_t cdc_drop_bytes;

static i2c_inst_t *const i2c_by_idx[MAX_I2C] = { i2c0, i2c1 };

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    uint16_t width;
    uint16_t height;
    uint8_t pages;
    uint8_t fb[SSD1306_FB_SIZE];
} ssd1306_t;

static ssd1306_t disp;

static void ssd1306_write_cmds(const uint8_t *cmds, size_t n)
{
    uint8_t buf[1 + 32];
    buf[0] = SSD1306_CTRL_CMD_STREAM;

    while (n) {
        size_t chunk = n;
        if (chunk > 32)
            chunk = 32;
        memcpy(&buf[1], cmds, chunk);
        (void)i2c_write_blocking(disp.i2c, disp.addr, buf, chunk + 1, false);
        cmds += chunk;
        n -= chunk;
    }
}

static void ssd1306_write_data(const uint8_t *data, size_t n)
{
    uint8_t buf[1 + 32];
    buf[0] = SSD1306_CTRL_DATA_STREAM;

    while (n) {
        size_t chunk = n;
        if (chunk > 32)
            chunk = 32;
        memcpy(&buf[1], data, chunk);
        (void)i2c_write_blocking(disp.i2c, disp.addr, buf, chunk + 1, false);
        data += chunk;
        n -= chunk;
    }
}

static void ssd1306_flush(void)
{
    const uint8_t width_minus_1 = (uint8_t)(disp.width - 1u);
    const uint8_t page_end = (uint8_t)(disp.pages - 1u);

    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, 0u, width_minus_1,
        SSD1306_CMD_SET_PAGE_ADDR, 0u, page_end,
    };
    ssd1306_write_cmds(addr_cmds, sizeof(addr_cmds));

    const size_t n = (size_t)disp.width * (size_t)disp.pages;
    ssd1306_write_data(disp.fb, n);
}

static void ssd1306_clear(void)
{
    memset(disp.fb, 0, sizeof(disp.fb));
}

static const uint8_t *ssd1306_glyph5x7(char c)
{
    static const uint8_t space[5] = {0, 0, 0, 0, 0};
    static const uint8_t dash[5]  = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t eq[5]    = {0x14, 0x14, 0x14, 0x14, 0x14};

    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
        {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
        {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
        {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
        {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
        {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    };

    static const uint8_t letters[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
        {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
        {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
        {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
        {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
        {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
        {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
        {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
        {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
        {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
        {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
        {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
        {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
        {0x46, 0x49, 0x49, 0x49, 0x31}, // S
        {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
        {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
        {0x63, 0x14, 0x08, 0x14, 0x63}, // X
        {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
        {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    };

    if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);

    if (c == ' ')
        return space;
    if (c == '-')
        return dash;
    if (c == ':')
        return colon;
    if (c == '=')
        return eq;

    if (c >= '0' && c <= '9')
        return digits[(uint8_t)(c - '0')];
    if (c >= 'A' && c <= 'Z')
        return letters[(uint8_t)(c - 'A')];

    return space;
}

static void ssd1306_text(uint16_t x, uint8_t page, const char *s)
{
    size_t off = (size_t)page * (size_t)disp.width + (size_t)x;
    while (*s && (off + 6u) <= (size_t)((page + 1u) * disp.width)) {
        const uint8_t *g = ssd1306_glyph5x7(*s++);
        for (uint8_t i = 0; i < 5; ++i)
            disp.fb[off++] = g[i];
        disp.fb[off++] = 0x00;
    }
}

static void ssd1306_show_drop_stats(void)
{
    char line0[22];
    char line1[22];

    const uint32_t writes = cdc_drop_writes;
    const uint32_t bytes = cdc_drop_bytes;

    (void)snprintf(line0, sizeof(line0), "CDC DROP");
    (void)snprintf(line1, sizeof(line1), "W=%lu B=%lu",
                   (unsigned long)writes, (unsigned long)bytes);

    ssd1306_clear();
    ssd1306_text(0, 0, line0);
    ssd1306_text(0, 1, line1);
    ssd1306_flush();
}

void ssd1306_notify_cdc_drop(size_t dropped_len)
{
    cdc_drop_writes++;
    cdc_drop_bytes += (uint32_t)dropped_len;

    TaskHandle_t h = ssd1306_task_handle;
    if (h)
        xTaskNotifyGive(h);
}

void ssd1306_task(void *pvParameters)
{
    const config_t *config = (const config_t *)pvParameters;

    const uint8_t bus = (uint8_t)config->ssd1306.i2c_idx;
    const uint8_t addr = (uint8_t)config->ssd1306.addr;
    const uint16_t width = config->ssd1306.width;
    const uint16_t height = config->ssd1306.height;

    disp.i2c = i2c_by_idx[bus];
    disp.addr = addr;
    disp.width = width;
    disp.height = height;
    disp.pages = (uint8_t)(height / 8u);

    gpio_set_function((uint)config->i2c[bus].sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)config->i2c[bus].scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)config->i2c[bus].sda);
    gpio_pull_up((uint)config->i2c[bus].scl);
    (void)i2c_init(disp.i2c, SSD1306_I2C_BAUD);

    // Minimal init (128x64 or 128x32).
    const uint8_t com_pins = (height == 32u) ? 0x02u : 0x12u;
    const uint8_t contrast = (height == 32u) ? 0x8Fu : 0xCFu;
    const uint8_t mux = (uint8_t)(height - 1u);
    const uint8_t init_cmds[] = {
        0xAE,               // display off
        0xD5, 0x80,         // clock div
        0xA8, mux,          // multiplex
        0xD3, 0x00,         // display offset
        0x40,               // start line 0
        0x8D, 0x14,         // charge pump
        SSD1306_CMD_SET_MEM_MODE, 0x00, // horizontal addressing mode
        0xA1,               // segment remap
        0xC8,               // COM scan direction
        0xDA, com_pins,     // COM pins
        0x81, contrast,     // contrast
        0xD9, 0xF1,         // precharge
        0xDB, 0x40,         // VCOM detect
        0xA4,               // display RAM
        0xA6,               // normal display
        0xAF,               // display on
    };
    ssd1306_write_cmds(init_cmds, sizeof(init_cmds));

    ssd1306_task_handle = xTaskGetCurrentTaskHandle();

    ssd1306_show_drop_stats();

    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ssd1306_show_drop_stats();
    }
}

