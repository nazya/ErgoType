#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "device.h"
#include "accel/filter.h"
#include "jconfig.h"
#include "log.h"
#include "pmw3360.h"
#include "pmw3389.h"
#include "pointer.h"

extern QueueHandle_t input_event_queue;

static TaskHandle_t motion_task_handle = NULL;
static bool mot_irq_callback_installed = false;
static uint32_t mot_pin_bits[30];
static spi_inst_t *const spi_by_idx[MAX_SPI] = { spi0, spi1 };

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

static void mot_irq_handler(uint gpio, uint32_t events)
{
    // Only `GPIO_IRQ_EDGE_FALL` is enabled for motion pins (see `mot_gpio_init()`).
    // if (!(events & GPIO_IRQ_EDGE_FALL))
    //     return;
    (void)events;

    uint32_t bits = 0;
    bits = mot_pin_bits[gpio];
    if (!bits)
        return;

    BaseType_t xHPTW = pdFALSE;
    xTaskNotifyFromISR(motion_task_handle, bits, eSetBits, &xHPTW);
    portYIELD_FROM_ISR(xHPTW);
}

static void mot_gpio_init(int8_t mot_pin)
{
    gpio_init((uint)mot_pin);
    gpio_set_dir((uint)mot_pin, GPIO_IN);
    gpio_pull_up((uint)mot_pin); // idle HIGH

    if (!mot_irq_callback_installed) {
        gpio_set_irq_enabled_with_callback((uint)mot_pin, GPIO_IRQ_EDGE_FALL, true, mot_irq_handler);
        mot_irq_callback_installed = true;
    } else {
        gpio_set_irq_enabled((uint)mot_pin, GPIO_IRQ_EDGE_FALL, true);
    }
}

void pointing_motion_irq_init(TaskHandle_t task_handle, int8_t mot_pin, uint8_t sensor_idx)
{
    motion_task_handle = task_handle;
    mot_pin_bits[(uint)mot_pin] |= (1u << sensor_idx);
    mot_gpio_init(mot_pin);
}

static void spi_bus_init_once(uint8_t bus, const config_t *config)
{
    spi_inst_t *spi = spi_by_idx[bus];
    int8_t sck = config->spi[bus].sck;
    int8_t mosi = config->spi[bus].mosi;
    int8_t miso = config->spi[bus].miso;

    gpio_set_function((uint)sck, GPIO_FUNC_SPI);
    gpio_set_function((uint)mosi, GPIO_FUNC_SPI);
    gpio_set_function((uint)miso, GPIO_FUNC_SPI);

    spi_init(spi, (uint)config->spi[bus].baud);
}

void pointing_device_task(void *pvParameters)
{
    config_t *config = pvParameters;
    struct filter_state *pmw3360_filters = NULL;
    struct filter_state *pmw3389_filters = NULL;
    accel_prepare(&config->move_accel);
    accel_prepare(&config->scroll_accel);

    if (config->nr_pmw3360) {
        pmw3360_filters = pvPortMalloc(sizeof(*pmw3360_filters) * config->nr_pmw3360);
        configASSERT(pmw3360_filters);
    }

    if (config->nr_pmw3389) {
        pmw3389_filters = pvPortMalloc(sizeof(*pmw3389_filters) * config->nr_pmw3389);
        configASSERT(pmw3389_filters);
    }

    for (uint8_t bus = 0; bus < MAX_SPI; ++bus) {
        if (config->spi_mask & (uint8_t)(1u << bus)) {
            spi_bus_init_once(bus, config);
        }
    }

    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        pmw3360_init(&config->pmw3360[i]);
        pmw3360_set_cpi(&config->pmw3360[i]);
        uint8_t role = config->pmw3360[i].role;
        filter_init(&pmw3360_filters[i],
                    role == SENSOR_ROLE_SCROLL ? &config->scroll_accel : &config->move_accel,
                    config->pmw3360[i].cpi);
    }

    for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
        pmw3389_init(&config->pmw3389[i]);
        pmw3389_set_cpi(&config->pmw3389[i]);
        uint8_t role = config->pmw3389[i].role;
        filter_init(&pmw3389_filters[i],
                    role == SENSOR_ROLE_SCROLL ? &config->scroll_accel : &config->move_accel,
                    config->pmw3389[i].cpi);
    }

    while (1) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        vTaskDelay(1);

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
            xQueueSendToBack(input_event_queue, &devev, portMAX_DELAY);
        }
        if (scroll_dx_sum || scroll_dy_sum) {
            devev.type = DEV_MOUSE_SCROLL;
            devev.x = scroll_dx_sum;
            devev.y = scroll_dy_sum;
            xQueueSendToBack(input_event_queue, &devev, portMAX_DELAY);
        }
    }
}
