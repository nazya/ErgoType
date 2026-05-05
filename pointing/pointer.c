#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "task.h"

#include "jconfig.h"
#include "log.h"
#include "pmw3360.h"
#include "pointer.h"
#include "vkbd.h"

static TaskHandle_t motion_task_handle = NULL;
static bool mot_irq_callback_installed = false;
static uint32_t mot_pin_bits[30];

static void mot_irq_handler(uint gpio, uint32_t events)
{
    if (!(events & GPIO_IRQ_EDGE_FALL) || !motion_task_handle)
        return;

    uint32_t bits = 0;
    if (gpio < (uint)(sizeof(mot_pin_bits) / sizeof(mot_pin_bits[0])))
        bits = mot_pin_bits[gpio];
    if (!bits)
        return;

    BaseType_t xHPTW = pdFALSE;
    xTaskNotifyFromISR(motion_task_handle, bits, eSetBits, &xHPTW);
    portYIELD_FROM_ISR(xHPTW);
}

static void mot_gpio_init(int8_t mot_pin)
{
    if (!IS_GPIO_PIN(mot_pin))
        return;

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
    if (IS_GPIO_PIN(mot_pin) && sensor_idx < 32)
        mot_pin_bits[(uint)mot_pin] |= (1u << sensor_idx);
    mot_gpio_init(mot_pin);
}

static bool spi_bus_init_once(int8_t bus, const config_t *config)
{
    spi_inst_t *spi = (bus == 1) ? spi1 : spi0;
    int8_t sck = config->spi[(bus == 1) ? 1u : 0u].sck;
    int8_t mosi = config->spi[(bus == 1) ? 1u : 0u].mosi;
    int8_t miso = config->spi[(bus == 1) ? 1u : 0u].miso;

    if (!IS_GPIO_PIN(sck) || !IS_GPIO_PIN(mosi) || !IS_GPIO_PIN(miso))
        return false;

    gpio_set_function((uint)sck, GPIO_FUNC_SPI);
    gpio_set_function((uint)mosi, GPIO_FUNC_SPI);
    gpio_set_function((uint)miso, GPIO_FUNC_SPI);

    spi_init(spi, 500000);
    return true;
}

void pointing_device_task(void *pvParameters)
{
    const config_t *config = (const config_t *)pvParameters;
    if (!config || config->nr_pmw3360 == 0) {
        vTaskDelete(NULL);
        return;
    }

    bool bus_inited[2] = {false, false};
    pmw3360_t sensors[MAX_PMW3360] = {0};
    bool sensor_active[MAX_PMW3360] = {0};
    uint8_t nr = 0;

    uint8_t cfg_nr = config->nr_pmw3360;
    if (cfg_nr > MAX_PMW3360)
        cfg_nr = MAX_PMW3360;

    for (uint8_t i = 0; i < cfg_nr; ++i) {
        const pmw3360_cfg_t *cfg = &config->pmw3360[i];
        if (cfg->bus < 0 || cfg->bus > 1)
            continue;

        if (!bus_inited[(uint)cfg->bus]) {
            if (!spi_bus_init_once(cfg->bus, config)) {
                err("pmw3360[%u]: spi%d pins missing/invalid", i, cfg->bus);
                continue;
            }
            bus_inited[(uint)cfg->bus] = true;
        }

        pmw3360_t dev = {
            .spi = (cfg->bus == 1) ? spi1 : spi0,
            .cs = cfg->cs,
            .baud = cfg->baud,
            .mode = cfg->mode,
        };

        if (!pmw3360_init(&dev)) {
            err("pmw3360[%u]: init failed", i);
            continue;
        }

        pmw3360_set_cpi(&dev, cfg->cpi);
        sensors[i] = dev;
        sensor_active[i] = true;
        nr++;
    }

    if (nr == 0) {
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        vTaskDelay(2);

        int32_t mouse_dx_sum = 0;
        int32_t mouse_dy_sum = 0;
        int32_t scroll_dx_sum = 0;
        int32_t scroll_dy_sum = 0;
        for (uint8_t i = 0; i < cfg_nr; ++i) {
            if (!sensor_active[i])
                continue;
            if (bits && !(bits & (1u << i)))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3360_get_deltas(&sensors[i], &dx, &dy);
            if (config->pmw3360[i].role == PMW3360_ROLE_SCROLL) {
                scroll_dx_sum += dx;
                scroll_dy_sum += dy;
            } else {
                mouse_dx_sum += dx;
                mouse_dy_sum += dy;
            }
        }

        if (mouse_dx_sum || mouse_dy_sum)
            vkbd_mouse_move(NULL, (int)mouse_dx_sum, (int)mouse_dy_sum);
        if (scroll_dx_sum || scroll_dy_sum)
            vkbd_mouse_scroll(NULL, (int)scroll_dx_sum, (int)scroll_dy_sum);
    }
}
