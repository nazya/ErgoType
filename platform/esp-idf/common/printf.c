#include "platform/printf.h"

#include <stdio.h>

int platform_vfctprintf(
    void (*out)(char character, void *arg),
    void *arg,
    const char *format,
    va_list va)
{
    static char buffer[2048];
    int n = vsnprintf(buffer, sizeof(buffer), format, va);
    if (n < 0)
        return n;

    int limit = n;
    if (limit >= (int)sizeof(buffer))
        limit = (int)sizeof(buffer) - 1;
    for (int i = 0; i < limit; ++i)
        out(buffer[i], arg);
    return n;
}
