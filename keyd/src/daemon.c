#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <time.h>   // For time(

// tmp REMOVE IT
#include "keyboard.h"
#include "porting.h"
#include "daemon.h"

static void send_key(uint8_t code, uint8_t state)
{
	keystate[code] = state;
	// printf("keyd out: %u %u\n", code, state);
	dbg("keyd out %s %s\n", KEY_NAME(code), state ? "down" : "up");

	// vkbd_send_key(vkbd, code, state);
}

static void on_layer_change(const struct keyboard *kbd, const struct layer *layer, uint8_t state)
{
	// size_t i;
	// char buf[MAX_LAYER_NAME_LEN+2];
	// ssize_t bufsz;

	// int keep[ARRAY_SIZE(listeners)];
	// size_t n = 0;

	// if (kbd->config.layer_indicator) {
	// 	int active_layers = 0;

	// 	for (i = 1; i < kbd->config.nr_layers; i++)
	// 		if (kbd->config.layers[i].type != LT_LAYOUT && kbd->layer_state[i].active) {
	// 			active_layers = 1;
	// 			break;
	// 		}

	// 	for (i = 0; i < device_table_sz; i++)
	// 		if (device_table[i].data == kbd)
	// 			device_set_led(&device_table[i], 1, active_layers);
	// }

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

	timeout -= ev->timestamp - last_time;
	last_time = ev->timestamp;

	timeout = timeout < 0 ? 0 : timeout;

	switch (ev->type) {
	case EV_TIMEOUT:
		if (!active_kbd)
			return 0;

		kev.code = 0;
		kev.timestamp = ev->timestamp;

		timeout = kbd_process_events(active_kbd, &kev, 1);
		break;
	case EV_DEV_EVENT:
		// if (ev->dev->data) {
		if (true) {
			// struct keyboard *kbd = ev->dev->data;
			// active_kbd = ev->dev->data; # now it is the only active kbd
			struct keyboard *kbd = active_kbd;
			if (!kbd) {
				printf("no kbd present\n");

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

void generate_random_key_presses() {
    struct event ev;
    int key;
    struct device_event devev; // Local struct to hold event details

	// eventQueue = xQueueCreate(10, sizeof(struct event));

    srand(time(NULL)); // Seed the random number generator
	printf("Entering keyssym loop\n");
    while (1) {
        // Generate a random key code between 'A' (65) and 'Z' (90)
        key = rand() % (4) + 2;

        // Setup the device event
        devev.type = DEV_KEY;
        devev.code = key;
        devev.pressed = 1; // Key down

        // Prepare the main event structure
        ev.type = EV_DEV_EVENT;
        ev.devev = &devev; // Assign the address of devev to ev.devev
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Convert ticks to milliseconds

        // Send event to the queue
		// printf("keyd in: %u %u\n", key, 1);
		dbg("sym ev %s %s", KEY_NAME(key), 1 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);

        // Delay to simulate the time between key presses
        vTaskDelay(pdMS_TO_TICKS(100));

        // Change to key up event
        devev.pressed = 0; // Key up
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Update timestamp for key up event

        // Send the key up event to the queue
		dbg("sym ev %s %s", KEY_NAME(key), 0 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);


        // Random delay between 1000 ms and 2000 ms before next key press
        vTaskDelay(pdMS_TO_TICKS(1000));

		//simulate chord
		//simulate chord
		//simulate chord
		//simulate chord
		//simulate chord
		// Setup the device event
        devev.type = DEV_KEY;
        devev.code = 1;
        devev.pressed = 1; // Key down

        ev.type = EV_DEV_EVENT;
        ev.devev = &devev; // Assign the address of devev to ev.devev
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Convert ticks to milliseconds

		dbg("sym ev %s %s", KEY_NAME(key), 1 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);


		devev.type = DEV_KEY;
        devev.code = 2;
        devev.pressed = 1; // Key down

        ev.type = EV_DEV_EVENT;
        ev.devev = &devev; // Assign the address of devev to ev.devev
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Convert ticks to milliseconds

		dbg("sym ev %s %s", KEY_NAME(key), 1 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);


		devev.type = DEV_KEY;
        devev.code = 1;
        devev.pressed = 0; // Key down

        ev.type = EV_DEV_EVENT;
        ev.devev = &devev; // Assign the address of devev to ev.devev
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Convert ticks to milliseconds

		dbg("sym ev %s %s", KEY_NAME(key), 1 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);


		devev.type = DEV_KEY;
        devev.code = 2;
        devev.pressed = 0; // Key down

        ev.type = EV_DEV_EVENT;
        ev.devev = &devev; // Assign the address of devev to ev.devev
        ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS; // Convert ticks to milliseconds

		dbg("sym ev %s %s", KEY_NAME(key), 1 ? "down" : "up");
        xQueueSend(eventQueue, &ev, portMAX_DELAY);


    }
}

int evloop(int (*event_handler) (struct event *ev))
{
	size_t i;
	int timeout = 0;

	struct event ev;

	TickType_t xTicksToWait = portMAX_DELAY; // Block indefinitely if not otherwise specified
	printf("Entering evloop\n");
    while (1) {
        if (xQueueReceive(eventQueue, &ev, xTicksToWait) == pdPASS) {
			// printf("Read from queue\n");

            // Handle the event
            timeout = event_handler(&ev);
            xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
        } else {
			// printf("Handling timeout\n");

            // Timeout occurred
            ev.type = EV_TIMEOUT;
			// ev.dev = NULL;
			ev.devev = NULL;
            int 
			timeout = event_handler(&ev);
            xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
        }
    }

    return 0; // Never reached
}

void keyb_init() {
	printf("size of struct conf: %u bytes\n", sizeof(struct config));

	struct output output = {
					.send_key = send_key,
					.on_layer_change = on_layer_change,
				};

    active_kbd = new_keyboard(&output);
	printf("kbd initialized. heap size: %u bytes\n", xPortGetFreeHeapSize());

	// active_kbd->config = calloc(1, sizeof(struct config));
    config_parse(&active_kbd->config);

    
}

void keys_task(void* pvParameters) {
    (void) pvParameters;
    generate_random_key_presses();
}

void keyd_task(void* pvParameters) {
    (void) pvParameters;
	// printf("before init heap size: %u bytes\n", xPortGetFreeHeapSize());
	// printf("after keyd init heap size: %u bytes\n", xPortGetFreeHeapSize());
    evloop(event_handler);
}