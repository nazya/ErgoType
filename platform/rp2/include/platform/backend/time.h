#ifndef ERGOTYPE_RP2_PLATFORM_BACKEND_TIME_H
#define ERGOTYPE_RP2_PLATFORM_BACKEND_TIME_H

#include "hardware/timer.h"
#include "pico/time.h"

static inline void platform_sleep_us(uint64_t us)
{
    sleep_us(us);
}

static inline void platform_busy_wait_us_32(uint32_t us)
{
    busy_wait_us_32(us);
}

#endif
