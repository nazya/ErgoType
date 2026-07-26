#include "platform/hid.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "platform/hid_reports.h"
#include "ui/ui.h"

LOG_MODULE_REGISTER(ergotype_hid, LOG_LEVEL_INF);

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

#define REPORT_RETRY_DELAY K_MSEC(10)
#define REPORT_QUEUE_DEPTH 64

typedef struct {
    uint16_t version;
    uint8_t code;
    uint8_t flags;
} __packed hids_info_t;

typedef struct {
    uint8_t id;
    uint8_t type;
} __packed hids_report_ref_t;

typedef struct {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int16_t wheel;
    int16_t pan;
} __packed mouse_report_t;

typedef enum {
    QUEUED_KEYBOARD,
    QUEUED_MOUSE,
    QUEUED_CONSUMER,
} queued_report_type_t;

typedef struct {
    queued_report_type_t type;
    uint32_t subscription;
    uint32_t sequence;
    union {
        uint8_t keyboard[HID_NKRO_KEY_BYTES];
        mouse_report_t mouse;
        uint16_t consumer[HID_CONSUMER_USAGE_COUNT];
    } data;
} queued_report_t;

static const hids_info_t hids_info = {
    .version = 0x0111,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};

static const hids_report_ref_t keyboard_input_ref = {
    .id = REPORT_ID_KEYBOARD,
    .type = HIDS_INPUT,
};

static const hids_report_ref_t keyboard_output_ref = {
    .id = REPORT_ID_KEYBOARD,
    .type = HIDS_OUTPUT,
};

static const hids_report_ref_t mouse_input_ref = {
    .id = REPORT_ID_MOUSE,
    .type = HIDS_INPUT,
};

static const hids_report_ref_t consumer_input_ref = {
    .id = REPORT_ID_CONSUMER,
    .type = HIDS_INPUT,
};

static const uint8_t report_map[] = {
    0x05, 0x01,
    0x09, 0x06,
    0xA1, 0x01,
    0x85, REPORT_ID_KEYBOARD,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x05, 0x08,
    0x19, 0x01,
    0x29, 0x05,
    0x91, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x91, 0x03,
    0x05, 0x07,
    0x19, 0x00,
    0x2A, 0xFF, 0x00,
    0x75, 0x01,
    0x96, 0x00, 0x01,
    0x81, 0x02,
    0xC0,

    0x05, 0x0C,
    0x09, 0x01,
    0xA1, 0x01,
    0x85, REPORT_ID_CONSUMER,
    0x15, 0x00,
    0x26, 0xFF, 0x03,
    0x19, 0x00,
    0x2A, 0xFF, 0x03,
    0x75, 0x10,
    0x95, HID_CONSUMER_USAGE_COUNT,
    0x81, 0x00,
    0xC0,

    0x05, 0x01,
    0x09, 0x02,
    0xA1, 0x01,
    0x85, REPORT_ID_MOUSE,
    0x09, 0x01,
    0xA1, 0x00,
    0x05, 0x09,
    0x19, 0x01,
    0x29, 0x05,
    0x15, 0x00,
    0x25, 0x01,
    0x95, 0x05,
    0x75, 0x01,
    0x81, 0x02,
    0x95, 0x01,
    0x75, 0x03,
    0x81, 0x03,
    0x05, 0x01,
    0x09, 0x30,
    0x09, 0x31,
    0x16, 0x00, 0x80,
    0x26, 0xFF, 0x7F,
    0x95, 0x02,
    0x75, 0x10,
    0x81, 0x06,
    0x09, 0x38,
    0x16, 0x00, 0x80,
    0x26, 0xFF, 0x7F,
    0x95, 0x01,
    0x75, 0x10,
    0x81, 0x06,
    0x05, 0x0C,
    0x0A, 0x38, 0x02,
    0x16, 0x00, 0x80,
    0x26, 0xFF, 0x7F,
    0x95, 0x01,
    0x75, 0x10,
    0x81, 0x06,
    0xC0,
    0xC0,

};

static uint8_t keyboard_report[HID_NKRO_KEY_BYTES];
static mouse_report_t mouse_report;
static uint16_t consumer_report[HID_CONSUMER_USAGE_COUNT];
static uint8_t keyboard_leds;
static uint8_t ctrl_point;
static bool keyboard_notify_enabled;
static bool mouse_notify_enabled;
static bool consumer_notify_enabled;
static uint32_t keyboard_subscription;
static uint32_t mouse_subscription;
static uint32_t consumer_subscription;
static queued_report_t report_queue[REPORT_QUEUE_DEPTH];
static size_t report_queue_head;
static size_t report_queue_count;
static uint32_t report_sequence;
static struct k_spinlock report_lock;
K_SEM_DEFINE(report_queue_slots, REPORT_QUEUE_DEPTH, REPORT_QUEUE_DEPTH);

BUILD_ASSERT(sizeof(keyboard_report) == 32);
BUILD_ASSERT(sizeof(consumer_report) == HID_CONSUMER_REPORT_BYTES);
BUILD_ASSERT(sizeof(mouse_report) == HID_MOUSE_REPORT_BYTES);

static void report_work_handler(struct k_work *work);
static void purge_report_type(queued_report_type_t type);
K_WORK_DELAYABLE_DEFINE(report_work, report_work_handler);

enum {
    ATTR_KEYBOARD_INPUT = 6,
    ATTR_KEYBOARD_OUTPUT = 10,
    ATTR_MOUSE_INPUT = 13,
    ATTR_CONSUMER_INPUT = 17,
};

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &hids_info, sizeof(hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(hids_report_ref_t));
}

static ssize_t read_keyboard_input(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset)
{
    uint8_t snapshot[sizeof(keyboard_report)];
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memcpy(snapshot, keyboard_report, sizeof(snapshot));
    k_spin_unlock(&report_lock, key);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot, sizeof(snapshot));
}

static ssize_t read_keyboard_output(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset)
{
    uint8_t snapshot;
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    snapshot = keyboard_leds;
    k_spin_unlock(&report_lock, key);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot, sizeof(snapshot));
}

static ssize_t write_keyboard_output(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                     const void *buf, uint16_t len, uint16_t offset,
                                     uint8_t flags)
{
    (void)conn;
    (void)attr;
    (void)flags;

    if (offset + len > sizeof(keyboard_leds))
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);

    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memcpy(&keyboard_leds + offset, buf, len);
    uint8_t leds = keyboard_leds;
    k_spin_unlock(&report_lock, key);
    ui_led_set_pattern(0, (leds & 0x02u) ? 0xFFFFFFFFu : 0u, true);
    return len;
}

static ssize_t read_mouse_input(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    mouse_report_t snapshot;
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    snapshot = mouse_report;
    k_spin_unlock(&report_lock, key);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot, sizeof(snapshot));
}

static ssize_t read_consumer_input(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset)
{
    uint16_t snapshot[ARRAY_SIZE(consumer_report)];
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memcpy(snapshot, consumer_report, sizeof(snapshot));
    k_spin_unlock(&report_lock, key);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, snapshot, sizeof(snapshot));
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags)
{
    (void)conn;
    (void)attr;
    (void)flags;

    if (offset + len > sizeof(ctrl_point))
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);

    memcpy(&ctrl_point + offset, buf, len);
    return len;
}

static void keyboard_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void)attr;
    bool enabled = value == BT_GATT_CCC_NOTIFY;
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    keyboard_notify_enabled = enabled;
    ++keyboard_subscription;
    k_spin_unlock(&report_lock, key);
    purge_report_type(QUEUED_KEYBOARD);
}

static void mouse_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void)attr;
    bool enabled = value == BT_GATT_CCC_NOTIFY;
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    mouse_notify_enabled = enabled;
    ++mouse_subscription;
    mouse_report.x = 0;
    mouse_report.y = 0;
    mouse_report.wheel = 0;
    mouse_report.pan = 0;
    k_spin_unlock(&report_lock, key);
    purge_report_type(QUEUED_MOUSE);
}

static void consumer_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void)attr;
    bool enabled = value == BT_GATT_CCC_NOTIFY;
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    consumer_notify_enabled = enabled;
    ++consumer_subscription;
    k_spin_unlock(&report_lock, key);
    purge_report_type(QUEUED_CONSUMER);
}

BT_GATT_SERVICE_DEFINE(ergotype_hids_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ, read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_keyboard_input, NULL, NULL),
    BT_GATT_CCC(keyboard_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                       read_report_ref, NULL, (void *)&keyboard_input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                           read_keyboard_output, write_keyboard_output, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                       read_report_ref, NULL, (void *)&keyboard_output_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_mouse_input, NULL, NULL),
    BT_GATT_CCC(mouse_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                       read_report_ref, NULL, (void *)&mouse_input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_consumer_input, NULL, NULL),
    BT_GATT_CCC(consumer_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                       read_report_ref, NULL, (void *)&consumer_input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, write_ctrl_point, &ctrl_point),
);

static bool temporary_notify_error(int rc)
{
    return rc == -ENOMEM || rc == -ENOBUFS || rc == -EAGAIN || rc == -EBUSY;
}

static bool report_enabled_locked(queued_report_type_t type, uint32_t subscription)
{
    switch (type) {
    case QUEUED_KEYBOARD:
        return keyboard_notify_enabled && subscription == keyboard_subscription;
    case QUEUED_MOUSE:
        return mouse_notify_enabled && subscription == mouse_subscription;
    case QUEUED_CONSUMER:
        return consumer_notify_enabled && subscription == consumer_subscription;
    }
    return false;
}

static uint32_t report_subscription_locked(queued_report_type_t type)
{
    switch (type) {
    case QUEUED_KEYBOARD:
        return keyboard_subscription;
    case QUEUED_MOUSE:
        return mouse_subscription;
    case QUEUED_CONSUMER:
        return consumer_subscription;
    }
    return 0;
}

static void drop_report_head_locked(void)
{
    report_queue_head = (report_queue_head + 1u) % REPORT_QUEUE_DEPTH;
    --report_queue_count;
}

static void purge_report_type(queued_report_type_t type)
{
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    size_t original_count = report_queue_count;
    size_t kept = 0;

    for (size_t i = 0; i < original_count; ++i) {
        size_t source = (report_queue_head + i) % REPORT_QUEUE_DEPTH;
        if (report_queue[source].type == type)
            continue;
        size_t destination = (report_queue_head + kept) % REPORT_QUEUE_DEPTH;
        report_queue[destination] = report_queue[source];
        ++kept;
    }
    report_queue_count = kept;
    k_spin_unlock(&report_lock, key);

    size_t removed = original_count - kept;
    while (removed--)
        k_sem_give(&report_queue_slots);
}

static void purge_report_queue(void)
{
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    size_t removed = report_queue_count;
    report_queue_head = 0;
    report_queue_count = 0;
    k_spin_unlock(&report_lock, key);

    while (removed--)
        k_sem_give(&report_queue_slots);
}

static int enqueue_report(queued_report_t *report)
{
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    report->subscription = report_subscription_locked(report->type);
    bool enabled = report_enabled_locked(report->type, report->subscription);
    k_spin_unlock(&report_lock, key);
    if (!enabled)
        return 0;

    int rc = k_sem_take(&report_queue_slots, K_FOREVER);
    if (rc)
        return rc;

    key = k_spin_lock(&report_lock);
    if (!report_enabled_locked(report->type, report->subscription)) {
        k_spin_unlock(&report_lock, key);
        k_sem_give(&report_queue_slots);
        return 0;
    }

    report->sequence = ++report_sequence;
    size_t tail = (report_queue_head + report_queue_count) % REPORT_QUEUE_DEPTH;
    report_queue[tail] = *report;
    ++report_queue_count;
    k_spin_unlock(&report_lock, key);

    k_work_schedule(&report_work, K_NO_WAIT);
    return 0;
}

static void report_work_handler(struct k_work *work)
{
    (void)work;
    for (;;) {
        k_spinlock_key_t key = k_spin_lock(&report_lock);
        if (!report_queue_count) {
            k_spin_unlock(&report_lock, key);
            return;
        }

        queued_report_t report = report_queue[report_queue_head];
        if (!report_enabled_locked(report.type, report.subscription)) {
            drop_report_head_locked();
            k_spin_unlock(&report_lock, key);
            k_sem_give(&report_queue_slots);
            continue;
        }
        k_spin_unlock(&report_lock, key);

        const struct bt_gatt_attr *attr;
        const void *data;
        size_t size;
        switch (report.type) {
        case QUEUED_KEYBOARD:
            attr = &ergotype_hids_svc.attrs[ATTR_KEYBOARD_INPUT];
            data = report.data.keyboard;
            size = sizeof(report.data.keyboard);
            break;
        case QUEUED_MOUSE:
            attr = &ergotype_hids_svc.attrs[ATTR_MOUSE_INPUT];
            data = &report.data.mouse;
            size = sizeof(report.data.mouse);
            break;
        case QUEUED_CONSUMER:
            attr = &ergotype_hids_svc.attrs[ATTR_CONSUMER_INPUT];
            data = report.data.consumer;
            size = sizeof(report.data.consumer);
            break;
        default:
            return;
        }

        int rc = bt_gatt_notify(NULL, attr, data, size);
        bool retry = false;
        bool removed = false;

        key = k_spin_lock(&report_lock);
        if (report_queue_count && report_queue[report_queue_head].sequence == report.sequence) {
            retry = temporary_notify_error(rc) &&
                    report_enabled_locked(report.type, report.subscription);
            if (!retry) {
                drop_report_head_locked();
                removed = true;
            }
        }
        k_spin_unlock(&report_lock, key);

        if (removed)
            k_sem_give(&report_queue_slots);
        if (rc && !temporary_notify_error(rc) && rc != -ENOTCONN)
            LOG_WRN("HID notification failed: %d", rc);
        if (retry) {
            k_work_schedule(&report_work, REPORT_RETRY_DELAY);
            return;
        }
    }
}

int platform_hid_init(void)
{
    purge_report_queue();
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memset(keyboard_report, 0, sizeof(keyboard_report));
    memset(&mouse_report, 0, sizeof(mouse_report));
    memset(consumer_report, 0, sizeof(consumer_report));
    keyboard_leds = 0;
    ctrl_point = 0;
    keyboard_notify_enabled = false;
    mouse_notify_enabled = false;
    consumer_notify_enabled = false;
    keyboard_subscription = 1;
    mouse_subscription = 1;
    consumer_subscription = 1;
    report_sequence = 0;
    k_spin_unlock(&report_lock, key);
    return 0;
}

void platform_hid_disconnected(void)
{
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    keyboard_notify_enabled = false;
    mouse_notify_enabled = false;
    consumer_notify_enabled = false;
    ++keyboard_subscription;
    ++mouse_subscription;
    ++consumer_subscription;
    mouse_report.x = 0;
    mouse_report.y = 0;
    mouse_report.wheel = 0;
    mouse_report.pan = 0;
    k_spin_unlock(&report_lock, key);
    (void)k_work_cancel_delayable(&report_work);
    purge_report_queue();
}

int platform_hid_nkro_report(const uint8_t *keys, size_t key_count)
{
    queued_report_t report = {
        .type = QUEUED_KEYBOARD,
    };

    size_t n = MIN(key_count, sizeof(report.data.keyboard));
    if (keys && n)
        memcpy(report.data.keyboard, keys, n);

    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memcpy(keyboard_report, report.data.keyboard, sizeof(keyboard_report));
    k_spin_unlock(&report_lock, key);
    return enqueue_report(&report);
}

int platform_hid_mouse_report(uint8_t buttons, int16_t x, int16_t y, int16_t wheel, int16_t pan)
{
    queued_report_t report = {
        .type = QUEUED_MOUSE,
        .data.mouse = {
            .buttons = buttons,
            .x = x,
            .y = y,
            .wheel = wheel,
            .pan = pan,
        },
    };
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    mouse_report = report.data.mouse;
    k_spin_unlock(&report_lock, key);
    return enqueue_report(&report);
}

int platform_hid_consumer_report(const uint16_t *usages, size_t usage_count)
{
    queued_report_t report = {
        .type = QUEUED_CONSUMER,
    };

    size_t n = MIN(usage_count, ARRAY_SIZE(report.data.consumer));
    for (size_t i = 0; usages && i < n; ++i)
        report.data.consumer[i] = sys_cpu_to_le16(usages[i]);

    k_spinlock_key_t key = k_spin_lock(&report_lock);
    memcpy(consumer_report, report.data.consumer, sizeof(consumer_report));
    k_spin_unlock(&report_lock, key);
    return enqueue_report(&report);
}

int platform_hid_release_all(void)
{
    static const uint8_t no_keys[HID_NKRO_KEY_BYTES] = {0};
    static const uint16_t no_consumer[HID_CONSUMER_USAGE_COUNT] = {0};
    int rc = platform_hid_nkro_report(no_keys, ARRAY_SIZE(no_keys));
    int mouse_rc = platform_hid_mouse_report(0, 0, 0, 0, 0);
    int consumer_rc = platform_hid_consumer_report(no_consumer, ARRAY_SIZE(no_consumer));

    if (!rc)
        rc = mouse_rc;
    if (!rc)
        rc = consumer_rc;

    return rc;
}

uint8_t platform_hid_keyboard_leds(void)
{
    k_spinlock_key_t key = k_spin_lock(&report_lock);
    uint8_t leds = keyboard_leds;
    k_spin_unlock(&report_lock, key);
    return leds;
}
