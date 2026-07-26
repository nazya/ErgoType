#include "platform/time.h"

#include <zephyr/kernel.h>

void platform_sleep_ms(uint32_t ms)
{
    k_sleep(K_MSEC(ms));
}

void platform_sleep_us(uint32_t us)
{
    k_busy_wait(us);
}

uint32_t platform_uptime_ms(void)
{
    return k_uptime_get_32();
}
