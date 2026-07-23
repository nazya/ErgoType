#ifndef ERGOTYPE_ESP_IDF_PLATFORM_BACKEND_TIME_H
#define ERGOTYPE_ESP_IDF_PLATFORM_BACKEND_TIME_H

#include "esp_rom_sys.h"

static inline void platform_sleep_us(uint64_t us)
{
    esp_rom_delay_us((uint32_t)us);
}

static inline void platform_busy_wait_us_32(uint32_t us)
{
    esp_rom_delay_us(us);
}

#endif
