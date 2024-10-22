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
#include "porting.h"
#include "daemon.h"

// #include "quantum/keyboard.h"
// #include "quantum/matrix.h"
#include "matrix.h"



//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
#define MODE_PIN 17 // GPIO18 for mode selection
#define ERASE_PIN 18 // GPIO18 for mode selection
#define CONFIG_TASK_PRIORITY (configMAX_PRIORITIES - 2)
#define CONFIG_TASK_STACK_SIZE (configMINIMAL_STACK_SIZE * 4)


#define TUD_TASK_STACK_SIZE 16384 // flash_fat_write requires 4096 bytes
#define HID_STACK_SZIE      configMINIMAL_STACK_SIZE

// Mode selection variable
uint32_t mode = 1; // 0: HID mode, 1: MSC mode
QueueHandle_t eventQueue;
QueueHandle_t key_event_queue;
struct keyboard *active_kbd;

// FreeRTOS tasks
void led_task(void* pvParameters); // led.c
void hid_app_task(void* pvParameters); // hid.c
void msc_app_task(void* pvParameters); // msc.c
void usb_device_task(void* pvParameters); // tud.c
void matrix_task(void* pvParameters); // keyscan.c

int main() {

    mode = 1;
    board_init();
    stdio_init_all();


    // Read mode selection button
    gpio_init(MODE_PIN);
    gpio_pull_up(MODE_PIN);
    gpio_set_dir(MODE_PIN, GPIO_IN);

    gpio_init(ERASE_PIN);
    gpio_pull_up(ERASE_PIN);
    gpio_set_dir(ERASE_PIN, GPIO_IN);

    // Read the button state (active low)
    mode = gpio_get(MODE_PIN) ? 0 : 1;

    int erase = gpio_get(ERASE_PIN) ? 0 : 1;
    
    if (erase) {
        printf("Filesystem reinit\n");
        flash_fat_initialize();
        printf("Filesystem reinited\n");

        while (1) { };
    }
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

        eventQueue = xQueueCreate(16, sizeof(struct event));
        key_event_queue = xQueueCreate(256, sizeof(key_event_t));
        keyb_init();

        matrix_t matrix;
        parse_matrix_file(&matrix);

        xTaskCreate(key_event_processor_task, "KeyProcessor", HID_STACK_SZIE, NULL, tskIDLE_PRIORITY + 3, NULL);
        xTaskCreate(matrix_task, "MT", 1024, &matrix, configMAX_PRIORITIES - 1, NULL);
        xTaskCreate(usb_device_task, "tud", HID_STACK_SZIE, NULL, tskIDLE_PRIORITY + 2, NULL);

        taskCreated = xTaskCreate(keyd_task, "keyd", 8*1024, NULL, configMAX_PRIORITIES - 2, NULL);
        if(taskCreated == pdPASS) {
            printf("Keyd task created successfully!\n");
        } else {
            printf("Failed to create keyd task, returned: %d\n", taskCreated);
        }
    }
    else {
        xTaskCreate(usb_device_task, "usb_device_task", TUD_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 3, NULL);
    }

    vTaskStartScheduler(); // block thread and pass control to FreeRTOS

    while (1) { };
    return 0;
}