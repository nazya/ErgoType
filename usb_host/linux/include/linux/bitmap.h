#ifndef USB_HOST_LINUX_BITMAP_H
#define USB_HOST_LINUX_BITMAP_H

#include "hid_compat.h"

// Linux bitmap.h is not imported; keep the upstream bitmap_empty() API as a
// thin firmware compatibility shim over the port bit operations.
static inline bool bitmap_empty(const unsigned long *src, unsigned int nbits)
{
	for (unsigned int bit = 0; bit < nbits; bit++)
		if (test_bit(bit, src))
			return false;

	return true;
}

/* Reduced compatibility implementation of Linux bitmap_weight(). */
static inline unsigned int bitmap_weight(const unsigned long *src,
					 unsigned int nbits)
{
	unsigned int weight = 0;

	for (unsigned int bit = 0; bit < nbits; bit++)
		weight += test_bit(bit, src);
	return weight;
}

#endif
