#ifndef USB_HOST_LINUX_WORKQUEUE_H
#define USB_HOST_LINUX_WORKQUEUE_H

#include "hid_compat.h"

/*
 * Upstream Linux workqueues run kernel workers and allow blocking flush/cancel
 * calls. The firmware bridge keeps queue_work() / schedule_work() nonblocking;
 * flush_work() and cancel_work_sync() may block only after HID disconnect has
 * been handed from TinyUSB callbacks to the usbhid lifecycle task.
 */

#endif
