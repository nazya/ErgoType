#ifndef USB_HOST_LINUX_DELAY_H
#define USB_HOST_LINUX_DELAY_H

#include "hid_compat.h"

static inline void msleep(unsigned int msecs)
{
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

static inline void mdelay(unsigned int msecs)
{
	/*
	 * PORTING DEBT: this sleeps instead of preserving Linux's busy-wait
	 * contract. There is no linked caller; audit its context before enabling
	 * one rather than relying on this reduced implementation.
	 */
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

#endif
