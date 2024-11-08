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

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "flash.h"
#include "daemon.h"
#include "jconfig.h"


#define TUD_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define MIN_STACK_SZIE configMINIMAL_STACK_SIZE

// Mode selection variables
enum mode {
    HID = 0,
    MSC = 1
};
uint8_t mode; // 0: HID mode, 1: MSC mode
char errstr[ERRSTR_LENGTH] = {0}; 

QueueHandle_t keyscan_queue;

// FreeRTOS tasks
void led_task(void* pvParameters); // led.c
void usb_device_task(void* pvParameters); // tud.c
void keyscan_task(void* pvParameters); // keyscan.c

uint8_t count_pressed_keys(matrix_t* matrix); // keyscan.c

// UART0:
//     UART0_TX: GPIO 0, 12, 16, 28
//     UART0_RX: GPIO 1, 13, 17, 29
// UART1:
//     UART1_TX: GPIO 4, 8, 20, 24
//     UART1_RX: GPIO 5, 9, 21, 25
void _uart_init(const config_t *config) {
    if (IS_GPIO_PIN(config->uart_rx_pin) && IS_GPIO_PIN(config->uart_tx_pin)) {
        uart_init(uart0, 115200);
        gpio_set_function(config->uart_tx_pin, GPIO_FUNC_UART);
        gpio_set_function(config->uart_rx_pin, GPIO_FUNC_UART);
    } else {
        uart_deinit(uart0);  // Or UART1_ID based on usage
    }
}

void check_and_reinit_filesystem() {
    FATFS filesystem;
    FRESULT res = f_mount(&filesystem, "/", 1);
    if (res != FR_OK) {
        flash_fat_initialize();
    }
    f_unmount("/");
}

int init_and_read_pin(int pin) {
    if (!IS_GPIO_PIN(pin))
        return -1;
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_init(pin);
    gpio_pull_up(pin);
    gpio_set_dir(pin, GPIO_IN);
    sleep_us(30);
    return gpio_get(pin);
}

int main() {
    board_init();
    stdio_init_all();
    check_and_reinit_filesystem();

    config_t config;
    // mode = parse(&config, "config.json") != 0;
    mode = parse(&config, "config.json") == 0 ? HID : MSC;
    printf("Mode selected: %s\n", mode == HID ? "HID" : "MSC");

    if (strlen(errstr) > 0) {
        printf("Errors in parsing 'config.json':\n%s", errstr);
    }

    _uart_init(&config); // until initialization no debugging outtput.
    uint8_t nr_pressed = 0;
    if (mode == HID) {
        nr_pressed = count_pressed_keys(&config.matrix);
        printf("Pressed keys at startup: %d\n", nr_pressed);

        switch(init_and_read_pin(config.erase_pin)) { // Read the button state (active low)
        case 0:
            flash_fat_initialize();
            mode = MSC;
            break;
        case 1:
            break;
        default:
            if (config.nr_pressed_erase > 0 && nr_pressed >= config.nr_pressed_erase) {
                flash_fat_initialize();
                mode = MSC;
            }
        }
    }
    printf("Mode selected: %s\n", mode == HID ? "HID" : "MSC");
    if (mode == HID) {
        switch(init_and_read_pin(config.msc_pin)) { // Read the button state (active low)
        case 0:
            printf("msc %d\n", init_and_read_pin(config.msc_pin)  );
            mode = MSC; 
            break;
        case 1:
            printf("msc %d\n", init_and_read_pin(config.msc_pin)  );
            mode = HID;
            break;
        default:
            if (config.nr_pressed_msc > 0 && nr_pressed >= config.nr_pressed_msc){
                mode = MSC;
                printf("set not via pin\n");

            }
        }
    }    
    printf("Mode selected: %s\n", mode == HID ? "HID" : "MSC");
    
    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    tusb_init();

    BaseType_t taskCreated;
    if (IS_GPIO_PIN(config.led_pin)) {
        xTaskCreate(led_task, NULL, MIN_STACK_SZIE, &config, tskIDLE_PRIORITY, NULL);
    }
    if (mode == HID) {
        printf("Enter HID mode\n");

        // keyd_init();
        keyscan_queue = xQueueCreate(16, sizeof(struct device_event));

        xTaskCreate(keyd_task,          NULL,    8*1024,      NULL,    configMAX_PRIORITIES - 2, NULL);
        xTaskCreate(key_event_hid_task, NULL, MIN_STACK_SZIE, NULL,    tskIDLE_PRIORITY + 3, NULL);
        xTaskCreate(keyscan_task,       NULL, MIN_STACK_SZIE, &config, configMAX_PRIORITIES - 1, NULL);
        xTaskCreate(usb_device_task,    NULL, MIN_STACK_SZIE, NULL,    tskIDLE_PRIORITY + 2, NULL);
    }
    else {
        printf("Enter MSC mode\n");
        xTaskCreate(usb_device_task,    NULL, TUD_STACK_SIZE, NULL, configMAX_PRIORITIES - 3, NULL);
    }
    printf("Starting os\n");
    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}