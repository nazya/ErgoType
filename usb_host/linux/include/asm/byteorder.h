#ifndef USB_HOST_ASM_BYTEORDER_H
#define USB_HOST_ASM_BYTEORDER_H

#include "linux/hid_compat.h"

static inline __le16 cpu_to_le16(u16 v)
{
	return v;
}

static inline __le32 cpu_to_le32(u32 v)
{
	/* Every supported firmware MCU target is little-endian. */
	return v;
}

#endif
