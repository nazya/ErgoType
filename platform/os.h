#ifndef ERGOTYPE_PLATFORM_OS_H
#define ERGOTYPE_PLATFORM_OS_H

#include <stdint.h>

typedef void (*platform_task_fn_t)(void *arg);

int platform_task_start(const char *name, platform_task_fn_t fn, void *arg,
                        void *stack, uint32_t stack_size, int priority);

#endif
