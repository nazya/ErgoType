#include "linux/include/linux/hid.h"
#include "linux/include/linux/hidraw.h"
#include "linux/include/linux/input.h"
#include "linux/include/linux/workqueue.h"

int hidraw_init(void)
{
    return 0;
}

void hidraw_exit(void)
{
}

int hidraw_connect(struct hid_device *hid)
{
    (void)hid;
    return -ENOSYS;
}

void hidraw_disconnect(struct hid_device *hid)
{
    (void)hid;
}

int hidraw_report_event(struct hid_device *hid, u8 *data, int len)
{
    (void)hid;
    (void)data;
    (void)len;
    return 0;
}

void input_port_proxy_hid_usage_event(struct hid_device *hid, struct input_dev *dev,
                                      struct hid_field *field,
                                      struct hid_usage *usage, __s32 value)
{
    /*
     * Current test slice stops at Linux input-event generation. KeyD/proxy
     * forwarding comes after mount/report parser is verified on hardware.
     */
    (void)hid;
    (void)dev;
    (void)field;
    (void)usage;
    (void)value;
}

int hid_port_register_device(struct hid_device *hid)
{
    /*
     * Boundary intentionally disabled in this slice. hid_connect() still runs
     * hid-input setup; firmware forwarding is the next hardware-test layer.
     */
    (void)hid;
    return 0;
}

void hid_port_unregister_device(struct hid_device *hid)
{
    (void)hid;
}

int hid_compat_workqueue_init(void)
{
    return 0;
}

bool hid_compat_workqueue_poll(void)
{
    return false;
}

TickType_t hid_compat_workqueue_next_timeout(void)
{
    return portMAX_DELAY;
}

bool schedule_work(struct work_struct *work)
{
    (void)work;
    return false;
}

bool hid_compat_queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
    (void)wq;
    (void)work;
    return false;
}

bool hid_compat_flush_work(struct work_struct *work)
{
    (void)work;
    return true;
}

struct workqueue_struct *hid_compat_create_singlethread_workqueue(const char *name)
{
    (void)name;
    return NULL;
}

void hid_compat_destroy_workqueue(struct workqueue_struct *wq)
{
    (void)wq;
}

void cancel_work_sync(struct work_struct *work)
{
    (void)work;
}

bool hid_compat_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
                                   unsigned long delay)
{
    (void)wq;
    (void)dwork;
    (void)delay;
    return false;
}

bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
    (void)dwork;
    (void)delay;
    return false;
}

bool cancel_delayed_work(struct delayed_work *dwork)
{
    (void)dwork;
    return false;
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
    (void)dwork;
    return false;
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
    /*
     * Linux input autorepeat timer is not part of this parser bring-up slice.
     * Keep the input core call sites intact and make the final boundary no-op.
     */
    if (timer)
        timer->expires = expires;
    return 0;
}

int timer_delete_sync(struct timer_list *timer)
{
    if (timer)
        timer->pending = 0;
    return 0;
}

int hid_compat_executor_init(void)
{
    return 0;
}

bool hid_compat_in_executor(void)
{
    return true;
}

void hid_compat_executor_kick(void)
{
}

int hid_compat_exec_sync(hid_compat_exec_fn_t fn, void *data)
{
    return fn(data);
}

int hid_compat_exec_async(hid_compat_exec_fn_t fn, void *data)
{
    return fn(data);
}
