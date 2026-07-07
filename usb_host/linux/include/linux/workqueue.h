#ifndef USB_HOST_LINUX_WORKQUEUE_H
#define USB_HOST_LINUX_WORKQUEUE_H

#include "hid_compat.h"

/*
 * Work3 implemented a firmware workqueue bridge here. The callback-driven HID
 * slice keeps work_struct types for upstream declarations, but does not expose
 * queue_work()/flush_work() entry points until driver continuations are wired.
 */

#endif
