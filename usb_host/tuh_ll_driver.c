#include "tusb.h"

#include "linux/include/linux/hid.h"
#include "linux/include/linux/usb.h"

/*
 * Upstream Linux uses usbhid as the low-level HID transport. This file is the
 * TinyUSB hid_ll_driver adapter and keeps synchronous hardware requests as
 * explicit no-op/ENOSYS boundaries until async control/report handling exists.
 */

static int tuh_ll_start(struct hid_device *hdev)
{
    /*
     * Upstream usbhid start may arm polling, reset LEDs, and enable wakeup.
     * Current callback slice arms interrupt IN from TinyUSB mount/report
     * callbacks and leaves control/output side effects disabled.
     */
    (void)hdev;
    return 0;
}

static void tuh_ll_stop(struct hid_device *hdev)
{
    /*
     * Upstream usbhid stop cancels URBs and queued delayed work. Current slice
     * has no transport queues/work items to drain here.
     */
    (void)hdev;
}

static int tuh_ll_open(struct hid_device *hdev)
{
    /*
     * Temporary callback-driven slice: TinyUSB interrupt IN is already armed
     * from mount/report callbacks. Do not start a second transport path here.
     */
    (void)hdev;
    return 0;
}

static void tuh_ll_close(struct hid_device *hdev)
{
    /*
     * Upstream close kills interrupt IN unless ALWAYS_POLL is active. Current
     * slice keeps receive ownership in TinyUSB callbacks.
     */
    (void)hdev;
}

static int tuh_ll_parse(struct hid_device *hdev)
{
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    struct usb_host_interface *interface = intf->cur_altsetting;
    unsigned long quirks = hdev->quirks;
    unsigned long transport_quirks = 0;
    int ret;

    if (!hdev->ll_rdesc || !hdev->ll_rsize)
        return -ENODEV;

    if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
        if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
            interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE) {
            /*
             * Keep upstream usbhid_parse() behavior for boot keyboards/mice:
             * do not GET_REPORT during init. In this slice all hardware
             * request paths are disabled anyway, but keeping the quirk matters
             * for later async request work.
             */
            quirks |= HID_QUIRK_NOGET;
            transport_quirks |= HID_QUIRK_NOGET;
        }
    }

    // ret = usbhid_parse(hid);
    // TinyUSB supplies the report descriptor to tuh_hid_mount_cb(); this
    // ll_driver consumes it here so hid_add_device() keeps upstream parse flow.
    ret = hid_parse_report(hdev, hdev->ll_rdesc, hdev->ll_rsize);
    if (ret)
        return ret;

    // hid->quirks |= quirks;
    // hid_device_probe() recomputes quirks from initial_quirks after parse();
    // keep transport-added usbhid_parse() quirks alive.
    hdev->initial_quirks |= transport_quirks;
    hdev->quirks |= quirks;
    return 0;
}

static void tuh_ll_request(struct hid_device *hdev, struct hid_report *report,
                           enum hid_class_request reqtype)
{
    /*
     * usbhid_submit_report(hdev, report, reqtype);
     * Hardware request path intentionally disabled for this bring-up step.
     * Later this becomes async request/response/continuation, not hid_hw_wait().
     */
    (void)hdev;
    (void)report;
    (void)reqtype;
}

static int tuh_ll_wait(struct hid_device *hdev)
{
    /*
     * usbhid_wait_io(hdev);
     * Do not block TinyUSB callbacks. This slice keeps parser/input behavior
     * active while sync hardware wait semantics are deferred.
     */
    (void)hdev;
    return 0;
}

static int tuh_ll_raw_request(struct hid_device *hdev, unsigned char reportnum,
                              __u8 *buf, size_t len, unsigned char rtype,
                              int reqtype)
{
    /*
     * Upstream raw GET/SET report is backed by synchronous USB control
     * transfers. Current slice has no async continuation yet.
     */
    (void)hdev;
    (void)reportnum;
    (void)buf;
    (void)len;
    (void)rtype;
    (void)reqtype;
    return -ENOSYS;
}

static int tuh_ll_output_report(struct hid_device *hdev, __u8 *buf, size_t len)
{
    /*
     * Upstream may send output reports through interrupt OUT or control SET.
     * Current callback-driven slice has no nonblocking output queue yet.
     */
    (void)hdev;
    (void)buf;
    (void)len;
    return -ENOSYS;
}

static int tuh_ll_idle(struct hid_device *hdev, int report, int idle, int reqtype)
{
    /*
     * return hid_set_idle(dev, ifnum, report, idle);
     * SET_IDLE is a synchronous control request; keep it deferred with the
     * rest of the hardware request path.
     */
    (void)hdev;
    (void)report;
    (void)idle;
    (void)reqtype;
    return 0;
}

int tuh_usb_control_msg(struct usb_device *dev, unsigned int pipe,
                        u8 request, u8 requesttype, u16 value, u16 index,
                        void *data, u16 size, int timeout)
{
    /*
     * Synchronous EP0 control is intentionally disabled in the callback-driven
     * parser slice. Any caller reaching this must be converted to async later.
     */
    (void)dev;
    (void)pipe;
    (void)request;
    (void)requesttype;
    (void)value;
    (void)index;
    (void)data;
    (void)size;
    (void)timeout;
    return -ENOSYS;
}

const struct hid_ll_driver tuh_hid_ll_driver = {
    .start = tuh_ll_start,
    .stop = tuh_ll_stop,
    .open = tuh_ll_open,
    .close = tuh_ll_close,
    .parse = tuh_ll_parse,
    .request = tuh_ll_request,
    .wait = tuh_ll_wait,
    .raw_request = tuh_ll_raw_request,
    .output_report = tuh_ll_output_report,
    .idle = tuh_ll_idle,
    .max_buffer_size = HID_MAX_BUFFER_SIZE,
};
