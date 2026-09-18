/* SPDX-License-Identifier: GPL-2.0 */
#ifndef USB_HOST_LINUX_ASUS_WMI_H
#define USB_HOST_LINUX_ASUS_WMI_H

#include <linux/hid_compat.h>

/*
 * Reduced upstream CONFIG_ASUS_WMI=n contract. Firmware has no laptop WMI
 * service; these events must return -ENODEV so hid-asus forwards the keys.
 * Listener/backlight/Ally interfaces are deliberately not provided.
 */
#if IS_REACHABLE(CONFIG_ASUS_WMI)
#error "ASUS WMI is not implemented by the firmware port"
#endif

enum asus_hid_event {
	ASUS_EV_BRTUP,
	ASUS_EV_BRTDOWN,
	ASUS_EV_BRTTOGGLE,
};

static inline int asus_hid_event(enum asus_hid_event event)
{
	(void)event;
	return -ENODEV;
}

#endif
