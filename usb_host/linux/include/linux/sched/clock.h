#ifndef USB_HOST_LINUX_SCHED_CLOCK_H
#define USB_HOST_LINUX_SCHED_CLOCK_H

#include "../hid_compat.h"

static inline u64 sched_clock(void)
{
	return (u64)xTaskGetTickCount() * (u64)portTICK_PERIOD_MS * 1000000ULL;
}

#endif
