#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "accel/filter.h"
#include "device.h"
#include "jconfig.h"
#include "log.h"
#include "platform/events.h"
#include "platform/gpio.h"
#include "platform/spi.h"
#include "platform/time.h"
#include "pointing/pointer.h"
#include "pmw3360.h"
#include "pmw3389.h"

static struct k_poll_signal motion_signal;
static atomic_t motion_bits;

static int32_t x125pct(int32_t value)
{
    int32_t magnitude = value < 0 ? -value : value;
    // Add 25%: shifting the magnitude right by 2 divides it by 4.
    magnitude += magnitude >> 2;

    return value < 0 ? -magnitude : magnitude;
}

static int32_t scaled_to_q10(int32_t value, int32_t scale)
{
    int32_t whole = value / scale;
    int32_t rem = value % scale;
    int32_t q10 = whole * Q10_ONE;

    if (rem < 0)
        q10 -= ((-rem * Q10_ONE) + scale / 2) / scale;
    else
        q10 += (rem * Q10_ONE + scale / 2) / scale;

    return q10;
}

static void accel_prepare(accel_profile_cfg_t *accel)
{
    switch (accel->profile) {
    case ACCEL_PROFILE_NONE:
        return;
    case ACCEL_PROFILE_FLAT:
    case ACCEL_PROFILE_ADAPTIVE:
        accel->speed = scaled_to_q10(accel->speed, accel->scale);
        return;
    case ACCEL_PROFILE_CUSTOM:
        return;
    }
}

static void motion_irq(platform_gpio_t pin, void *user)
{
    (void)pin;

    uint32_t bits = (uint32_t)(uintptr_t)user;
    atomic_or(&motion_bits, (atomic_val_t)bits);
    k_poll_signal_raise(&motion_signal, 1);
}

void pointing_motion_irq_init(void *task_handle, int8_t mot_pin, uint8_t sensor_idx)
{
    (void)task_handle;

    uint32_t bit = 1u << sensor_idx;
    platform_gpio_irq_falling(mot_pin, motion_irq, (void *)(uintptr_t)bit);
}

static void spi_bus_init_once(uint8_t bus, const config_t *config)
{
    platform_spi_init(bus,
                      config->spi[bus].sck,
                      config->spi[bus].mosi,
                      config->spi[bus].miso,
                      config->spi[bus].baud);
}

void pointing_device_task(void *arg)
{
    config_t *config = arg;
    struct filter_state *pmw3360_filters = NULL;
    struct filter_state *pmw3389_filters = NULL;
    accel_prepare(&config->move_accel);
    accel_prepare(&config->scroll_accel);

    if (config->nr_pmw3360) {
        pmw3360_filters = k_malloc(sizeof(*pmw3360_filters) * config->nr_pmw3360);
        __ASSERT_NO_MSG(pmw3360_filters);
    }

    if (config->nr_pmw3389) {
        pmw3389_filters = k_malloc(sizeof(*pmw3389_filters) * config->nr_pmw3389);
        __ASSERT_NO_MSG(pmw3389_filters);
    }

    k_poll_signal_init(&motion_signal);

    for (uint8_t bus = 0; bus < MAX_SPI; ++bus) {
        if (config->spi_mask & (uint8_t)(1u << bus))
            spi_bus_init_once(bus, config);
    }

    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        pmw3360_init(&config->pmw3360[i]);
        pmw3360_set_cpi(&config->pmw3360[i]);
        uint8_t role = config->pmw3360[i].role;
        filter_init(&pmw3360_filters[i],
                    role == SENSOR_ROLE_SCROLL ? &config->scroll_accel : &config->move_accel,
                    config->pmw3360[i].cpi);
        pointing_motion_irq_init(NULL, config->pmw3360[i].irq, i);
    }

    for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
        pmw3389_init(&config->pmw3389[i]);
        pmw3389_set_cpi(&config->pmw3389[i]);
        uint8_t role = config->pmw3389[i].role;
        filter_init(&pmw3389_filters[i],
                    role == SENSOR_ROLE_SCROLL ? &config->scroll_accel : &config->move_accel,
                    config->pmw3389[i].cpi);
        pointing_motion_irq_init(NULL, config->pmw3389[i].irq, (uint8_t)(MAX_PMW3360 + i));
    }

    struct k_poll_event event = K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL,
                                                         K_POLL_MODE_NOTIFY_ONLY,
                                                         &motion_signal);

    for (;;) {
        k_poll(&event, 1, K_FOREVER);
        k_poll_signal_reset(&motion_signal);
        event.state = K_POLL_STATE_NOT_READY;

        platform_sleep_ms(1);

        uint32_t bits = (uint32_t)atomic_set(&motion_bits, 0);
        int mouse_dx_sum = 0;
        int mouse_dy_sum = 0;
        int scroll_dx_sum = 0;
        int scroll_dy_sum = 0;
        struct device_event devev = {0};

        for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
            if (bits && !(bits & (1u << i)))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3360_get_deltas(&config->pmw3360[i], &dx, &dy);
            // Temporary transform for the current trackball prototype, whose sensor is mounted at 30 degrees.
            // This may move to config if per-device coordinate transforms are needed.
            int32_t x = x125pct(dx);
            int32_t y = -dy;
            uint8_t role = config->pmw3360[i].role;
            filter_process(&pmw3360_filters[i], &x, &y);
            if (role == SENSOR_ROLE_SCROLL) {
                scroll_dx_sum += x;
                scroll_dy_sum += y;
            } else {
                mouse_dx_sum += x;
                mouse_dy_sum += y;
            }
        }

        for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
            if (bits && !(bits & (1u << (MAX_PMW3360 + i))))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3389_get_deltas(&config->pmw3389[i], &dx, &dy);
            // Temporary transform for the current trackball prototype, whose sensor is mounted at 30 degrees.
            // This may move to config if per-device coordinate transforms are needed.
            int32_t x = x125pct(dx);
            int32_t y = -dy;
            uint8_t role = config->pmw3389[i].role;
            filter_process(&pmw3389_filters[i], &x, &y);
            if (role == SENSOR_ROLE_SCROLL) {
                scroll_dx_sum += x;
                scroll_dy_sum += y;
            } else {
                mouse_dx_sum += x;
                mouse_dy_sum += y;
            }
        }

        if (mouse_dx_sum || mouse_dy_sum) {
            devev.type = DEV_MOUSE_MOVE;
            devev.x = mouse_dx_sum;
            devev.y = mouse_dy_sum;
            platform_input_event_send(&devev, PLATFORM_EVENT_WAIT_FOREVER);
        }
        if (scroll_dx_sum || scroll_dy_sum) {
            devev.type = DEV_MOUSE_SCROLL;
            devev.x = scroll_dx_sum;
            devev.y = scroll_dy_sum;
            platform_input_event_send(&devev, PLATFORM_EVENT_WAIT_FOREVER);
        }
    }
}
