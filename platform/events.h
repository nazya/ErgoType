#ifndef ERGOTYPE_PLATFORM_EVENTS_H
#define ERGOTYPE_PLATFORM_EVENTS_H

#include <stdint.h>

#include "device.h"

#define PLATFORM_EVENT_WAIT_FOREVER (-1)
#define PLATFORM_EVENT_NO_WAIT 0

int platform_input_events_init(void);
int platform_input_event_send(const struct device_event *event, int32_t timeout_ms);
int platform_input_event_recv(struct device_event *event, int32_t timeout_ms);

#endif
