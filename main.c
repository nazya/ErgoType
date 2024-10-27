/* main.c */
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"

#include "tusb.h"
#include "ff.h"
// #include "ffconf.h"
// #include "pico/cyw43_arch.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// #include "bootsel_button.h"
#include "flash.h"
#include "log.h"
#include "daemon.h"

#include "jconfig.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
#define MODE_PIN 17 // GPIO18 for mode selection
#define ERASE_PIN 18 // GPIO18 for mode selection


#define TUD_TASK_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define MIN_STACK_SZIE      configMINIMAL_STACK_SIZE

// Mode selection variable
uint32_t mode = 1; // 0: HID mode, 1: MSC mode
QueueHandle_t keyd_queue;
QueueHandle_t keyscan_queue;
struct keyboard *active_kbd;
char errstr[2048];
// int log_level = 0;

// FreeRTOS tasks
void led_task(void* pvParameters); // led.c
// void hid_app_task(void* pvParameters); // hid.c
void usb_device_task(void* pvParameters); // tud.c
void keyscan_task(void* pvParameters); // keyscan.c

int main() {
    board_init();
    stdio_init_all();

    config_t config;
    ParseStatus status;
    status = parse(&config, "config.json");

    switch (status) {
        case SUCCESS:
            printf("Config parsed successfully.\n");
            mode = 0;
            break;
        case NO_JSON:
            printf("Error: No JSON provided. Triggers formating filesystem.\n");
            // flash_fat_initialize();
            break;
        case NO_GPIO_COLS:
        case NO_GPIO_ROWS:
        case INVALID_JSON:
        case KEYMAP_ERROR:
        default:
            printf("Error occurred, entering MSC mode\n");
            break;
    }

    gpio_init(ERASE_PIN);
    gpio_pull_up(ERASE_PIN);
    gpio_set_dir(ERASE_PIN, GPIO_IN);
    if (gpio_get(ERASE_PIN) ? 0 : 1) {
        flash_fat_initialize();
        printf("Filesystem reinited\n");
        while (1) { };
    }

    
    // Read mode selection button
    gpio_init(MODE_PIN);
    gpio_pull_up(MODE_PIN);
    gpio_set_dir(MODE_PIN, GPIO_IN);
    // Read the button state (active low)
    mode += gpio_get(MODE_PIN) ? 0 : 1; // avoid HID mode if errors occured
    mode = (mode > 0) ? 1 : mode;
    printf("Mode selected: %s\n", mode == 0 ? "HID" : "MSC");
    

    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }
    tusb_init();
    printf("Initialized\n");

    
    printf("Starting os\n");
    BaseType_t taskCreated;
    xTaskCreate(led_task, "led", 128, NULL, tskIDLE_PRIORITY + 1, NULL);
    if (mode == 0) { // HID mode
        printf("Enter HID mode\n");

        keyd_queue = xQueueCreate(256, sizeof(key_event_t));
        keyscan_queue = xQueueCreate(16, sizeof(struct device_event));
        keyb_init();

        // parse_matrix_file(&config.matrix);

        xTaskCreate(key_event_hid_task, NULL, MIN_STACK_SZIE, NULL,    tskIDLE_PRIORITY + 3, NULL);
        xTaskCreate(keyscan_task,       NULL, MIN_STACK_SZIE, &config, configMAX_PRIORITIES - 1, NULL);
        xTaskCreate(usb_device_task,    NULL, MIN_STACK_SZIE, NULL,    tskIDLE_PRIORITY + 2, NULL);
        xTaskCreate(keyd_task,          NULL,    8*1024,      NULL,    configMAX_PRIORITIES - 2, NULL);
        // taskCreated = 5xTaskCreate(keyd_task, "keyd", 8*1024, NULL, configMAX_PRIORITIES - 2, NULL);
        // if(taskCreated == pdPASS) {
        //     printf("Keyd task created successfully!\n");
        // } else {
        //     printf("Failed to create keyd task, returned: %d\n", taskCreated);
        // }
    }
    else {
        xTaskCreate(usb_device_task, NULL, TUD_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 3, NULL);
    }

    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}