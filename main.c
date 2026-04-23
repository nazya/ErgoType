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
#define TUD_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define MIN_STACK_SIZE configMINIMAL_STACK_SIZE
#define IDLE_PRIORITY tskIDLE_PRIORITY

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
        uart_init(uart0, 115200);
        gpio_set_function(tx, GPIO_FUNC_UART);
        gpio_set_function(rx, GPIO_FUNC_UART);
        return;
    }

    if (uart1_tx_ok && uart1_rx_ok) {
        dbg("uart: using uart1 tx=%d rx=%d", tx, rx);
        uart_init(uart1, 115200);
        gpio_set_function(tx, GPIO_FUNC_UART);
        gpio_set_function(rx, GPIO_FUNC_UART);
        return;
    }

    if (IS_GPIO_PIN(tx) || IS_GPIO_PIN(rx)) {
        warn("uart: invalid pin pair tx=%d rx=%d", tx, rx);
    }

    uart_deinit(uart0);
    uart_deinit(uart1);
}

void check_reinit_filesystem() {
    FATFS filesystem;
    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK) {
        flash_fat_initialize();
    }
    f_unmount("/");
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

static uint8_t resolve_mode(config_t *config, uint8_t base_mode) {
    if (base_mode == HID) {
        dbg("base mode=HID");
    } else {
        warn("base mode=MSC: config parse failed");
    }

    uint8_t mode = base_mode;
    uint8_t nr_pressed = count_pressed_keys(&config->matrix);
    dbg3("pressed keys at startup: %d", nr_pressed);

    if (mode == HID) {

        int erase_pin_state = init_and_read_pin(config->erase_pin);
        dbg("erase pin=%d state=%d", config->erase_pin, erase_pin_state);
        switch (erase_pin_state) { // Read the button state (active low)
        case 0:
            dbg("switching to MSC: erase pin active");
            flash_fat_initialize();
            mode = MSC;
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_erase > 0 && nr_pressed >= config->nr_pressed_erase) {
                dbg("switching to MSC: nr_pressed(%u) >= nr_pressed_erase(%d)",
                    (unsigned)nr_pressed, config->nr_pressed_erase);
                flash_fat_initialize();
                mode = MSC;
            }
        }
    }

    if (mode == HID) {
        int msc_pin_state = init_and_read_pin(config->msc_pin);
        switch (msc_pin_state) { // Read the button state (active low)
        case 0:
            dbg("switching to MSC: msc pin active");
            mode = MSC;
            break;
        case 1:
            break;
        default:
            if (config->nr_pressed_msc > 0 && nr_pressed >= config->nr_pressed_msc) {
                mode = MSC;
                dbg("switching to MSC: nr_pressed(%u) >= nr_pressed_msc(%d)",
                    (unsigned)nr_pressed, config->nr_pressed_msc);
            }
        }
    }

    dbg("mode resolved: %s", mode == HID ? "HID" : "MSC");
    return mode;
}

int main() {
    board_init();
    stdio_init_all();
    check_reinit_filesystem();

    static config_t config;
    int parse_rc = parse(&config, "config.json");
    uint8_t base_mode = (parse_rc == 0) ? HID : MSC; // HID by default; if config parse failed -> start MSC

    if (strlen(errstr) > 0) {
        warn("Errors in parsing 'config.json':\n%s", errstr);
    }

    _uart_init(&config); // until initialization no debugging outtput.

    mode = resolve_mode(&config, base_mode);

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    tusb_init();

    if (IS_GPIO_PIN(config.led_pin)) {
        xTaskCreate(gpio_led_task, NULL, MIN_STACK_SIZE, &config.led_pin,    IDLE_PRIORITY, NULL);
    }
    if (IS_GPIO_PIN(config.ws2812_pin)) {
        xTaskCreate(ws2812_task,   NULL, MIN_STACK_SIZE, &config.ws2812_pin, IDLE_PRIORITY, NULL);
    }

    static TickType_t tusb_task_delay = pdMS_TO_TICKS(TUD_TASK_DELAY_MS);
    if (mode == HID) {
        dbg("entered HID mode");
        
        QueueHandle_t keyscan_queue_handle = xQueueCreate(16, sizeof(struct device_event));
        static void *keyscan_task_params[2];
        keyscan_task_params[0] = &config;              // config_t*
        keyscan_task_params[1] = keyscan_queue_handle; // QueueHandle_t
        
        xTaskCreate(keyscan_task,         NULL, MIN_STACK_SIZE, keyscan_task_params,  IDLE_PRIORITY + 5, NULL);
        xTaskCreate(keyd_task,            NULL, 8192,           keyscan_queue_handle, IDLE_PRIORITY + 4, NULL); // empirically: min free watermark was 3408 words
        xTaskCreate(tusb_device_task,     NULL, MIN_STACK_SIZE, &tusb_task_delay,     IDLE_PRIORITY + 3, NULL);
        xTaskCreate(key_event_hid_task,   NULL, MIN_STACK_SIZE, NULL,                 IDLE_PRIORITY + 2, NULL);
        

        TaskHandle_t pointing_task_handle = NULL;
        xTaskCreate(pointing_device_task, NULL, MIN_STACK_SIZE, NULL,                 IDLE_PRIORITY + 1, &pointing_task_handle);
        pointing_motion_irq_init(pointing_task_handle);
    } else {
        dbg("entered MSC mode");
        xTaskCreate(tusb_device_task,     NULL, TUD_STACK_SIZE, &tusb_task_delay,     IDLE_PRIORITY + 1, NULL);
    }
    dbg("starting os");
    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}
