#ifndef USB_HOST_LINUX_DELAY_H
#define USB_HOST_LINUX_DELAY_H

#include "hid_compat.h"

static inline void msleep(unsigned int msecs)
{
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

static inline void mdelay(unsigned int msecs)
{
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

#endif
