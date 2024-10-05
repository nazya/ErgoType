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



// Helper function to convert enum op to string
const char* op_to_string(enum op operation) {
    switch (operation) {
        case OP_KEYSEQUENCE: return "OP_KEYSEQUENCE";
        case OP_ONESHOT: return "OP_ONESHOT";
        case OP_ONESHOTM: return "OP_ONESHOTM";
        case OP_LAYERM: return "OP_LAYERM";
        case OP_SWAP: return "OP_SWAP";
        case OP_SWAPM: return "OP_SWAPM";
        case OP_LAYER: return "OP_LAYER";
        case OP_LAYOUT: return "OP_LAYOUT";
        case OP_CLEAR: return "OP_CLEAR";
        case OP_CLEARM: return "OP_CLEARM";
        case OP_OVERLOAD: return "OP_OVERLOAD";
        case OP_OVERLOAD_TIMEOUT: return "OP_OVERLOAD_TIMEOUT";
        case OP_OVERLOAD_TIMEOUT_TAP: return "OP_OVERLOAD_TIMEOUT_TAP";
        case OP_OVERLOAD_IDLE_TIMEOUT: return "OP_OVERLOAD_IDLE_TIMEOUT";
        case OP_TOGGLE: return "OP_TOGGLE";
        case OP_TOGGLEM: return "OP_TOGGLEM";
        case OP_MACRO: return "OP_MACRO";
        case OP_MACRO2: return "OP_MACRO2";
        case OP_COMMAND: return "OP_COMMAND";
        case OP_TIMEOUT: return "OP_TIMEOUT";
        case OP_SCROLL_TOGGLE: return "OP_SCROLL_TOGGLE";
        case OP_SCROLL: return "OP_SCROLL";
        default: return "UNKNOWN_OP";
    }
}

// Function to print a descriptor
void print_descriptor(const struct descriptor* desc) {
    printf("    Descriptor:\n");
    printf("      Operation: %s\n", op_to_string(desc->op));
    for (int i = 0; i < MAX_DESCRIPTOR_ARGS; i++) {
        printf("      Arg %d: ", i);
        switch (desc->op) {
            case OP_KEYSEQUENCE:
                printf("Code: %u, Mods: %u\n", desc->args[i].code, desc->args[i].mods);
                break;
            case OP_LAYER:
            case OP_LAYERM:
            case OP_SWAP:
            case OP_SWAPM:
            case OP_TOGGLE:
            case OP_TOGGLEM:
                printf("Index: %d\n", desc->args[i].idx);
                break;
            case OP_OVERLOAD_TIMEOUT:
            case OP_OVERLOAD_TIMEOUT_TAP:
            case OP_OVERLOAD_IDLE_TIMEOUT:
            case OP_TIMEOUT:
                printf("Timeout: %u ms\n", desc->args[i].timeout);
                break;
            case OP_ONESHOT:
            case OP_ONESHOTM:
                printf("Size: %u\n", desc->args[i].sz);
                break;
            case OP_SCROLL:
            case OP_SCROLL_TOGGLE:
                printf("Sensitivity: %d\n", desc->args[i].sensitivity);
                break;
            case OP_MACRO:
            case OP_MACRO2:
            case OP_COMMAND:
                printf("Index: %d\n", desc->args[i].idx);
                break;
            default:
                printf("Data: %u\n", desc->args[i].code);
                break;
        }
    }
}

// Function to print a chord
void print_chord(const struct chord* ch) {
    printf("    Chord:\n");
    printf("      Keys: ");
    for (size_t i = 0; i < ch->sz; i++) {
        printf("%u ", ch->keys[i]);
    }
    printf("\n");
    printf("      Number of Keys: %zu\n", ch->sz);
    printf("      Descriptor:\n");
    print_descriptor(&ch->d);
}

// Function to print a layer
void print_layer(const struct layer* lay) {
    printf("  Layer:\n");
    printf("    Name: %s\n", lay->name);
    printf("    Type: ");
    switch (lay->type) {
        case LT_NORMAL: printf("LT_NORMAL\n"); break;
        case LT_LAYOUT: printf("LT_LAYOUT\n"); break;
        case LT_COMPOSITE: printf("LT_COMPOSITE\n"); break;
        default: printf("UNKNOWN_TYPE\n"); break;
    }
    printf("    Mods: %u\n", lay->mods);

    // Print keymap (only non-zero descriptors for brevity)
    printf("    Keymap:\n");
    for (int i = 0; i < 256; i++) {
        if (lay->keymap[i].op != 0) {
            printf("      Keycode %d:\n", i);
            print_descriptor(&lay->keymap[i]);
        }
    }

    // Print chords
    printf("    Chords (Total: %zu):\n", lay->nr_chords);
    for (size_t i = 0; i < lay->nr_chords; i++) {
        print_chord(&lay->chords[i]);
    }

    // Print constituents for composite layers
    if (lay->type == LT_COMPOSITE) {
        printf("    Constituents (Total: %zu): ", lay->nr_constituents);
        for (size_t i = 0; i < lay->nr_constituents; i++) {
            printf("%d ", lay->constituents[i]);
        }
        printf("\n");
    }
}

// Function to print a macro
void print_macro(const struct macro* m) {
    printf("  Macro (Total Entries: %zu):\n", m->sz);
    for (size_t i = 0; i < m->sz; i++) {
        printf("    Entry %zu:\n", i);
        printf("      Type: %s\n", op_to_string(m->entries[i].type));
        printf("      Data: %u\n", m->entries[i].data);
    }
}

// Function to print aliases
void print_aliases(const struct config* cfg) {
    // printf("  Aliases (Total: %zu):\n", MAX_ALIASES);
    // for (size_t i = 0; i < MAX_ALIASES; i++) {
    //     if (cfg->aliases[i][0] != '\0') {
    //         printf("    Alias %zu: %s\n", i, cfg->aliases[i]);
    //     }
    // }
}

// Function to print descriptors
void print_descriptors(const struct config* cfg) {
    printf("  Descriptors (Total: %zu):\n", 10);
    for (size_t i = 0; i < 6; i++) {
        if (cfg->descriptors[i].op != 0) {
            printf("    Descriptor %zu:\n", i);
            print_descriptor(&cfg->descriptors[i]);
        }
    }
}

// Function to print the entire config
void print_config(const struct config* cfg) {
    printf("Config:\n");
    printf("  Number of Layers: %zu\n", cfg->nr_layers);
    for (size_t i = 0; i < cfg->nr_layers; i++) {
        printf("  Layer %zu:\n", i);
        print_layer(&cfg->layers[i]);
    }

    printf("  Number of Descriptors: %zu\n", cfg->nr_descriptors);
    print_descriptors(cfg);

    printf("  Number of Macros: %zu\n", cfg->nr_macros);
    for (size_t i = 0; i < cfg->nr_macros; i++) {
        print_macro(&cfg->macros[i]);
    }

    printf("  Aliases:\n");
    print_aliases(cfg);

    // Print other fields
    printf("  Number of IDs: %zu\n", cfg->nr_ids);
    // printf("  Number of Commands: %zu\n", cfg->nr_commands);
    printf("  Macro Timeout: %ld ms\n", cfg->macro_timeout);
    printf("  Macro Sequence Timeout: %ld ms\n", cfg->macro_sequence_timeout);
    printf("  Macro Repeat Timeout: %ld ms\n", cfg->macro_repeat_timeout);
    printf("  Oneshot Timeout: %ld ms\n", cfg->oneshot_timeout);
    printf("  Overload Tap Timeout: %ld ms\n", cfg->overload_tap_timeout);
    printf("  Chord Interkey Timeout: %ld ms\n", cfg->chord_interkey_timeout);
    printf("  Chord Hold Timeout: %ld ms\n", cfg->chord_hold_timeout);
    printf("  Layer Indicator: %u\n", cfg->layer_indicator);
    printf("  Disable Modifier Guard: %u\n", cfg->disable_modifier_guard);
    printf("  Default Layout: %s\n", cfg->default_layout);
}

void keyb_init() {
	printf("before calloc: %u bytes\n", xPortGetFreeHeapSize());
	printf("size of struct conf: %u bytes\n", sizeof(struct config));

	struct config *config = calloc(1, sizeof(struct config));
	// printf("before parse heap size: %u bytes\n", xPortGetFreeHeapSize());

    config_parse(config);
	// printf("after parse heap size: %u bytes\n", xPortGetFreeHeapSize());

    struct output output = {
					.send_key = send_key,
					.on_layer_change = on_layer_change,
				};

    // static struct keyboard *active_kbd = NULL;
    active_kbd = new_keyboard(config, &output);
	// tasks
	printf("kbd initialized\n");
	print_config(config);
	printf("struct config printed\n");



	

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