#include <stdbool.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "accel/filter.h"
#include "devmon.h"
#include "jconfig.h"
#include "log.h"
#include "pmw3360.h"
#include "pmw3389.h"
#include "pointer.h"

static TaskHandle_t motion_task_handle = NULL;
static bool mot_irq_callback_installed = false;
static uint32_t mot_pin_bits[30];
static spi_inst_t *const spi_by_idx[MAX_SPI] = { spi0, spi1 };
enum {
    FILTER_CURVE_SCALE = 20,
};
static const int32_t move_filter_points_q10[] = {
    FILTER_Q10(0, FILTER_CURVE_SCALE),
    FILTER_Q10(1, FILTER_CURVE_SCALE),
    FILTER_Q10(4, FILTER_CURVE_SCALE),
    FILTER_Q10(10, FILTER_CURVE_SCALE),
    FILTER_Q10(20, FILTER_CURVE_SCALE),
    FILTER_Q10(50, FILTER_CURVE_SCALE),
    FILTER_Q10(90, FILTER_CURVE_SCALE),
};
static const int32_t scroll_filter_points_q10[] = {
    FILTER_Q10(0, FILTER_CURVE_SCALE),
    FILTER_Q10(1, FILTER_CURVE_SCALE),
    FILTER_Q10(4, FILTER_CURVE_SCALE),
    FILTER_Q10(10, FILTER_CURVE_SCALE),
    FILTER_Q10(20, FILTER_CURVE_SCALE),
    FILTER_Q10(50, FILTER_CURVE_SCALE),
    FILTER_Q10(90, FILTER_CURVE_SCALE),
};
static const struct filter_params move_filter_params = {
    .profile = FILTER_PROFILE_CUSTOM,
    .speed_q10 = FILTER_Q10(0, 1),
    .custom_step_q10 = FILTER_Q10(30, FILTER_CURVE_SCALE),
    .custom_points_q10 = move_filter_points_q10,
    .custom_npoints = sizeof(move_filter_points_q10) / sizeof(move_filter_points_q10[0]),
    .adaptive_velocity_averaging = true,
};
static const struct filter_params scroll_filter_params = {
    .profile = FILTER_PROFILE_CUSTOM,
    .speed_q10 = FILTER_Q10(0, 1),
    .custom_step_q10 = FILTER_Q10(30, FILTER_CURVE_SCALE),
    .custom_points_q10 = scroll_filter_points_q10,
    .custom_npoints = sizeof(scroll_filter_points_q10) / sizeof(scroll_filter_points_q10[0]),
    .adaptive_velocity_averaging = true,
};

struct pointing_filter {
    struct filter_state *state;
    const struct filter_params *params;
};

static void send_pointing_input_event(QueueHandle_t queue,
                                      uint16_t type,
                                      uint16_t code,
                                      int32_t value)
{
    struct input_event ev = {
        .type = type,
        .code = code,
        .value = value,
    };

    xQueueSendToBack(queue, &ev, portMAX_DELAY);
}

static void send_pointing_event(QueueHandle_t queue,
                                bool scroll,
                                struct pointing_filter *filter,
                                int32_t x,
                                int32_t y)
{
    uint32_t time_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;

    filter_process(filter->state, filter->params, time_ms, &x, &y);

    if (scroll) {
        send_pointing_input_event(queue, EV_REL, REL_HWHEEL, x);
        send_pointing_input_event(queue, EV_REL, REL_WHEEL, y);
    } else {
        send_pointing_input_event(queue, EV_REL, REL_X, x);
        send_pointing_input_event(queue, EV_REL, REL_Y, y);
    }
    send_pointing_input_event(queue, EV_SYN, SYN_REPORT, 0);
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
    struct port_input_dev pmw3360_devices[MAX_PMW3360];
    struct port_input_dev pmw3389_devices[MAX_PMW3389];
    QueueHandle_t pmw3360_queues[MAX_PMW3360];
    QueueHandle_t pmw3389_queues[MAX_PMW3389];
    struct pointing_filter move_filter = { .params = &move_filter_params };
    struct pointing_filter scroll_filter = { .params = &scroll_filter_params };

    move_filter.state = pvPortMalloc(sizeof *move_filter.state);
    scroll_filter.state = pvPortMalloc(sizeof *scroll_filter.state);
    configASSERT(move_filter.state);
    configASSERT(scroll_filter.state);
    filter_init(move_filter.state, move_filter.params, DEFAULT_MOUSE_DPI);
    filter_init(scroll_filter.state, scroll_filter.params, DEFAULT_MOUSE_DPI);

    for (uint8_t bus = 0; bus < MAX_SPI; ++bus) {
        if (config->spi_mask & (uint8_t)(1u << bus)) {
            spi_bus_init_once(bus, config);
        }
    }

    for (uint8_t i = 0; i < config->nr_pmw3360; ++i) {
        pmw3360_devices[i] = (struct port_input_dev) {
            .vendor = 0x0000,
            .product = 0x0002,
            .name = "pmw3360",
        };
        input_bitmap_set(REL_X, pmw3360_devices[i].relbit);
        input_bitmap_set(REL_Y, pmw3360_devices[i].relbit);
        input_bitmap_set(REL_WHEEL, pmw3360_devices[i].relbit);
        input_bitmap_set(REL_HWHEEL, pmw3360_devices[i].relbit);
        pmw3360_devices[i].ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
        configASSERT(pmw3360_devices[i].ev_queue);
        int add_rc = devmon_add_device(&pmw3360_devices[i]);
        configASSERT(add_rc == 0);
        pmw3360_queues[i] = pmw3360_devices[i].ev_queue;

        pmw3360_init(&config->pmw3360[i]);
        pmw3360_set_cpi(&config->pmw3360[i]);
    }

    for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
        pmw3389_devices[i] = (struct port_input_dev) {
            .vendor = 0x0000,
            .product = 0x0002,
            .name = "pmw3389",
        };
        input_bitmap_set(REL_X, pmw3389_devices[i].relbit);
        input_bitmap_set(REL_Y, pmw3389_devices[i].relbit);
        input_bitmap_set(REL_WHEEL, pmw3389_devices[i].relbit);
        input_bitmap_set(REL_HWHEEL, pmw3389_devices[i].relbit);
        pmw3389_devices[i].ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
        configASSERT(pmw3389_devices[i].ev_queue);
        int add_rc = devmon_add_device(&pmw3389_devices[i]);
        configASSERT(add_rc == 0);
        pmw3389_queues[i] = pmw3389_devices[i].ev_queue;

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
            bool scroll = config->pmw3360[i].role == SENSOR_ROLE_SCROLL;
            struct pointing_filter *filter = scroll ? &scroll_filter : &move_filter;
            send_pointing_event(pmw3360_queues[i],
                                scroll,
                                filter,
                                dx,
                                dy);
        }

        for (uint8_t i = 0; i < config->nr_pmw3389; ++i) {
            if (bits && !(bits & (1u << (MAX_PMW3360 + i))))
                continue;
            int16_t dx = 0;
            int16_t dy = 0;
            pmw3389_get_deltas(&config->pmw3389[i], &dx, &dy);
            bool scroll = config->pmw3389[i].role == SENSOR_ROLE_SCROLL;
            struct pointing_filter *filter = scroll ? &scroll_filter : &move_filter;
            send_pointing_event(pmw3389_queues[i],
                                scroll,
                                filter,
                                dx,
                                dy);
        }
    }
}
