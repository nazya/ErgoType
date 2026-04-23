#include <stdbool.h>
#include <string.h>

#include "hardware/gpio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "pmw3360.h"
#include "pointer.h"

#define PIN_MOT   7
#define HID_READY_WAIT_TICKS 2

static TaskHandle_t motion_task_handle = NULL;

/*******************************************************************
 * 1.  GPIO + IRQ initialisation  (run once, before scheduler starts)
 ******************************************************************/
static void mot_irq_handler(uint gpio, uint32_t events) {
    if (gpio == PIN_MOT && (events & GPIO_IRQ_EDGE_FALL)) {
        BaseType_t xHPTW = pdFALSE;
        vTaskNotifyGiveFromISR(motion_task_handle, &xHPTW);
        portYIELD_FROM_ISR(xHPTW);
    }
}

static void mot_gpio_init(void) {
    gpio_init(PIN_MOT);
    gpio_set_dir(PIN_MOT, GPIO_IN);
    gpio_pull_up(PIN_MOT);                         // idle HIGH
    gpio_set_irq_enabled_with_callback(
        PIN_MOT, GPIO_IRQ_EDGE_FALL, true, mot_irq_handler);
}

void pointing_motion_irq_init(TaskHandle_t task_handle) {
    motion_task_handle = task_handle;
    mot_gpio_init();
}


// HID report
typedef struct __attribute__ ((packed)) {
    uint8_t buttons;
    int16_t dx;
    int16_t dy;
} hid_report_t;

hid_report_t report;

void pin_init(uint pin) {
    // gpio_init(pin);
    // gpio_set_dir(pin, GPIO_IN);
    // gpio_pull_up(pin);
}

void pins_init(void) {
    // pin_init(PIN_LMB);
    // pin_init(PIN_RMB);
}

void report_init(void) {
    memset(&report, 0, sizeof(report));
}

void pointing_device_task(void *pvParameters)
{
    (void)pvParameters;
    int16_t dx = 5;
    int16_t dy = 3;

    // stdio_init_all();
    // board_init();
    pins_init();
    pmw3360_init();
    report_init();
    // tusb_init();

    pmw3360_set_cpi(800);    

    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Wait until the HID interface is ready
        

        // Reverse direction every 20 ticks
        // if (++count >= 20)
        // {
        //     // dx = -dx;
        //     // dy = -dy;
        //     pmw3360_get_deltas(&dx, &dy);
        //     wheel = -wheel;
        //     pan = -pan;
        //     count = 0;
        // } 
        // tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, dx, dy, wheel, pan);
        pmw3360_get_deltas(&dx, &dy);
        while (!tud_hid_ready()) {
            // tud_task();
            vTaskDelay(HID_READY_WAIT_TICKS);
        }
        tud_hid_mouse_report(REPORT_ID_MOUSE, 0, dx, dy, 0, 0);

        // // Cycle through pressing and releasing 8 buttons one at a time
        // if (pressing)
        // {
        //     buttons = (1 << current_button);
        //     pressing = false;
        // }
        // else
        // {
        //     buttons = 0;
        //     pressing = true;
        //     // current_button = (current_button + 1) % 8;
        // }

        // // Wait for HID ready before sending button report
        // while (!tud_hid_ready()) {
        //     tud_task();
        // }
        // tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, 0, 0, 0, 0);
        // Delay to control speed (5ms between moves)
        // vTaskDelay(pdMS_TO_TICKS(5));
    }
}
