#ifndef USB_HOST_ASM_BYTEORDER_H
#define USB_HOST_ASM_BYTEORDER_H

#include "linux/hid_compat.h"

static inline __le16 cpu_to_le16(u16 v)
{
	return v;
}

#endif
