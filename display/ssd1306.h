#ifndef ERGOTYPE_DISPLAY_SSD1306_H
#define ERGOTYPE_DISPLAY_SSD1306_H

#include <stddef.h>

void ssd1306_task(void *pvParameters);
void ssd1306_notify_cdc_drop(size_t dropped_len);

#endif

