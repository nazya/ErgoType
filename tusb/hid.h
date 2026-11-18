#ifndef ERGOTYPE_TUSB_HID_H
#define ERGOTYPE_TUSB_HID_H

#include <stdint.h>

#include "platform/os.h"

/* keyd output events queued for the USB device task. */
typedef struct {
	uint8_t type;
	uint8_t code;   /* Key code */
	uint8_t state;  /* Key state: 1 for down, 0 for up */
	uint8_t buttons;
	int16_t x;
	int16_t y;
} vkbd_event_t;

extern QueueHandle_t vkbd_event_queue;

/* Called only from TinyUSB's device task, after tud_task(). */
void hid_work(void);

#endif /* ERGOTYPE_TUSB_HID_H */
