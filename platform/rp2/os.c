#include "platform/os.h"

BaseType_t platform_task_create_affinity(TaskFunction_t task,
                                         const char *name,
                                         configSTACK_DEPTH_TYPE stack_depth,
                                         void *parameters,
                                         UBaseType_t priority,
                                         UBaseType_t core_affinity_mask,
                                         TaskHandle_t *created_task)
{
    return xTaskCreateAffinitySet(
        task, name, stack_depth, parameters, priority, core_affinity_mask, created_task);
}

void *platform_large_alloc(size_t size)
{
    return pvPortMalloc(size);
}

void platform_large_free(void *ptr)
{
    vPortFree(ptr);
}
