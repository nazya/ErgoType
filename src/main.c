#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "jconfig.h"
#include "platform/ble.h"
#include "platform/events.h"
#include "platform/os.h"
#include "platform/storage.h"
#include "platform/vkbd_events.h"
#include "pointing/pointer.h"
#include "keys.h"
#include "ui/ui.h"

LOG_MODULE_REGISTER(ergotype_nrf, LOG_LEVEL_INF);

extern const char *keyd_overlay_conf;

K_THREAD_STACK_DEFINE(keyscan_stack, 2048);
K_THREAD_STACK_DEFINE(keyd_stack, 8192);
K_THREAD_STACK_DEFINE(pointing_stack, 4096);
K_THREAD_STACK_DEFINE(ui_stack, 2048);
K_THREAD_STACK_DEFINE(vkbd_hid_stack, 4096);

void keyscan_task(void *arg);
uint8_t count_pressed_keys(config_t *config, uint8_t *single_code);
void keyd_task(void *arg);
void vkbd_hid_nkro_task(void *arg);

void on_layout_change(const char *name)
{
    ui_notify_layout(name);
}

static void select_profile(uint8_t code)
{
    switch (code) {
    case KEYD_A:
        keyd_overlay_conf = "android.conf";
        break;
    case KEYD_W:
        keyd_overlay_conf = "windows.conf";
        break;
    case KEYD_M:
        keyd_overlay_conf = "macos.conf";
        break;
    case KEYD_D:
        keyd_overlay_conf = NULL;
        break;
    default:
        return;
    }
}

int main(void)
{
    LOG_INF("ErgoTypeNRF boot");

    int rc = platform_storage_mount();
    if (rc) {
        LOG_WRN("storage mount failed: %d", rc);
    }

    static config_t config;
    bool config_loaded = false;
    rc = parse(&config, "config.json");
    if (rc == 0) {
        uint8_t startup_key = 0;
        uint8_t nr_pressed = count_pressed_keys(&config, &startup_key);
        select_profile(startup_key);
        config_loaded = true;

        LOG_INF("config loaded: rows=%u cols=%u encoders=%u pmw3360=%u pmw3389=%u",
                config.matrix.nr_rows,
                config.matrix.nr_cols,
                config.nr_encoders,
                config.nr_pmw3360,
                config.nr_pmw3389);
        LOG_INF("startup keys: count=%u key=%u", nr_pressed, startup_key);
    } else {
        LOG_WRN("config.json not loaded; keyboard runtime is not started yet");
    }

    rc = platform_ble_start();
    if (rc) {
        LOG_ERR("BLE start failed: %d", rc);
        return rc;
    }

    LOG_INF("BLE advertising started");

    if (config_loaded) {
        rc = platform_input_events_init();
        if (rc) {
            LOG_ERR("input event queue init failed: %d", rc);
            return rc;
        }
        rc = platform_vkbd_events_init();
        if (rc) {
            LOG_ERR("vkbd event queue init failed: %d", rc);
            return rc;
        }

        rc = platform_task_start("keyscan", keyscan_task, &config,
                                 keyscan_stack, K_THREAD_STACK_SIZEOF(keyscan_stack),
                                 K_PRIO_PREEMPT(3));
        if (rc) {
            LOG_ERR("keyscan task start failed: %d", rc);
            return rc;
        }
        if (config.nr_leds != 0 || config.ws2812_pin != -1 || config.ssd1306.i2c_idx != -1) {
            rc = platform_task_start("ui", ui_task, &config,
                                     ui_stack, K_THREAD_STACK_SIZEOF(ui_stack),
                                     K_PRIO_PREEMPT(5));
            if (rc) {
                LOG_ERR("ui task start failed: %d", rc);
                return rc;
            }
        }
        if (config.nr_pmw3360 || config.nr_pmw3389) {
            rc = platform_task_start("pointing", pointing_device_task, &config,
                                     pointing_stack, K_THREAD_STACK_SIZEOF(pointing_stack),
                                     K_PRIO_PREEMPT(1));
            if (rc) {
                LOG_ERR("pointing task start failed: %d", rc);
                return rc;
            }
        }
        rc = platform_task_start("vkbd-hid", vkbd_hid_nkro_task, NULL,
                                 vkbd_hid_stack, K_THREAD_STACK_SIZEOF(vkbd_hid_stack),
                                 K_PRIO_PREEMPT(4));
        if (rc) {
            LOG_ERR("vkbd HID task start failed: %d", rc);
            return rc;
        }
        rc = platform_task_start("keyd", keyd_task, NULL,
                                 keyd_stack, K_THREAD_STACK_SIZEOF(keyd_stack),
                                 K_PRIO_PREEMPT(2));
        if (rc) {
            LOG_ERR("keyd task start failed: %d", rc);
            return rc;
        }
    }

    for (;;) {
        k_sleep(K_SECONDS(60));
    }
}
