#include "linux/include/linux/hid.h"
#include "linux/include/linux/hidraw.h"

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

void cancel_work_sync(struct work_struct *work)
{
    (void)work;
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
