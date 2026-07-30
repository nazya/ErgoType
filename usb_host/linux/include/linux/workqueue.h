#ifndef USB_HOST_LINUX_WORKQUEUE_H
#define USB_HOST_LINUX_WORKQUEUE_H

#include "hid_compat.h"

/*
 * Upstream Linux workqueues run kernel workers and allow blocking flush/cancel
 * calls. The firmware bridge keeps queue_work() / schedule_work() nonblocking;
 * synchronous flush/cancel calls run only from task-context probe, teardown,
 * and lifecycle paths, never directly from TinyUSB callbacks.
 */

#endif
