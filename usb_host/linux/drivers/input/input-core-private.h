/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef USB_HOST_LINUX_INPUT_CORE_PRIVATE_H
#define USB_HOST_LINUX_INPUT_CORE_PRIVATE_H

#include "../../include/linux/input.h"

/*
 * Reduced private input-core shim. Upstream declares more inhibit helpers;
 * keep the MT/input event declarations used by the local input core.
 */

void input_mt_release_slots(struct input_dev *dev);
void input_handle_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value);

#endif
