#ifndef USB_HOST_LINUX_MATH64_H
#define USB_HOST_LINUX_MATH64_H

#include "hid_compat.h"

static inline s64 div_s64(s64 dividend, s32 divisor)
{
	return dividend / divisor;
}

#endif
