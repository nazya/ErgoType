#ifndef ERGOTYPE_DISPLAY_SSD1306_H
#define ERGOTYPE_DISPLAY_SSD1306_H

#include <stddef.h>
#include <stdint.h>

#include "jconfig.h"

void ssd1306_init(const ssd1306_cfg_t *cfg);
void ssd1306_clear(const ssd1306_cfg_t *cfg);
void ssd1306_clear_page(const ssd1306_cfg_t *cfg, uint8_t row);
void ssd1306_write_page_span(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t x, const uint8_t *data, uint8_t n);
void ssd1306_putn4x8(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, const char *s, size_t n);
void ssd1306_putn6x8_x(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t x, const char *s, size_t n);
void ssd1306_putn6x8(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, const char *s, size_t n);
void ssd1306_box(const ssd1306_cfg_t *cfg, uint8_t row, uint8_t col, uint8_t w, uint8_t h);

#endif
