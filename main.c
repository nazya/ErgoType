 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio_uart.h"
#include "pico/stdio_usb.h"
// #include "pico/cyw43_arch.h"

#include "tusb.h"
#include "ff.h"
#include "usb_descriptors.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "flash.h"
#include "daemon.h"
#include "jconfig.h"
#include "led/plain.h"
#include "led/ws2812.h"
#include "pointing/pointer.h"

#define TUD_TASK_DELAY_MS 1
#define CDC_SETTLE_MS 50
#define TUD_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define MIN_STACK_SIZE configMINIMAL_STACK_SIZE
#define IDLE_PRIORITY tskIDLE_PRIORITY

#define CORE0 ( ( UBaseType_t ) ( 1u << 0 ) )
#define CORE1 ( ( UBaseType_t ) ( 1u << 1 ) )

// Shared runtime symbols (owned by main.c).
uint8_t mode; // read by USB descriptor callbacks

// FreeRTOS tasks
void tusb_device_task(void* pvParameters); // tusb_device_task.c
void keyscan_task(void* pvParameters); // keyscan.c
uint8_t count_pressed_keys(matrix_t* matrix); // keyscan.c

void _uart_init(const config_t *config) {
    int tx = config->uart_tx_pin;
    int rx = config->uart_rx_pin;

    int uart0_tx_ok = (tx == 0 || tx == 12 || tx == 16 || tx == 28);
    int uart0_rx_ok = (rx == 1 || rx == 13 || rx == 17 || rx == 29);
    int uart1_tx_ok = (tx == 4 || tx == 8 || tx == 20 || tx == 24);
    int uart1_rx_ok = (rx == 5 || rx == 9 || rx == 21 || rx == 25);

    if (uart0_tx_ok && uart0_rx_ok) {
        dbg("uart: using uart0 tx=%d rx=%d", tx, rx);
        stdio_uart_deinit();
        stdio_uart_init_full(uart0, 115200, tx, rx);
        return;
    }

    if (uart1_tx_ok && uart1_rx_ok) {
        dbg("uart: using uart1 tx=%d rx=%d", tx, rx);
        stdio_uart_deinit();
        stdio_uart_init_full(uart1, 115200, tx, rx);
        return;
    }

    if (IS_GPIO_PIN(tx) || IS_GPIO_PIN(rx)) {
        warn("uart: invalid pin pair tx=%d rx=%d", tx, rx);
    }

    stdio_uart_deinit();
    uart_deinit(uart0);
    uart_deinit(uart1);
}

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

int main() {
    board_init();
    stdio_uart_init();

    static config_t config;
    int parse_rc = parse(&config, "config.json");
    uint8_t base_mode = (parse_rc == 0) ? HID : MSC; // HID by default; if config parse failed -> start MSC

    _uart_init(&config); // until initialization no debugging outtput.
    
    mode_resolution_t mode_resolution = resolve_mode(&config, base_mode);
    mode = mode_resolution.mode;

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    tusb_init();
    stdio_usb_init();

    for (int i = 0; i < 1000 && !tud_cdc_connected(); ++i) {
        tud_task();
        sleep_ms(1);
    }
    for (int i = 0; i < CDC_SETTLE_MS; ++i) {
        tud_task();
        sleep_ms(1);
    }
    dbg("CDC ready");
    if (base_mode != HID) {
        warn("base mode=MSC: config parse failed");
    }
    msg("mode resolved: %s (%s)\n",
        mode_resolution.mode == HID ? "HID" : "MSC", mode_resolution.reason);
    print_parse_errors();
    dbg3config(&config);
    

    if (IS_GPIO_PIN(config.led_pin)) {
        xTaskCreateAffinitySet(gpio_led_task, NULL, MIN_STACK_SIZE, &config.led_pin, IDLE_PRIORITY,
                               CORE1, NULL);
    }
    if (IS_GPIO_PIN(config.ws2812_pin)) {
        xTaskCreateAffinitySet(ws2812_task, NULL, MIN_STACK_SIZE, &config.ws2812_pin, IDLE_PRIORITY,
                               CORE1, NULL);
    }

    static TickType_t tusb_task_delay = pdMS_TO_TICKS(TUD_TASK_DELAY_MS);
    if (mode == HID) {
        dbg("entered HID mode");

        QueueHandle_t keyscan_queue_handle = xQueueCreate(16, sizeof(struct device_event));
        static void *keyscan_task_params[2];
        keyscan_task_params[0] = &config;               // config_t*
        keyscan_task_params[1] = keyscan_queue_handle;  // QueueHandle_t

        xTaskCreateAffinitySet(keyscan_task, NULL, MIN_STACK_SIZE, keyscan_task_params, IDLE_PRIORITY + 5, CORE1,
                               NULL);
        xTaskCreateAffinitySet(keyd_task, NULL, 8192, keyscan_queue_handle, IDLE_PRIORITY + 4, CORE1,
                               NULL); // empirically: min free watermark was 3408 words
        xTaskCreateAffinitySet(tusb_device_task, NULL, MIN_STACK_SIZE, &tusb_task_delay, IDLE_PRIORITY + 3,
                               CORE0, NULL);
        xTaskCreateAffinitySet(key_event_hid_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 2,
                               CORE0, NULL);

        TaskHandle_t pointing_task_handle = NULL;
        xTaskCreateAffinitySet(pointing_device_task, NULL, MIN_STACK_SIZE, NULL, IDLE_PRIORITY + 1,
                               CORE0, &pointing_task_handle);
        pointing_motion_irq_init(pointing_task_handle);
    } else {
        dbg("entered MSC mode");
        xTaskCreateAffinitySet(tusb_device_task, NULL, TUD_STACK_SIZE, &tusb_task_delay, IDLE_PRIORITY + 1,
                               CORE0, NULL);
    }
    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}
