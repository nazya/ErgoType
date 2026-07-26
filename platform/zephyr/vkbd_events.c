#include "platform/vkbd_events.h"

#include <errno.h>

#include <zephyr/kernel.h>

K_MSGQ_DEFINE(platform_vkbd_event_queue, sizeof(vkbd_event_t), 256, 4);

static k_timeout_t event_timeout(int32_t timeout_ms)
{
    if (timeout_ms < 0)
        return K_FOREVER;
    if (timeout_ms == 0)
        return K_NO_WAIT;
    return K_MSEC(timeout_ms);
}

int platform_vkbd_events_init(void)
{
    k_msgq_purge(&platform_vkbd_event_queue);
    return 0;
}

int platform_vkbd_event_send(const vkbd_event_t *event, int32_t timeout_ms)
{
    if (!event)
        return -EINVAL;

    return k_msgq_put(&platform_vkbd_event_queue, event, event_timeout(timeout_ms));
}

int platform_vkbd_event_recv(vkbd_event_t *event, int32_t timeout_ms)
{
    if (!event)
        return -EINVAL;

    return k_msgq_get(&platform_vkbd_event_queue, event, event_timeout(timeout_ms));
}
