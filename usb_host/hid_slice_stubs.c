#include "linux/include/linux/hid.h"
#include "linux/include/linux/hidraw.h"

/*
 * Upstream Linux implements hidraw, work cancellation, and input timers in
 * separate subsystems. The callback-driven HID host slice keeps those call
 * sites linkable but inactive until proxy/raw/workqueue ownership is wired.
 */

int hidraw_init(void)
{
    return 0;
}

void hidraw_exit(void)
{
}

int hidraw_connect(struct hid_device *hid)
{
    /*
     * Upstream hid_connect() calls hidraw_connect() for HID_CONNECT_HIDRAW.
     * Firmware has no active hidraw listener yet, so leave HIDRAW unclaimed.
     */
    (void)hid;
    return -ENOSYS;
}

void hidraw_disconnect(struct hid_device *hid)
{
    (void)hid;
}

int hidraw_report_event(struct hid_device *hid, u8 *data, int len)
{
    /*
     * Upstream buffers this for hidraw readers. Current proxy/raw boundary is
     * not wired, so keep the report parser path running and drop at the edge.
     */
    (void)hid;
    (void)data;
    (void)len;
    return 0;
}

void cancel_work_sync(struct work_struct *work)
{
    /* Upstream waits for queued work; this slice does not schedule work. */
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
