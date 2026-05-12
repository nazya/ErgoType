#ifndef ERGOTYPE_DISPLAY_SSD1306_H
#define ERGOTYPE_DISPLAY_SSD1306_H

#include <stddef.h>
#include <stdint.h>

#include "hardware/i2c.h"

#define SSD1306_MAX_WIDTH 128u
#define SSD1306_MAX_HEIGHT 64u
#define SSD1306_FB_SIZE (SSD1306_MAX_WIDTH * (SSD1306_MAX_HEIGHT / 8u))

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    uint16_t width;
    uint16_t height;
    uint8_t pages;
    uint8_t cols;
    uint8_t rows;
    uint8_t fb[SSD1306_FB_SIZE];
} ssd1306_t;

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
                  int scl);

void ssd1306_clear(ssd1306_t *d);
void ssd1306_putc(ssd1306_t *d, uint8_t row, uint8_t col, char c);
void ssd1306_puts(ssd1306_t *d, uint8_t row, uint8_t col, const char *s);
void ssd1306_box(ssd1306_t *d, uint8_t row, uint8_t col, uint8_t w, uint8_t h);
void ssd1306_flush(ssd1306_t *d);
void ssd1306_flush_cols(ssd1306_t *d, uint8_t x0, uint8_t x1);

#endif
