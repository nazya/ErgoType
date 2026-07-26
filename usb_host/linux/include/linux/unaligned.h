#ifndef USB_HOST_LINUX_UNALIGNED_H
#define USB_HOST_LINUX_UNALIGNED_H

#include "hid_compat.h"

/*
 * Linux's generic put_unaligned() permits typed unaligned destinations.
 * memcpy preserves that contract on Cortex-M0+ without an unaligned store.
 */
#define put_unaligned(value, ptr)                                      \
	do {                                                           \
		__typeof__(*(ptr)) put_unaligned_value = (value);        \
		memcpy((ptr), &put_unaligned_value,                     \
		       sizeof(put_unaligned_value));                    \
	} while (0)

#endif
