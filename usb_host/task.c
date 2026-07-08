#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Upstream Linux: no equivalent. This file owns the TinyUSB host task,
 * TinyUSB HID callbacks, and the local lookup/lifetime glue that feeds the
 * Linux-shaped HID core from firmware callback context.
 */

#include "pio_usb.h"
#include "tusb.h"

#include "log.h"
#include "evdev.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "linux/include/linux/hid.h"
#include "linux/include/linux/usb.h"

int hid_core_init(void);
int hid_builtin_drivers_init(void);

extern const struct hid_ll_driver tuh_hid_ll_driver;

#define HID_HOST_MAX_DEVICES CFG_TUH_HID
#define HID_HOST_EVENT_CAP 64
#define HID_HOST_LOG_STACK_SIZE 1024
#define HID_HOST_LOG_PRIORITY (configMAX_PRIORITIES - 3)
#define HID_HOST_LOG_CORE ((UBaseType_t)(1u << 0))

enum hid_host_event_type {
    HID_HOST_EVENT_TASK_START,
    HID_HOST_EVENT_INIT,
    HID_HOST_EVENT_CORE_INIT,
    HID_HOST_EVENT_EVDEV_INIT,
    HID_HOST_EVENT_DRIVER_INIT,
    HID_HOST_EVENT_TUSB_CONFIGURE,
    HID_HOST_EVENT_TUSB_INIT,
    HID_HOST_EVENT_MOUNT_STAGE,
    HID_HOST_EVENT_MOUNT,
    HID_HOST_EVENT_ADD_OK,
    HID_HOST_EVENT_ADD_FAIL,
    HID_HOST_EVENT_UMOUNT,
    HID_HOST_EVENT_REPORT,
    HID_HOST_EVENT_REPORT_SKIP,
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
    int result;
    bool receive_ok;
};

static QueueHandle_t hid_host_event_queue;
static uint32_t hid_host_event_dropped;

static struct hid_device *hid_host_devices[HID_HOST_MAX_DEVICES];

static void hid_host_push_event(struct hid_host_event const *event)
{
    if (!hid_host_event_queue || xQueueSend(hid_host_event_queue, event, 0) != pdPASS)
        hid_host_event_dropped++;
}

static struct hid_device *hid_host_lookup(uint8_t dev_addr, uint8_t instance)
{
    for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
        struct hid_device *hid = hid_host_devices[i];

        if (hid && hid->dev_addr == dev_addr && hid->instance == instance)
            return hid;
    }

    return NULL;
}

static int hid_host_insert(struct hid_device *hid)
{
    for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
        if (!hid_host_devices[i]) {
            hid_host_devices[i] = hid;
            return 0;
        }
    }

    return -ENOMEM;
}

static void hid_host_remove_slot(struct hid_device *hid)
{
    for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
        if (hid_host_devices[i] == hid) {
            hid_host_devices[i] = NULL;
            return;
        }
    }
}

struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev, unsigned int ifnum)
{
    for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
        struct hid_device *hid = hid_host_devices[i];

        if (hid && hid->usb_dev.dev_addr == dev->dev_addr &&
            hid->usb_intf.cur_altsetting &&
            hid->usb_intf.cur_altsetting->desc.bInterfaceNumber == ifnum)
            return &hid->usb_intf;
    }

    return NULL;
}

static void hid_host_log_event(struct hid_host_event const *event)
{
    switch (event->type) {
    case HID_HOST_EVENT_TASK_START:
        dbg("hid host task start");
        break;
    case HID_HOST_EVENT_INIT:
        if (event->result)
            err("hid linux init failed ret=%d", event->result);
        else
            dbg("hid linux init ok");
        break;
    case HID_HOST_EVENT_CORE_INIT:
        if (event->result)
            err("hid core init failed ret=%d", event->result);
        else
            dbg("hid core init ok");
        break;
    case HID_HOST_EVENT_EVDEV_INIT:
        if (event->result)
            err("hid evdev init failed ret=%d", event->result);
        else
            dbg("hid evdev init ok");
        break;
    case HID_HOST_EVENT_DRIVER_INIT:
        if (event->result)
            err("hid drivers init failed ret=%d", event->result);
        else
            dbg("hid drivers init ok");
        break;
    case HID_HOST_EVENT_TUSB_CONFIGURE:
        if (event->result)
            err("tuh configure failed ret=%d", event->result);
        else
            dbg("tuh configure ok");
        break;
    case HID_HOST_EVENT_TUSB_INIT:
        if (event->result)
            err("tusb host init failed ret=%d", event->result);
        else
            dbg("tusb host init ok");
        break;
    case HID_HOST_EVENT_MOUNT_STAGE:
        dbg("tuh hid mount stage=%d dev=%u inst=%u vid=%04x pid=%04x proto=%u desc_len=%u",
            event->result, event->dev_addr, event->instance, event->vid, event->pid,
            event->proto, event->desc_len);
        break;
    case HID_HOST_EVENT_MOUNT:
        dbg("tuh hid mount dev=%u inst=%u vid=%04x pid=%04x proto=%u desc_len=%u",
            event->dev_addr, event->instance, event->vid, event->pid, event->proto, event->desc_len);
        if (!event->receive_ok)
            err("tuh hid receive start failed dev=%u inst=%u", event->dev_addr, event->instance);
        break;
    case HID_HOST_EVENT_ADD_OK:
        dbg("hid add ok dev=%u inst=%u", event->dev_addr, event->instance);
        break;
    case HID_HOST_EVENT_ADD_FAIL:
        err("hid add failed dev=%u inst=%u ret=%d",
            event->dev_addr, event->instance, event->result);
        break;
    case HID_HOST_EVENT_UMOUNT:
        dbg("tuh hid umount dev=%u inst=%u", event->dev_addr, event->instance);
        break;
    case HID_HOST_EVENT_REPORT:
        if (!event->receive_ok)
            err("tuh hid receive rearm failed dev=%u inst=%u", event->dev_addr, event->instance);
        break;
    case HID_HOST_EVENT_REPORT_SKIP:
        err("tuh hid report skipped dev=%u inst=%u len=%u ret=%d",
            event->dev_addr, event->instance, event->len, event->result);
        if (!event->receive_ok)
            err("tuh hid receive rearm failed dev=%u inst=%u", event->dev_addr, event->instance);
        break;
    }

    if (hid_host_event_dropped) {
        uint32_t dropped = hid_host_event_dropped;
        hid_host_event_dropped = 0;
        err("tuh hid dropped %lu debug events", (unsigned long)dropped);
    }
}

static void hid_host_log_task(void *pvParameters)
{
    struct hid_host_event event;

    (void)pvParameters;

    while (1) {
        if (xQueueReceive(hid_host_event_queue, &event, portMAX_DELAY) == pdTRUE)
            hid_host_log_event(&event);
    }
}

static void hid_host_start_log_task(void)
{
    BaseType_t ret;

    hid_host_event_queue = xQueueCreate(HID_HOST_EVENT_CAP, sizeof(struct hid_host_event));
    if (!hid_host_event_queue)
        return;

    ret = xTaskCreateAffinitySet(hid_host_log_task, NULL, HID_HOST_LOG_STACK_SIZE, NULL,
                                 HID_HOST_LOG_PRIORITY, HID_HOST_LOG_CORE, NULL);
    if (ret != pdPASS) {
        vQueueDelete(hid_host_event_queue);
        hid_host_event_queue = NULL;
    }
}

void tusb_host_task(void *pvParameters)
{
    int ret;

    (void)pvParameters;

    tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;

    hid_host_start_log_task();

    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_TASK_START,
    });

    ret = hid_core_init();
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_CORE_INIT,
        .result = ret,
    });
    if (!ret) {
        ret = evdev_init();
        hid_host_push_event(&(struct hid_host_event){
            .type = HID_HOST_EVENT_EVDEV_INIT,
            .result = ret,
        });
    }
    if (!ret) {
        ret = hid_builtin_drivers_init();
        hid_host_push_event(&(struct hid_host_event){
            .type = HID_HOST_EVENT_DRIVER_INIT,
            .result = ret,
        });
    }
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_INIT,
        .result = ret,
    });
    if (ret)
        while (1)
            vTaskDelay(portMAX_DELAY);

    pio_cfg.pin_dp = PICO_DEFAULT_PIO_USB_DP_PIN;
    pio_cfg.pinout = PIO_USB_PINOUT_DMDP;
    ret = tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg) ? 0 : -EIO;
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_TUSB_CONFIGURE,
        .result = ret,
    });
    if (ret)
        while (1)
            vTaskDelay(portMAX_DELAY);

    tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
    ret = tusb_init(BOARD_TUH_RHPORT, &host_init) ? 0 : -EIO;
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_TUSB_INIT,
        .result = ret,
    });
    if (ret)
        while (1)
            vTaskDelay(portMAX_DELAY);

    while (1) {
        tuh_task();
    }
}

static int hid_host_fill_device(struct hid_device *hid, uint8_t dev_addr, uint8_t instance,
                                uint8_t const *desc_report, uint16_t desc_len)
{
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    tuh_itf_info_t itf_info;

    memset(&itf_info, 0, sizeof(itf_info));
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    tuh_hid_itf_get_info(dev_addr, instance, &itf_info);

    device_initialize(&hid->usb_dev.dev);
    device_initialize(&hid->usb_intf.dev);

    hid->dev_addr = dev_addr;
    hid->instance = instance;
    hid->usb_dev.dev_addr = dev_addr;
    hid->usb_dev.descriptor.idVendor = vid;
    hid->usb_dev.descriptor.idProduct = pid;

    hid->usb_altsetting.desc.bInterfaceNumber = itf_info.desc.bInterfaceNumber;
    hid->usb_altsetting.desc.bInterfaceSubClass = itf_info.desc.bInterfaceSubClass;
    hid->usb_altsetting.desc.bInterfaceProtocol = itf_info.desc.bInterfaceProtocol;
    hid->usb_altsetting.desc.bNumEndpoints = itf_info.desc.bNumEndpoints;
    hid->usb_intf.altsetting = &hid->usb_altsetting;
    hid->usb_intf.cur_altsetting = &hid->usb_altsetting;
    hid->usb_intf.dev.parent = &hid->usb_dev.dev;
    hid->dev.parent = &hid->usb_intf.dev;
    usb_set_intfdata(&hid->usb_intf, hid);

    hid->ll_driver = &tuh_hid_ll_driver;
    hid->ll_rdesc = desc_report;
    hid->ll_rsize = desc_len;
    init_waitqueue_head(&hid->ll_wait);

    hid->bus = BUS_USB;
    hid->vendor = vid;
    hid->product = pid;
    hid->version = 0;
    if (proto == HID_ITF_PROTOCOL_MOUSE)
        hid->type = HID_TYPE_USBMOUSE;
    else if (proto == HID_ITF_PROTOCOL_NONE)
        hid->type = HID_TYPE_USBNONE;

    snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x", hid->vendor, hid->product);
    usb_make_path(&hid->usb_dev, hid->phys, sizeof(hid->phys));
    strlcat(hid->phys, "/input", sizeof(hid->phys));

    return 0;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
    struct hid_device *hid;
    uint8_t *rdesc;
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
    bool receive_ok = false;
    int ret;

    tuh_vid_pid_get(dev_addr, &vid, &pid);
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 0,
    });

    if (!desc_report || !desc_len) {
        hid_host_push_event(&(struct hid_host_event){
            .type = HID_HOST_EVENT_ADD_FAIL,
            .dev_addr = dev_addr,
            .instance = instance,
            .result = -EINVAL,
        });
        return;
    }

    hid = hid_allocate_device();
    if (IS_ERR(hid)) {
        hid_host_push_event(&(struct hid_host_event){
            .type = HID_HOST_EVENT_ADD_FAIL,
            .dev_addr = dev_addr,
            .instance = instance,
            .result = PTR_ERR(hid),
        });
        return;
    }
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 1,
    });

    rdesc = kmemdup(desc_report, desc_len, GFP_KERNEL);
    if (!rdesc) {
        hid_destroy_device(hid);
        hid_host_push_event(&(struct hid_host_event){
            .type = HID_HOST_EVENT_ADD_FAIL,
            .dev_addr = dev_addr,
            .instance = instance,
            .result = -ENOMEM,
        });
        return;
    }
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 2,
    });

    ret = hid_host_fill_device(hid, dev_addr, instance, rdesc, desc_len);
    if (ret < 0)
        goto fail;
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 3,
    });

    ret = hid_host_insert(hid);
    if (ret < 0)
        goto fail;
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 4,
    });

    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 5,
    });
    ret = hid_add_device(hid);
    if (ret < 0) {
        hid_host_remove_slot(hid);
        goto fail;
    }
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT_STAGE,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .result = 6,
    });

    receive_ok = tuh_hid_receive_report(dev_addr, instance);
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_MOUNT,
        .dev_addr = dev_addr,
        .instance = instance,
        .proto = proto,
        .vid = vid,
        .pid = pid,
        .desc_len = desc_len,
        .receive_ok = receive_ok,
    });
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_ADD_OK,
        .dev_addr = dev_addr,
        .instance = instance,
    });
    hid->ll_rdesc = NULL;
    hid->ll_rsize = 0;
    kfree(rdesc);
    return;

fail:
    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_ADD_FAIL,
        .dev_addr = dev_addr,
        .instance = instance,
        .result = ret,
    });
    kfree(rdesc);
    hid_destroy_device(hid);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    struct hid_device *hid = hid_host_lookup(dev_addr, instance);

    if (hid) {
        hid_host_remove_slot(hid);
        hid_destroy_device(hid);
    }

    hid_host_push_event(&(struct hid_host_event){
        .type = HID_HOST_EVENT_UMOUNT,
        .dev_addr = dev_addr,
        .instance = instance,
    });
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
    struct hid_device *hid = hid_host_lookup(dev_addr, instance);
    uint8_t report_buf[CFG_TUH_HID_EPIN_BUFSIZE];
    bool receive_ok;
    int ret = -ENODEV;

    if (hid) {
        // ret = hid_safe_input_report(hid, HID_INPUT_REPORT, (u8 *)report,
        //                             CFG_TUH_HID_EPIN_BUFSIZE, len, 1);
        /*
         * hid_safe_input_report() may zero-pad short reports in-place. TinyUSB
         * gives us a const callback buffer, so give the Linux parser a writable
         * transport-sized copy without allocating in the callback.
         */
        memset(report_buf, 0, sizeof(report_buf));
        memcpy(report_buf, report, len);
        ret = hid_safe_input_report(hid, HID_INPUT_REPORT, report_buf,
                                    sizeof(report_buf), len, 1);
    }

    receive_ok = tuh_hid_receive_report(dev_addr, instance);

    hid_host_push_event(&(struct hid_host_event){
        .type = ret < 0 ? HID_HOST_EVENT_REPORT_SKIP : HID_HOST_EVENT_REPORT,
        .dev_addr = dev_addr,
        .instance = instance,
        .len = len,
        .first = {
            len > 0 ? report[0] : 0,
            len > 1 ? report[1] : 0,
            len > 2 ? report[2] : 0,
            len > 3 ? report[3] : 0,
        },
        .result = ret,
        .receive_ok = receive_ok,
    });
}
