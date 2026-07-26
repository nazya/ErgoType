#include "platform/ble.h"
#include "platform/ble_config.h"
#include "platform/ble_log.h"
#include "platform/hid.h"

#include <stddef.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ergotype_ble, LOG_LEVEL_INF);

static const struct bt_data advertising_data[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL)),
};

static const struct bt_data scan_response_data[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static int start_advertising(void)
{
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                           advertising_data,
                           ARRAY_SIZE(advertising_data),
                           scan_response_data,
                           ARRAY_SIZE(scan_response_data));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_WRN("BLE connect failed: %u", err);
        int rc = start_advertising();
        if (rc)
            LOG_ERR("BLE advertising restart after failed connection failed: %d", rc);
        return;
    }

    int rc = bt_conn_set_security(conn, BT_SECURITY_L2);
    if (rc)
        LOG_WRN("BLE security request failed: %d", rc);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    (void)conn;
    LOG_INF("BLE disconnected: %u", reason);
    platform_hid_disconnected();
    platform_ble_config_disconnected();

    int rc = start_advertising();
    if (rc)
        LOG_ERR("BLE advertising restart after disconnect failed: %d", rc);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int platform_ble_start(void)
{
    int rc = bt_enable(NULL);
    if (rc)
        return rc;

    rc = platform_hid_init();
    if (rc)
        return rc;

    rc = platform_ble_config_init();
    if (rc)
        return rc;

    rc = platform_ble_log_init();
    if (rc)
        return rc;

    if (IS_ENABLED(CONFIG_SETTINGS))
        settings_load();

    rc = start_advertising();
    if (rc)
        return rc;

    return 0;
}
