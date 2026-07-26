#include "platform/os.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#define PLATFORM_MAX_TASKS 8

typedef struct {
    struct k_thread thread;
    platform_task_fn_t fn;
    void *arg;
    bool used;
} platform_task_slot_t;

static platform_task_slot_t task_slots[PLATFORM_MAX_TASKS];

static void platform_task_entry(void *p1, void *p2, void *p3)
{
    (void)p2;
    (void)p3;

    platform_task_slot_t *slot = p1;
    slot->fn(slot->arg);
}

int platform_task_start(const char *name, platform_task_fn_t fn, void *arg,
                        void *stack, uint32_t stack_size, int priority)
{
    if (!fn || !stack || stack_size == 0)
        return -EINVAL;

    platform_task_slot_t *slot = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(task_slots); ++i) {
        if (!task_slots[i].used) {
            slot = &task_slots[i];
            slot->used = true;
            break;
        }
    }

    if (!slot)
        return -ENOMEM;

    slot->fn = fn;
    slot->arg = arg;

    k_tid_t tid = k_thread_create(&slot->thread,
                                  (k_thread_stack_t *)stack,
                                  stack_size,
                                  platform_task_entry,
                                  slot,
                                  NULL,
                                  NULL,
                                  priority,
                                  0,
                                  K_NO_WAIT);
    if (!tid) {
        slot->used = false;
        return -EIO;
    }

    if (name)
        k_thread_name_set(tid, name);

    return 0;
}
