#include "platform/time.h"

#include "pico/stdlib.h"

void platform_sleep_ms(uint32_t ms)
{
    sleep_ms(ms);
}
