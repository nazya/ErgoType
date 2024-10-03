#include "pico/stdlib.h"
#include "tusb.h"
#include "bsp/board.h"
#include "usb_descriptors.h"

#include "config.h"
#include "porting.h"

// #include "FreeRTOS.h"
// #include "task.h"

#define MAX_KEYS 10
typedef struct {
    uint8_t modifier;           // Modifier keys bitmask
    uint8_t reserved;           // Always 0
    uint8_t keys[MAX_KEYS];     // Array of currently pressed keys
} keyboard_report_t;

typedef enum {
    KEY_STATE_IDLE,
    KEY_STATE_PRESSED,
    KEY_STATE_RELEASED
} key_state_t;

keyboard_report_t current_report = {0};

// Function to add a key to the report
void add_key(uint8_t keycode) {
    for (int i = 0; i < MAX_KEYS; i++) {
        if (current_report.keys[i] == 0) {
            current_report.keys[i] = keycode;
            break;
        }
    }
}

// Function to remove a key from the report
void remove_key(uint8_t keycode) {
    for (int i = 0; i < MAX_KEYS; i++) {
        if (current_report.keys[i] == keycode) {
            current_report.keys[i] = 0;
            break;
        }
    }
}

// Function to set a modifier key
void set_modifier(uint8_t modifier_bit, bool pressed) {
    if (pressed) {
        current_report.modifier |= modifier_bit;
    } else {
        current_report.modifier &= ~modifier_bit;
    }
}


//--------------------------------------------------------------------+
// HID Task
//--------------------------------------------------------------------+

void hid_task(void)
{
    // Example: Press 'A' and 'B', then release 'A' after some time
    static uint32_t last_time = 0;
    static key_state_t state = KEY_STATE_IDLE;

    uint32_t current_time = board_millis();

    if (current_time - last_time >= 1000)
    {
        last_time = current_time;

        switch (state)
        {
            case KEY_STATE_IDLE:
                // Press 'A' and 'B'
                add_key(HID_KEY_A);
                add_key(HID_KEY_B);
                printf("HID: Pressed 'A + B'\r\n");
                tud_hid_keyboard_report(REPORT_ID_KEYBOARD, current_report.modifier, current_report.keys);
                state = KEY_STATE_PRESSED;
                break;

            case KEY_STATE_PRESSED:
                // Release only 'A'
                remove_key(HID_KEY_A);
                printf("HID: Released 'A'\r\n");
                tud_hid_keyboard_report(REPORT_ID_KEYBOARD, current_report.modifier, current_report.keys);
                state = KEY_STATE_RELEASED;
                break;

            case KEY_STATE_RELEASED:
                // Release 'B'
                remove_key(HID_KEY_B);
                printf("HID: Released 'B'\r\n");
                tud_hid_keyboard_report(REPORT_ID_KEYBOARD, current_report.modifier, current_report.keys);
                state = KEY_STATE_IDLE;
                break;

            default:
                state = KEY_STATE_IDLE;
                break;
        }
    }
}

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) instance;
    (void) report;
    (void) len;
}

// Invoked when received GET_REPORT control request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    // Not implemented
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// Invoked when received SET_REPORT control request or data on OUT endpoint
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    // Not implemented
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

void hid_app_task(void* pvParameters) {
    (void) pvParameters;

    // keyb_init();


    while (1) {
        hid_task();
        // vTaskDelay(pdMS_TO_TICKS(10));
    }
}


