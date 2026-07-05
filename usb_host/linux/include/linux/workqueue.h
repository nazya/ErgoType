#ifndef USB_HOST_LINUX_WORKQUEUE_H
#define USB_HOST_LINUX_WORKQUEUE_H

#include "hid_compat.h"

// Upstream Linux workqueue API is reduced to the HID driver-task subset
// implemented in workqueue.c.

#define queue_work(wq, work) hid_compat_queue_work((wq), (work))
#define queue_delayed_work(wq, dwork, delay) \
	hid_compat_queue_delayed_work((wq), (dwork), (delay))
#define flush_work(work) hid_compat_flush_work(work)

static inline struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
	return hid_compat_create_singlethread_workqueue(name);
}

static inline void destroy_workqueue(struct workqueue_struct *wq)
{
	hid_compat_destroy_workqueue(wq);
}

#endif
