#include "platform/ble_log.h"

#include <stdbool.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/util.h>

#define LOG_NOTIFY_CHUNK 20u
#define ATTR_LOG_TX 2

#define BT_UUID_ERGOTYPE_LOG_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x4552474f, 0x5459, 0x5045, 0x4c4f, 0x470000000001)
#define BT_UUID_ERGOTYPE_LOG_TX_VAL \
    BT_UUID_128_ENCODE(0x4552474f, 0x5459, 0x5045, 0x4c4f, 0x470000000002)

extern const struct bt_gatt_service_static ergotype_log_svc;

static bool notify_enabled;

static void log_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void)attr;
    notify_enabled = value == BT_GATT_CCC_NOTIFY;
}

BT_GATT_SERVICE_DEFINE(ergotype_log_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(BT_UUID_ERGOTYPE_LOG_SERVICE_VAL)),
    BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(BT_UUID_ERGOTYPE_LOG_TX_VAL),
                           BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(log_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int platform_ble_log_init(void)
{
    notify_enabled = false;
    return 0;
}

void platform_ble_log_write(const char *data, size_t len)
{
    if (!notify_enabled)
        return;

    while (len) {
        size_t chunk = MIN(len, LOG_NOTIFY_CHUNK);
        int rc = bt_gatt_notify(NULL, &ergotype_log_svc.attrs[ATTR_LOG_TX], data, chunk);
        if (rc)
            return;
        data += chunk;
        len -= chunk;
    }
}
