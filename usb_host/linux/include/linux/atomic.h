#ifndef USB_HOST_LINUX_ATOMIC_H
#define USB_HOST_LINUX_ATOMIC_H

#include "hid_compat.h"

static inline void atomic_set(atomic_t *v, int i)
{
	/* Linux non-RMW atomic_set() is unordered. */
	__atomic_store_n(v, i, __ATOMIC_RELAXED);
}

static inline void atomic_dec(atomic_t *v)
{
	/* Linux non-returning atomic arithmetic carries no implicit barrier. */
	(void)__atomic_fetch_sub(v, 1, __ATOMIC_RELAXED);
}

#endif
