#include <stdbool.h>
#include <stdint.h>

#include "pio_usb.h"
#include "tusb.h"

#include "log.h"

#include "FreeRTOS.h"
#include "task.h"

enum hid_host_event_type {
    HID_HOST_EVENT_MOUNT,
    HID_HOST_EVENT_UMOUNT,
    HID_HOST_EVENT_REPORT,
};

struct hid_host_event {
    enum hid_host_event_type type;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t proto;
    uint8_t first[4];
    uint16_t vid;
    uint16_t pid;
    uint16_t desc_len;
    uint16_t len;
    bool receive_ok;
};

#define HID_HOST_EVENT_CAP 16

static struct hid_host_event hid_host_events[HID_HOST_EVENT_CAP];
static uint8_t hid_host_event_head;
static uint8_t hid_host_event_tail;
static uint32_t hid_host_event_dropped;

static void hid_host_push_event(struct hid_host_event const *event)
{
    uint8_t next = (hid_host_event_head + 1) % HID_HOST_EVENT_CAP;

    if (next == hid_host_event_tail) {
        hid_host_event_dropped++;
        return;
    }

    hid_host_events[hid_host_event_head] = *event;
    hid_host_event_head = next;
}

static void hid_host_log_events(void)
{
    while (hid_host_event_tail != hid_host_event_head) {
        struct hid_host_event event = hid_host_events[hid_host_event_tail];
        hid_host_event_tail = (hid_host_event_tail + 1) % HID_HOST_EVENT_CAP;

        switch (event.type) {
        case HID_HOST_EVENT_MOUNT:
            dbg("tuh hid mount dev=%u inst=%u vid=%04x pid=%04x proto=%u desc_len=%u",
                event.dev_addr, event.instance, event.vid, event.pid, event.proto, event.desc_len);
            if (!event.receive_ok)
                err("tuh hid receive start failed dev=%u inst=%u", event.dev_addr, event.instance);
            break;
        case HID_HOST_EVENT_UMOUNT:
            dbg("tuh hid umount dev=%u inst=%u", event.dev_addr, event.instance);
            break;
        case HID_HOST_EVENT_REPORT:
            dbg2("tuh hid report dev=%u inst=%u len=%u first=%02x %02x %02x %02x",
                 event.dev_addr, event.instance, event.len,
                 event.first[0], event.first[1], event.first[2], event.first[3]);
            if (!event.receive_ok)
                err("tuh hid receive rearm failed dev=%u inst=%u", event.dev_addr, event.instance);
            break;
        }
    }

    if (hid_host_event_dropped) {
        uint32_t dropped = hid_host_event_dropped;
        hid_host_event_dropped = 0;
        err("tuh hid dropped %lu debug events", (unsigned long)dropped);
    }
}

void tusb_host_task(void *pvParameters)
{
    (void)pvParameters;

    tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;

    pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
    (void)tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    (void)tusb_init(BOARD_TUH_RHPORT, &host_init);

    while (1) {
        tuh_task();
        hid_host_log_events();
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;

    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    bool receive_ok = tuh_hid_receive_report(dev_addr, instance);

    struct hid_host_event event = {
        .type = HID_HOST_EVENT_MOUNT,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .receive_ok = receive_ok,
    };
    hid_host_push_event(&event);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    struct hid_host_event event = {
        .type = HID_HOST_EVENT_UMOUNT,
        .dev_addr = dev_addr,
        .instance = instance,
    };
    hid_host_push_event(&event);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    struct hid_host_event event = {
        .type = HID_HOST_EVENT_REPORT,
        .dev_addr = dev_addr,
        .instance = instance,
        .len = len,
        .first = {
            len > 0 ? report[0] : 0,
            len > 1 ? report[1] : 0,
            len > 2 ? report[2] : 0,
            len > 3 ? report[3] : 0,
        },
        .receive_ok = tuh_hid_receive_report(dev_addr, instance),
    };
    hid_host_push_event(&event);
}
