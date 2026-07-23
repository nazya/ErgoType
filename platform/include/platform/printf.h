#ifndef ERGOTYPE_PLATFORM_PRINTF_H
#define ERGOTYPE_PLATFORM_PRINTF_H

#include <stdarg.h>
int platform_vfctprintf(
    void (*out)(char character, void *arg),
    void *arg,
    const char *format,
    va_list va);

#endif
