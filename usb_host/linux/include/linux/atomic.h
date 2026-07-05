#ifndef USB_HOST_LINUX_ATOMIC_H
#define USB_HOST_LINUX_ATOMIC_H

#include "hid_compat.h"

static inline void atomic_set(atomic_t *v, int i)
{
	taskENTER_CRITICAL();
	*v = i;
	taskEXIT_CRITICAL();
}

static inline void atomic_dec(atomic_t *v)
{
	taskENTER_CRITICAL();
	--*v;
	taskEXIT_CRITICAL();
}

#endif
