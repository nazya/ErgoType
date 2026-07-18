#ifndef RTOS_FREERTOS_HOOK_H
#define RTOS_FREERTOS_HOOK_H

#include <stdint.h>

/* Number of dynamic allocation failures observed since boot. */
uint32_t freertos_malloc_failure_count(void);

#endif
