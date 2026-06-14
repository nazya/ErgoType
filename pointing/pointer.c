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
#include "pmw3389.h"
#include "pointer.h"
#include "device.h"

static TaskHandle_t motion_task_handle = NULL;
static bool mot_irq_callback_installed = false;
static uint32_t mot_pin_bits[30];
static spi_inst_t *const spi_by_idx[MAX_SPI] = { spi0, spi1 };

static void send_pointing_event(QueueHandle_t queue, uint8_t type, int32_t x, int32_t y)
{
    struct device_event ev = {
        .type = type,
        .x = x,
        .y = y,
    };

    xQueueSendToBack(queue, &ev, portMAX_DELAY);
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
    const config_t *config = (const config_t *)pvParameters;
    struct device pmw3360_devices[MAX_PMW3360];
    struct device pmw3389_devices[MAX_PMW3389];
    QueueHandle_t pmw3360_queues[MAX_PMW3360];
    QueueHandle_t pmw3389_queues[MAX_PMW3389];

    for (uint8_t bus = 0; bus < MAX_SPI; ++bus) {
        if (config->spi_mask & (uint8_t)(1u << bus)) {
            spi_bus_init_once(bus, config);
        }
    }

    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        pmw3360_devices[i] = (struct device) {
            .capabilities = CAP_MOUSE,
            .name = "pmw3360",
        };
        snprintf(pmw3360_devices[i].id, sizeof(pmw3360_devices[i].id), "pmw3360:%u", i);
        pmw3360_devices[i].events = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct device_event));
        configASSERT(pmw3360_devices[i].events);
        device_add(&pmw3360_devices[i]);
        pmw3360_queues[i] = pmw3360_devices[i].events;

        pmw3360_init(&config->pmw3360[i]);
        pmw3360_set_cpi(&config->pmw3360[i]);
    }

    for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
        pmw3389_devices[i] = (struct device) {
            .capabilities = CAP_MOUSE,
            .name = "pmw3389",
        };
        snprintf(pmw3389_devices[i].id, sizeof(pmw3389_devices[i].id), "pmw3389:%u", i);
        pmw3389_devices[i].events = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct device_event));
        configASSERT(pmw3389_devices[i].events);
        device_add(&pmw3389_devices[i]);
        pmw3389_queues[i] = pmw3389_devices[i].events;

        pmw3389_init(&config->pmw3389[i]);
        pmw3389_set_cpi(&config->pmw3389[i]);
    }

    while (1) {
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, portMAX_DELAY);
        vTaskDelay(2);

        for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
            if (bits && !(bits & (1u << i)))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3360_get_deltas(&config->pmw3360[i], &dx, &dy);
            send_pointing_event(pmw3360_queues[i],
                                config->pmw3360[i].role == SENSOR_ROLE_SCROLL ? DEV_MOUSE_SCROLL : DEV_MOUSE_MOVE,
                                dx,
                                dy);
        }

        for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
            if (bits && !(bits & (1u << (MAX_PMW3360 + i))))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3389_get_deltas(&config->pmw3389[i], &dx, &dy);
            send_pointing_event(pmw3389_queues[i],
                                config->pmw3389[i].role == SENSOR_ROLE_SCROLL ? DEV_MOUSE_SCROLL : DEV_MOUSE_MOVE,
                                dx,
                                dy);
        }
    }
}
