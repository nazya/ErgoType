#include "platform/os.h"

#include "esp_heap_caps.h"

BaseType_t platform_task_create_affinity(TaskFunction_t task,
                                         const char *name,
                                         configSTACK_DEPTH_TYPE stack_depth,
                                         void *parameters,
                                         UBaseType_t priority,
                                         UBaseType_t core_affinity_mask,
                                         TaskHandle_t *created_task)
{
    const char *task_name = name ? name : "ergotype";
#if defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE
    (void)core_affinity_mask;
    return xTaskCreate(task, task_name, stack_depth, parameters, priority, created_task);
#else
    const BaseType_t core = (core_affinity_mask & (1u << 1)) ? 1 : 0;
    return xTaskCreatePinnedToCore(task, task_name, stack_depth, parameters, priority, created_task, core);
#endif
}

void *platform_large_alloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void platform_large_free(void *ptr)
{
    heap_caps_free(ptr);
}
