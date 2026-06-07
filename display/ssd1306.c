#include "display/ssd1306.h"

#include <string.h>

#include "hardware/i2c.h"

#include "display/px4x8_font.h"
#include "display/px6x8_font.h"

#define SSD1306_CTRL_CMD_STREAM 0x00u
#define SSD1306_CTRL_DATA_STREAM 0x40u

#define SSD1306_CMD_SET_MEM_MODE 0x20u
#define SSD1306_CMD_SET_COLUMN_ADDR 0x21u
#define SSD1306_CMD_SET_PAGE_ADDR 0x22u
#define SSD1306_I2C_CHUNK 32u

static i2c_inst_t *const i2c_by_idx[MAX_I2C] = { i2c0, i2c1 };

static inline i2c_inst_t *ssd1306_i2c(const ssd1306_cfg_t *cfg)
{
    return i2c_by_idx[(uint8_t)cfg->i2c_idx];
}

static void ssd1306_write_cmds(const ssd1306_cfg_t *cfg, const uint8_t *cmds, size_t n);
static void ssd1306_write_data(const ssd1306_cfg_t *cfg, const uint8_t *data, size_t n);

void ssd1306_write_page_span(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t x, const uint8_t *data, uint8_t n)
{
    const uint8_t x1 = x + n - 1u;
    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, x, x1,
        SSD1306_CMD_SET_PAGE_ADDR, row, row,
    };
    ssd1306_write_cmds(cfg, addr_cmds, sizeof(addr_cmds));
    ssd1306_write_data(cfg, data, n);
}

static void ssd1306_write_cmds(const ssd1306_cfg_t *cfg, const uint8_t *cmds, size_t n)
{
    uint8_t buf[1 + SSD1306_I2C_CHUNK];
    buf[0] = SSD1306_CTRL_CMD_STREAM;
    i2c_inst_t *i2c = ssd1306_i2c(cfg);

    while (n) {
        size_t chunk = n;
        if (chunk > SSD1306_I2C_CHUNK)
            chunk = SSD1306_I2C_CHUNK;
        memcpy(&buf[1], cmds, chunk);
        (void)i2c_write_blocking(i2c, cfg->addr, buf, chunk + 1, false);
        cmds += chunk;
        n -= chunk;
    }
}

static void ssd1306_write_data(const ssd1306_cfg_t *cfg, const uint8_t *data, size_t n)
{
    uint8_t buf[1 + SSD1306_I2C_CHUNK];
    buf[0] = SSD1306_CTRL_DATA_STREAM;
    i2c_inst_t *i2c = ssd1306_i2c(cfg);

    while (n) {
        size_t chunk = n;
        if (chunk > SSD1306_I2C_CHUNK)
            chunk = SSD1306_I2C_CHUNK;
        memcpy(&buf[1], data, chunk);
        (void)i2c_write_blocking(i2c, cfg->addr, buf, chunk + 1, false);
        data += chunk;
        n -= chunk;
    }
}

void ssd1306_init(const ssd1306_cfg_t *cfg)
{
    const uint8_t com_pins = (cfg->height == 32u) ? 0x02u : 0x12u;
    const uint8_t clock = (cfg->height == 32u) ? 0x80u : 0xF0u;
    const uint8_t mux = cfg->height - 1u;
    const uint8_t init_cmds[] = {
        0xAE,               // display off
        0xD5, clock,        // clock div / oscillator freq
        0xA8, mux,          // multiplex
        0xD3, 0x00,         // display offset
        0x40,               // start line 0
        0x8D, 0x14,         // charge pump
        SSD1306_CMD_SET_MEM_MODE, 0x00, // horizontal addressing mode
        0xA1,               // segment remap
        0xC8,               // COM scan direction
        0xDA, com_pins,     // COM pins
        0x81, cfg->contrast,     // contrast
        0xD9, cfg->precharge,    // precharge
        0xDB, cfg->vcomh,        // VCOMH deselect level
        0xA4,               // display RAM
        0xA6,               // normal display
        0xAF,               // display on
    };
    ssd1306_write_cmds(cfg, init_cmds, sizeof(init_cmds));

    ssd1306_clear(cfg);
}

void ssd1306_clear(const ssd1306_cfg_t *cfg)
{
    const uint8_t pages = cfg->height / 8u;
    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, 0u, cfg->width - 1u,
        SSD1306_CMD_SET_PAGE_ADDR, 0u, pages - 1u,
    };
    ssd1306_write_cmds(cfg, addr_cmds, sizeof(addr_cmds));

    const uint8_t zeros[SSD1306_I2C_CHUNK] = {0};
    size_t n = (size_t)cfg->width * (size_t)pages;
    for (; n >= sizeof(zeros); n -= sizeof(zeros))
        ssd1306_write_data(cfg, zeros, sizeof(zeros));
    if (n)
        ssd1306_write_data(cfg, zeros, n);
}

void ssd1306_clear_page(const ssd1306_cfg_t *cfg, uint8_t row)
{
    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, 0u, cfg->width - 1u,
        SSD1306_CMD_SET_PAGE_ADDR, row, row,
    };
    ssd1306_write_cmds(cfg, addr_cmds, sizeof(addr_cmds));

    const uint8_t zeros[SSD1306_I2C_CHUNK] = {0};
    size_t n = cfg->width;
    for (; n >= sizeof(zeros); n -= sizeof(zeros))
        ssd1306_write_data(cfg, zeros, sizeof(zeros));
    if (n)
        ssd1306_write_data(cfg, zeros, n);
}

void ssd1306_putn4x8(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, const char *s, size_t n)
{
    const uint8_t x0 = col * 4u;
    const uint8_t x1 = x0 + n * 4u - 1u;
    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, x0, x1,
        SSD1306_CMD_SET_PAGE_ADDR, row, row,
    };
    ssd1306_write_cmds(cfg, addr_cmds, sizeof(addr_cmds));

    for (size_t i = 0; i < n; ++i)
        ssd1306_write_data(cfg, px4x8_glyphs[(uint8_t)s[i]], 4u);
}

void ssd1306_putn6x8_x(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t x, const char *s, size_t n)
{
    const uint8_t x1 = x + n * 6u - 1u;
    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, x, x1,
        SSD1306_CMD_SET_PAGE_ADDR, row, row,
    };
    ssd1306_write_cmds(cfg, addr_cmds, sizeof(addr_cmds));

    for (size_t i = 0; i < n; ++i)
        ssd1306_write_data(cfg, px6x8_glyphs[(uint8_t)s[i]], 6u);
}

void ssd1306_putn6x8(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, const char *s, size_t n)
{
    ssd1306_putn6x8_x(cfg, row, col * 6u, s, n);
}

void ssd1306_box(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, uint8_t w, uint8_t h)
{
    const uint8_t col_last = col + w - 1u;
    const uint8_t row_last = row + h - 1u;

    ssd1306_putn4x8(cfg, row, col, "+", 1u);
    for (uint8_t x = 1; x < w - 1u; ++x)
        ssd1306_putn4x8(cfg, row, col + x, "-", 1u);
    ssd1306_putn4x8(cfg, row, col_last, "+", 1u);

    for (uint8_t y = 1; y < h - 1u; ++y) {
        ssd1306_putn4x8(cfg, row + y, col, "|", 1u);
        ssd1306_putn4x8(cfg, row + y, col_last, "|", 1u);
    }

    ssd1306_putn4x8(cfg, row_last, col, "+", 1u);
    for (uint8_t x = 1; x < w - 1u; ++x)
        ssd1306_putn4x8(cfg, row_last, col + x, "-", 1u);
    ssd1306_putn4x8(cfg, row_last, col_last, "+", 1u);
}
