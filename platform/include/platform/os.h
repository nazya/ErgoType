#ifndef ERGOTYPE_PLATFORM_OS_H
#define ERGOTYPE_PLATFORM_OS_H

#include <stddef.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#else
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"
#endif

#ifdef ESP_PLATFORM
#define PLATFORM_STACK_DEPTH(bytes) ((configSTACK_DEPTH_TYPE)(bytes))
#else
#define PLATFORM_STACK_DEPTH(bytes) \
    ((configSTACK_DEPTH_TYPE)(((bytes) + sizeof(StackType_t) - 1u) / sizeof(StackType_t)))
#endif

BaseType_t platform_task_create_affinity(TaskFunction_t task,
                                         const char *name,
                                         configSTACK_DEPTH_TYPE stack_depth,
                                         void *parameters,
                                         UBaseType_t priority,
                                         UBaseType_t core_affinity_mask,
                                         TaskHandle_t *created_task);
void *platform_large_alloc(size_t size);
void platform_large_free(void *ptr);

#endif
