#include "display/ssd1306.h"

#include <string.h>

#include "hardware/gpio.h"

#define SSD1306_CTRL_CMD_STREAM 0x00u
#define SSD1306_CTRL_DATA_STREAM 0x40u

#define SSD1306_CMD_SET_COLUMN_ADDR 0x21u
#define SSD1306_CMD_SET_PAGE_ADDR 0x22u
#define SSD1306_CMD_SET_MEM_MODE 0x20u

static void ssd1306_write_cmds(ssd1306_t *d, const uint8_t *cmds, size_t n)
{
    uint8_t buf[1 + 32];
    buf[0] = SSD1306_CTRL_CMD_STREAM;

    while (n) {
        size_t chunk = n;
        if (chunk > 32)
            chunk = 32;
        memcpy(&buf[1], cmds, chunk);
        (void)i2c_write_blocking(d->i2c, d->addr, buf, chunk + 1, false);
        cmds += chunk;
        n -= chunk;
    }
}

static void ssd1306_write_data(ssd1306_t *d, const uint8_t *data, size_t n)
{
    uint8_t buf[1 + 32];
    buf[0] = SSD1306_CTRL_DATA_STREAM;

    while (n) {
        size_t chunk = n;
        if (chunk > 32)
            chunk = 32;
        memcpy(&buf[1], data, chunk);
        (void)i2c_write_blocking(d->i2c, d->addr, buf, chunk + 1, false);
        data += chunk;
        n -= chunk;
    }
}

static const uint8_t *ssd1306_glyph5x7(char c)
{
    static const uint8_t space[5] = {0, 0, 0, 0, 0};

    static const uint8_t punct_dash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t punct_dot[5]  = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t punct_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t punct_semi[5]  = {0x00, 0x56, 0x36, 0x00, 0x00};
    static const uint8_t punct_comma[5] = {0x00, 0x50, 0x30, 0x00, 0x00};
    static const uint8_t punct_uscore[5] = {0x40, 0x40, 0x40, 0x40, 0x40};

    static const uint8_t punct_eq[5]    = {0x14, 0x14, 0x14, 0x14, 0x14};
    static const uint8_t punct_plus[5]  = {0x08, 0x08, 0x3E, 0x08, 0x08};
    static const uint8_t punct_pipe[5]  = {0x00, 0x00, 0x7F, 0x00, 0x00};

    static const uint8_t punct_slash[5]  = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t punct_bslash[5] = {0x02, 0x04, 0x08, 0x10, 0x20};

    static const uint8_t punct_lparen[5] = {0x00, 0x1C, 0x22, 0x41, 0x00};
    static const uint8_t punct_rparen[5] = {0x00, 0x41, 0x22, 0x1C, 0x00};
    static const uint8_t punct_lbrack[5] = {0x00, 0x00, 0x7F, 0x41, 0x41};
    static const uint8_t punct_rbrack[5] = {0x41, 0x41, 0x7F, 0x00, 0x00};
    static const uint8_t punct_lt[5]     = {0x00, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t punct_gt[5]     = {0x41, 0x22, 0x14, 0x08, 0x00};

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

    static const uint8_t upper[26][5] = {
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

    static const uint8_t lower[26][5] = {
        {0x20, 0x54, 0x54, 0x54, 0x78}, // a
        {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
        {0x38, 0x44, 0x44, 0x44, 0x20}, // c
        {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
        {0x38, 0x54, 0x54, 0x54, 0x18}, // e
        {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
        {0x08, 0x14, 0x54, 0x54, 0x3C}, // g
        {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
        {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
        {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
        {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
        {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
        {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
        {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
        {0x38, 0x44, 0x44, 0x44, 0x38}, // o
        {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
        {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
        {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
        {0x48, 0x54, 0x54, 0x54, 0x20}, // s
        {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
        {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
        {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
        {0x44, 0x28, 0x10, 0x28, 0x44}, // x
        {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
        {0x44, 0x64, 0x54, 0x4C, 0x44}, // z
    };

    switch (c) {
    case ' ': return space;
    case '-': return punct_dash;
    case '.': return punct_dot;
    case ':': return punct_colon;
    case ';': return punct_semi;
    case ',': return punct_comma;
    case '_': return punct_uscore;
    case '=': return punct_eq;
    case '+': return punct_plus;
    case '|': return punct_pipe;
    case '/': return punct_slash;
    case '\\': return punct_bslash;
    case '(': return punct_lparen;
    case ')': return punct_rparen;
    case '[': return punct_lbrack;
    case ']': return punct_rbrack;
    case '<': return punct_lt;
    case '>': return punct_gt;
    default: break;
    }

    if (c >= '0' && c <= '9')
        return digits[(uint8_t)(c - '0')];
    if (c >= 'A' && c <= 'Z')
        return upper[(uint8_t)(c - 'A')];
    if (c >= 'a' && c <= 'z')
        return lower[(uint8_t)(c - 'a')];

    return space;
}

void ssd1306_init(ssd1306_t *d,
                  i2c_inst_t *i2c,
                  uint8_t addr,
                  uint16_t width,
                  uint16_t height,
                  uint32_t baud,
                  uint8_t contrast,
                  uint8_t precharge,
                  uint8_t vcomh,
                  int sda,
                  int scl)
{
    d->i2c = i2c;
    d->addr = addr;
    d->width = width;
    d->height = height;

    if (d->width > SSD1306_MAX_WIDTH)
        d->width = SSD1306_MAX_WIDTH;
    if (d->height > SSD1306_MAX_HEIGHT)
        d->height = SSD1306_MAX_HEIGHT;

    d->pages = (uint8_t)(d->height / 8u);
    d->cols = (uint8_t)(d->width / 6u);
    d->rows = d->pages;

    gpio_set_function((uint)sda, GPIO_FUNC_I2C);
    gpio_set_function((uint)scl, GPIO_FUNC_I2C);
    gpio_pull_up((uint)sda);
    gpio_pull_up((uint)scl);
    (void)i2c_init(d->i2c, baud);

    const uint8_t com_pins = (d->height == 32u) ? 0x02u : 0x12u;
    const uint8_t clock = (d->height == 32u) ? 0x80u : 0xF0u;
    const uint8_t mux = (uint8_t)(d->height - 1u);
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
        0x81, contrast,     // contrast
        0xD9, precharge,    // precharge
        0xDB, vcomh,        // VCOMH deselect level
        0xA4,               // display RAM
        0xA6,               // normal display
        0xAF,               // display on
    };
    ssd1306_write_cmds(d, init_cmds, sizeof(init_cmds));

    ssd1306_clear(d);
    ssd1306_flush(d);
}

void ssd1306_clear(ssd1306_t *d)
{
    memset(d->fb, 0, sizeof(d->fb));
}

void ssd1306_putc(ssd1306_t *d, uint8_t col, uint8_t row, char c)
{
    if (row >= d->rows || col >= d->cols)
        return;

    size_t off = (size_t)row * (size_t)d->width + (size_t)col * 6u;
    const uint8_t *g = ssd1306_glyph5x7(c);
    for (uint8_t i = 0; i < 5; ++i)
        d->fb[off++] = g[i];
    d->fb[off] = 0x00;
}

void ssd1306_puts(ssd1306_t *d, uint8_t col, uint8_t row, const char *s)
{
    while (*s && col < d->cols) {
        ssd1306_putc(d, col, row, *s++);
        col++;
    }
}

void ssd1306_box(ssd1306_t *d, uint8_t col, uint8_t row, uint8_t w, uint8_t h)
{
    if (w < 2 || h < 2)
        return;

    ssd1306_putc(d, col, row, '+');
    for (uint8_t x = 1; x < (uint8_t)(w - 1); ++x)
        ssd1306_putc(d, (uint8_t)(col + x), row, '-');
    ssd1306_putc(d, (uint8_t)(col + w - 1), row, '+');

    for (uint8_t y = 1; y < (uint8_t)(h - 1); ++y) {
        ssd1306_putc(d, col, (uint8_t)(row + y), '|');
        ssd1306_putc(d, (uint8_t)(col + w - 1), (uint8_t)(row + y), '|');
    }

    ssd1306_putc(d, col, (uint8_t)(row + h - 1), '+');
    for (uint8_t x = 1; x < (uint8_t)(w - 1); ++x)
        ssd1306_putc(d, (uint8_t)(col + x), (uint8_t)(row + h - 1), '-');
    ssd1306_putc(d, (uint8_t)(col + w - 1), (uint8_t)(row + h - 1), '+');
}

void ssd1306_flush(ssd1306_t *d)
{
    const uint8_t width_minus_1 = (uint8_t)(d->width - 1u);
    const uint8_t page_end = (uint8_t)(d->pages - 1u);

    const uint8_t addr_cmds[] = {
        SSD1306_CMD_SET_COLUMN_ADDR, 0u, width_minus_1,
        SSD1306_CMD_SET_PAGE_ADDR, 0u, page_end,
    };
    ssd1306_write_cmds(d, addr_cmds, sizeof(addr_cmds));

    const size_t n = (size_t)d->width * (size_t)d->pages;
    ssd1306_write_data(d, d->fb, n);
}
