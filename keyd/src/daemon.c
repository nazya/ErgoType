#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "tusb.h"
#include "usb_descriptors.h"

#include "keyboard.h"
#include "log.h"
#include "daemon.h"

// HID Keyboard Report

static uint8_t mods = 0;
static uint8_t keys[6] = {0};

static void send_hid_report()
{
	struct hid_report {
		uint8_t hid_mods;
		uint8_t reserved;
		uint8_t hid_code[6];
	};
	struct hid_report report;

	for (int i = 0; i < 6; i++)
		report.hid_code[i] = keys[i];

	report.hid_mods = mods;

	while (!tud_hid_ready()) {
		// Handle USB background tasks
		tud_task();
	}
	tud_hid_keyboard_report(REPORT_ID_KEYBOARD, report.hid_mods, report.hid_code);
}

static uint8_t get_modifier(int code)
{
	switch (code) {
	case KEYD_LEFTSHIFT:
		return HID_SHIFT;
		break;
	case KEYD_RIGHTSHIFT:
		return HID_RIGHTSHIFT;
		break;
	case KEYD_LEFTCTRL:
		return HID_CTRL;
		break;
	case KEYD_RIGHTCTRL:
		return HID_RIGHTCTRL;
		break;
	case KEYD_LEFTALT:
		return HID_ALT;
		break;
	case KEYD_RIGHTALT:
		return HID_ALT_GR;
		break;
	case KEYD_LEFTMETA:
		return HID_SUPER;
		break;
	case KEYD_RIGHTMETA:
		return HID_RIGHTSUPER;
		break;
	default:
		return 0;
		break;
	}
}

static void update_key_state(uint16_t code, int state)
{
	int i;
	int set = 0;
	uint8_t hid_code = hid_table[code];

	for (i = 0; i < 6; i++) {
		if (keys[i] == hid_code) {
			set = 1;
			if (state == 0)
				keys[i] = 0;
		}
	}
	if (state && !set) {
		for (i = 0; i < 6; i++) {
			if (keys[i] == 0) {
				keys[i] = hid_code;
				break;
			}
		}
	}
}

static int update_modifier_state(int code, int state)
{
	uint16_t mod = get_modifier(code);

	if (mod) {
		if (state)
			mods |= mod;
		else
			mods &= ~mod;
		return 0;
	}

	return -1;
}

void vkbd_send_key(uint8_t code, int state)
{
	if (update_modifier_state(code, state) < 0)
		update_key_state(code, state);

	send_hid_report();
}

void key_event_hid_task(void *pvParameters)
{
    key_event_t event;

    while(1) {
        // Wait indefinitely for a key event
        if (xQueueReceive(keyd_queue, &event, portMAX_DELAY) == pdPASS) {
			vkbd_send_key(event.code, event.state);
        }
    }
}

// static void clear_hid_state() {
// 	size_t i;
// 	key_event_t event;

// 	for (i = 0; i < 256; i++)
// 		if (keystate[i]) {
// 			keystate[i] = 0;
// 			event.code = i;
// 			event.state = 0;
// 			xQueueSendToBack(key_event_queue, &event, portMAX_DELAY);
// 		}
// }

static void send_key(uint8_t code, uint8_t state)
{
	keystate[code] = state;

	key_event_t event;
    event.code = code;
    event.state = state;

    // Enqueue the key event
    if (xQueueSendToBack(keyd_queue, &event, pdMS_TO_TICKS(3)) != pdPASS) {
        // Handle queue full condition if necessary
        dbg("Failed to enqueue key event (queue full)");
    }
	// else {
	// 	clear_hid_state();
	// }
}

static void on_layer_change(const struct keyboard *kbd, const struct layer *layer, uint8_t state)
{
	size_t i;
	char buf[MAX_LAYER_NAME_LEN+2];
	ssize_t bufsz;

	// int keep[ARRAY_SIZE(listeners)];
	size_t n = 0;

	if (kbd->config.layer_indicator) {
		int active_layers = 0;

		for (i = 1; i < kbd->config.nr_layers; i++)
			if (kbd->config.layers[i].type != LT_LAYOUT && kbd->layer_state[i].active) {
				active_layers = 1;
				break;
			}

		// for (i = 0; i < device_table_sz; i++)
		// 	if (device_table[i].data == kbd){
		// 		device_set_led(&device_table[i], 1, active_layers);
		// 	}
	}

	// if (!nr_listeners)
	// 	return;

	// if (layer->type == LT_LAYOUT)
	// 	bufsz = snprintf(buf, sizeof(buf), "/%s\n", layer->name);
	// else
	// 	bufsz = snprintf(buf, sizeof(buf), "%c%s\n", state ? '+' : '-', layer->name);

	// for (i = 0; i < nr_listeners; i++) {
	// 	ssize_t nw = write(listeners[i], buf, bufsz);

	// 	if (nw == bufsz)
	// 		keep[n++] = listeners[i];
	// 	else
	// 		close(listeners[i]);
	// }

	// if (n != nr_listeners) {
	// 	nr_listeners = n;
	// 	memcpy(listeners, keep, n * sizeof(int));
	// }
}


void keyb_init() {
	dbg3("size of struct conf: %u bytes", sizeof(struct config));

	struct output output = {
					.send_key = send_key,
					.on_layer_change = on_layer_change,
				};

    active_kbd = new_keyboard(&output);
	dbg3("kbd initialized");
	dbg3("free heap size: %u bytes", xPortGetFreeHeapSize());
}

static int event_handler(struct event *ev)
{
	static int last_time = 0;
	static int timeout = 0;
	struct key_event kev = {0};

	// timeout -= ev->timestamp - last_time;
	// last_time = ev->timestamp;

	// timeout = timeout < 0 ? 0 : timeout;
	
	switch (ev->type) {
	case EV_TIMEOUT:
		if (!active_kbd)
			return 0;

		kev.code = 0;
		kev.timestamp = ev->timestamp;

		timeout = kbd_process_events(active_kbd, &kev, 1);
		dbg3("input ev timeout");
		break;
	case EV_DEV_EVENT:
		// if (ev->dev->data) {
		if (true) {
			// struct keyboard *kbd = ev->dev->data;
			// active_kbd = ev->dev->data; # now it is the only active kbd
			struct keyboard *kbd = active_kbd;
			if (!kbd) {
				dbg("no kbd set");
				return 0;
			}
			switch (ev->devev->type) {
			size_t i;
			case DEV_KEY:
				dbg("input %s %s", KEY_NAME(ev->devev->code), ev->devev->pressed ? "down" : "up");

				kev.code = ev->devev->code;
				kev.pressed = ev->devev->pressed;
				kev.timestamp = ev->timestamp;

				timeout = kbd_process_events(kbd, &kev, 1);
				break;
			case DEV_MOUSE_MOVE:
				// if (kbd->scroll.active) {
				// 	if (kbd->scroll.sensitivity == 0)
				// 		break;
				// 	int xticks, yticks;

				// 	kbd->scroll.y += ev->devev->y;
				// 	kbd->scroll.x += ev->devev->x;

				// 	yticks = kbd->scroll.y / kbd->scroll.sensitivity;
				// 	kbd->scroll.y %= kbd->scroll.sensitivity;

				// 	xticks = kbd->scroll.x / kbd->scroll.sensitivity;
				// 	kbd->scroll.x %= kbd->scroll.sensitivity;

				// 	vkbd_mouse_scroll(vkbd, 0, -1*yticks);
				// 	vkbd_mouse_scroll(vkbd, 0, xticks);
				// } else {
				// 	vkbd_mouse_move(vkbd, ev->devev->x, ev->devev->y);
				// }
				break;
			case DEV_MOUSE_MOVE_ABS:
				// vkbd_mouse_move_abs(vkbd, ev->devev->x, ev->devev->y);
				break;
			default:
				break;
			case DEV_MOUSE_SCROLL:
				// /*
				//  * Treat scroll events as mouse buttons so oneshot and the like get
				//  * cleared.
				//  */
				// if (active_kbd) {
				// 	kev.code = KEYD_EXTERNAL_MOUSE_BUTTON;
				// 	kev.pressed = 1;
				// 	kev.timestamp = ev->timestamp;

				// 	kbd_process_events(ev->dev->data, &kev, 1);

				// 	kev.pressed = 0;
				// 	timeout = kbd_process_events(ev->dev->data, &kev, 1);
				// }

				// vkbd_mouse_scroll(vkbd, ev->devev->x, ev->devev->y);
				break;
			}
		// } else if (ev->dev->is_virtual && ev->devev->type == DEV_LED) {
		} else if (ev->devev->type == DEV_LED) {
			// size_t i;

			// /* 
			//  * Propagate LED events received by the virtual device from userspace
			//  * to all grabbed devices.
			//  *
			//  * NOTE/TODO: Account for potential layer_indicator interference
			//  */
			// for (i = 0; i < device_table_sz; i++)
			// 	if (device_table[i].data)
			// 		device_set_led(&device_table[i], ev->devev->code, ev->devev->pressed);
		}

		break;
	// case EV_DEV_ADD:
	// 	manage_device(ev->dev);
	// 	break;
	// case EV_DEV_REMOVE:
	// 	keyd_log("DEVICE: r{removed}\t%s %s\n", ev->dev->id, ev->dev->name);

	// 	break;
	// case EV_FD_ACTIVITY:
	// 	if (ev->fd == ipcfd) {
	// 		int con = accept(ipcfd, NULL, 0);
	// 		if (con < 0) {
	// 			perror("accept");
	// 			exit(-1);
	// 		}

	// 		handle_client(con);
	// 	}
	// 	break;
	default:
		break;
	}
	return timeout;
}
int evloop(int (*event_handler) (struct event *ev))
{
	size_t i;
	int timeout = 0;

	struct event ev;

	struct device_event devev;


	TickType_t xTicksToWait = portMAX_DELAY; // Block indefinitely if not otherwise specified
	dbg3("entering evloop");
    while (1) {
        if (xQueueReceive(keyscan_queue, &devev, xTicksToWait) == pdPASS) {
			ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
			ev.type = EV_DEV_EVENT;
			ev.devev = &devev;
            timeout = event_handler(&ev);
			timeout = timeout < 0 ? 0 : timeout;
            xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
        } else {
            // Timeout occurred
            ev.type = EV_TIMEOUT;
			ev.devev = NULL;
			ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
            timeout = event_handler(&ev);
			timeout = timeout < 0 ? 0 : timeout;
            xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
        }
    }

    return 0; // Never reached
}
void keyd_task(void* pvParameters) {
    (void) pvParameters;
    evloop(event_handler);
}