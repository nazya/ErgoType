#ifndef ERGOTYPE_PLATFORM_CDC_H
#define ERGOTYPE_PLATFORM_CDC_H

#include <stddef.h>

#define PLATFORM_CDC_BUFFER_SIZE 2048u

void platform_cdc_init(void);
void platform_cdc_write(const void *buf, size_t length);
void platform_cdc_poll(void);

#endif
