/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 * © 2021 Giorgi Chavchanidze
*/
 
/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "led/plain.h"

#include "keyboard.h"
#include "vkbd.h"
#include "log.h"
#include "daemon.h"

static struct keyboard *active_kbd;
QueueHandle_t keyd_queue;

static struct vkbd *vkbd;

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

	switch (code) {
		case KEYD_SCROLL_DOWN:
			if (state)
				vkbd_mouse_scroll(vkbd, 0, -1);
			break;
		case KEYD_SCROLL_UP:
			if (state)
				vkbd_mouse_scroll(vkbd, 0, 1);
			break;
		case KEYD_SCROLL_RIGHT:
			if (state)
				vkbd_mouse_scroll(vkbd, 1, 0);
			break;
		case KEYD_SCROLL_LEFT:
			if (state)
				vkbd_mouse_scroll(vkbd, -1, 0);
			break;
		default:
			vkbd_send_key(vkbd, code, state);
			break;
	}
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
			// gpio_led_set_pattern(active_layers ? 0xFFFFFFFFu : 0);

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
	// 	msg("DEVICE: r{removed}\t%s %s\n", ev->dev->id, ev->dev->name);

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
static int evloop(QueueHandle_t keyscan_event_queue, int (*event_handler) (struct event *ev))
{
	size_t i;
	int timeout = 0;
	struct event ev;
	struct device_event devev;
	
	TickType_t xTicksToWait = portMAX_DELAY; // Block indefinitely if not otherwise specified
	dbg3("entering evloop");

	// UBaseType_t keyd_stack_min_free_words = uxTaskGetStackHighWaterMark(NULL);
	// dbg3("keyd_task stack initial free watermark: %u words (%u bytes)",
	//      (unsigned)keyd_stack_min_free_words,
	//      (unsigned)(keyd_stack_min_free_words * sizeof(StackType_t)));
	    while (1) {
	        if (xQueueReceive(keyscan_event_queue, &devev, xTicksToWait) == pdPASS) {
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

			// UBaseType_t keyd_stack_free_words = uxTaskGetStackHighWaterMark(NULL);
			// if (keyd_stack_free_words < keyd_stack_min_free_words) {
			// 	keyd_stack_min_free_words = keyd_stack_free_words;
			// 	dbg3("keyd_task stack new min free watermark: %u words (%u bytes)",
			// 	     (unsigned)keyd_stack_min_free_words,
			// 	     (unsigned)(keyd_stack_min_free_words * sizeof(StackType_t)));
			// }
	    }

	    return 0; // Never reached
	}

static void keyd_init() {
	keyd_queue = xQueueCreate(256, sizeof(key_event_t));
	dbg3("size of struct conf: %u bytes", sizeof(struct config));

	struct output output = {
					.send_key = send_key,
					.on_layer_change = on_layer_change,
				};

	vkbd = vkbd_init("ErgoType");
	active_kbd = new_keyboard(&output);
	// if (active_kbd && active_kbd->config.layer_indicator) {
	// 	gpio_led_set_pattern(0);
	// }
	dbg3("kbd initialized");
	dbg3("free heap size: %u bytes", xPortGetFreeHeapSize());
}

void keyd_task(void *pvParameters) {
	QueueHandle_t keyscan_event_queue = (QueueHandle_t)pvParameters;
	keyd_init();
    evloop(keyscan_event_queue, event_handler);
}
