 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
// #include "pico/cyw43_arch.h"

#include "tusb.h"
#include "ff.h"
#include "usb_descriptors.h"

#include "stdio_tusb_cdc.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "flash.h"
#include "daemon.h"
#include "jconfig.h"
#include "led/plain.h"
#include "led/ws2812.h"
#include "pointing/pointer.h"
#include "log.h"

#define TUD_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define MIN_STACK_SIZE configMINIMAL_STACK_SIZE
#define IDLE_PRIORITY tskIDLE_PRIORITY

#define TUSB_PRIORITY ( configMAX_PRIORITIES - 2 ) // keep below timer task

#define CORE0 ( ( UBaseType_t ) ( 1u << 0 ) )
#define CORE1 ( ( UBaseType_t ) ( 1u << 1 ) )

// Shared runtime symbols (owned by main.c).
uint8_t mode; // read by USB descriptor callbacks
SemaphoreHandle_t log_mutex;

// FreeRTOS tasks
void tusb_device_task(void* pvParameters); // tusb_device_task.c
void keyscan_task(void* pvParameters); // keyscan.c
uint8_t count_pressed_keys(matrix_t* matrix); // keyscan.c
bool uart_stdio_init(const config_t *config); // uart_stdio.c

static void app_task(void *pvParameters);

int8_t init_and_read_pin(int pin) {
    if (!IS_GPIO_PIN(pin))
        return -1;
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_init(pin);
    gpio_pull_up(pin);
    gpio_set_dir(pin, GPIO_IN);
    sleep_us(2000); // for YD2040 USR button
    return gpio_get(pin);
}

typedef struct {
    uint8_t mode;
    const char *reason;
} mode_resolution_t;

static mode_resolution_t resolve_mode(config_t *config, uint8_t base_mode) {
    mode_resolution_t resolution = {
        .mode = base_mode,
        .reason = base_mode == HID ? "base HID" : "config parse failed",
    };
    uint8_t nr_pressed = count_pressed_keys(&config->matrix);

    if (resolution.mode == HID) {

        int erase_pin_state = init_and_read_pin(config->erase_pin);
        switch (erase_pin_state) { // Read the button state (active low)
        case 0:
            flash_fat_initialize();
            resolution.mode = MSC;
            resolution.reason = "erase pin active";
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_erase > 0 && nr_pressed >= config->nr_pressed_erase) {
                flash_fat_initialize();
                resolution.mode = MSC;
                resolution.reason = "nr_pressed >= nr_pressed_erase";
            }
        }
    }

    if (resolution.mode == HID) {
        int msc_pin_state = init_and_read_pin(config->msc_pin);
        switch (msc_pin_state) { // Read the button state (active low)
        case 0:
            resolution.mode = MSC;
            resolution.reason = "msc pin active";
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_msc > 0 && nr_pressed >= config->nr_pressed_msc) {
                resolution.mode = MSC;
                resolution.reason = "nr_pressed >= nr_pressed_msc";
            }
        }
    }

	return resolution;
}

static void app_task(void *pvParameters)
{
    (void)pvParameters;

    static config_t config;

    int parse_rc = parse(&config, "config.json");
    uint8_t base_mode = (parse_rc == 0) ? HID : MSC; // HID by default; if config parse failed -> start MSC

    // Optional: enable "plain" printf()/puts() over UART if config pins are valid.
    uart_stdio_init(&config);

    mode_resolution_t mode_resolution = resolve_mode(&config, base_mode);
    mode = mode_resolution.mode;

    if (mode == HID) {
        xTaskCreateAffinitySet(tusb_device_task, NULL, MIN_STACK_SIZE, NULL, TUSB_PRIORITY,
                               CORE0, NULL);
    } else {
        xTaskCreateAffinitySet(tusb_device_task, NULL, TUD_STACK_SIZE, NULL, TUSB_PRIORITY,
                               CORE0, NULL);
    }

    // Print mode/config diagnostics (buffered until CDC is actually opened by the host).
    if (base_mode != HID) {
        warn("base mode=MSC: config parse failed");
    }
    msg("mode resolved: %s (%s)\n",
        mode == HID ? "HID" : "MSC",
        mode_resolution.reason);
    dbg3config(&config);
    

    if (IS_GPIO_PIN(config.led_pin)) {
        xTaskCreateAffinitySet(gpio_led_task, NULL, MIN_STACK_SIZE, &config.led_pin, IDLE_PRIORITY,
                               CORE1, NULL);
    }
    if (IS_GPIO_PIN(config.ws2812_pin)) {
        xTaskCreateAffinitySet(ws2812_task, NULL, MIN_STACK_SIZE, &config.ws2812_pin, IDLE_PRIORITY,
                               CORE1, NULL);
    }

    if (mode == HID) {
        QueueHandle_t keyscan_queue_handle = xQueueCreate(16, sizeof(struct device_event));
        static void *keyscan_task_params[2];
        keyscan_task_params[0] = &config;               // config_t*
        keyscan_task_params[1] = keyscan_queue_handle;  // QueueHandle_t

        xTaskCreateAffinitySet(keyscan_task, NULL, MIN_STACK_SIZE, keyscan_task_params, IDLE_PRIORITY + 5, CORE1,
                               NULL);
        xTaskCreateAffinitySet(keyd_task, NULL, 8192, keyscan_queue_handle, IDLE_PRIORITY + 4, CORE1,
                               NULL); // empirically: min free watermark was 3408 words
        xTaskCreateAffinitySet(key_event_hid_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 2,
                               CORE0, NULL);

        TaskHandle_t pointing_task_handle = NULL;
        xTaskCreateAffinitySet(pointing_device_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 1,
                               CORE0, &pointing_task_handle);
        pointing_motion_irq_init(pointing_task_handle);
    }

    vTaskDelete(NULL);
}

int main()
{
    board_init();

    // Keep main() thin: start the scheduler ASAP, run app init from a task.
    log_mutex = xSemaphoreCreateMutex();
    configASSERT(log_mutex);
    msg("\n")
    xTaskCreateAffinitySet(app_task, NULL, 4*MIN_STACK_SIZE, NULL, TUSB_PRIORITY - 1, CORE0, NULL);
    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}
