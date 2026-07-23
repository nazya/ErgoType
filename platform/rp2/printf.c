#include "platform/printf.h"

#include "pico/printf.h"

int platform_vfctprintf(
    void (*out)(char character, void *arg),
    void *arg,
    const char *format,
    va_list va)
{
    return vfctprintf(out, arg, format, va);
}
