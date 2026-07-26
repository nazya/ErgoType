#ifndef ERGOTYPE_PLATFORM_VKBD_EVENTS_H
#define ERGOTYPE_PLATFORM_VKBD_EVENTS_H

#include <stdint.h>

#include "vkbd/vkbd_event.h"

#define PLATFORM_VKBD_EVENT_WAIT_FOREVER (-1)
#define PLATFORM_VKBD_EVENT_NO_WAIT 0

int platform_vkbd_events_init(void);
int platform_vkbd_event_send(const vkbd_event_t *event, int32_t timeout_ms);
int platform_vkbd_event_recv(vkbd_event_t *event, int32_t timeout_ms);

#endif
