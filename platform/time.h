#ifndef ERGOTYPE_PLATFORM_TIME_H
#define ERGOTYPE_PLATFORM_TIME_H

#include <stdint.h>

void platform_sleep_ms(uint32_t ms);
void platform_sleep_us(uint32_t us);
uint32_t platform_uptime_ms(void);

#endif
