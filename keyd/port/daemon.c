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

#include "keyboard.h"
#include "vkbd.h"
#include "log.h"
#include "keyd.h"

static struct keyboard *active_kbd;

static struct vkbd *vkbd;
static uint8_t keystate[256];

static void free_config(struct config *config)
{
	size_t i;

	for (i = 0; i < config->nr_layers; i++) {
		struct layer *layer = &config->layers[i];

		for (size_t j = 0; j < ARRAY_SIZE(layer->keymap); j++)
			vPortFree(layer->keymap[j]);

		vPortFree(layer->chords);
	}

	for (i = 0; i < config->nr_macros; i++)
		vPortFree(config->macros[i]);

	for (i = 0; i < ARRAY_SIZE(config->aliases); i++)
		vPortFree(config->aliases[i]);

	vPortFree(config);
}

static void free_configs(void)
{
	if (active_kbd) {
		free_config(active_kbd->config);
		vPortFree(active_kbd);
		active_kbd = NULL;
	}
}

static void cleanup(void)
{
	free_configs();
	free_vkbd(vkbd);
}

static void clear_vkbd(void)
{
	size_t i;

	for (i = 0; i < 256; i++)
		if (keystate[i]) {
			vkbd_send_key(vkbd, i, 0);
			keystate[i] = 0;
		}
}

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

static void send_key_macro_wrapper(void *ctx, uint8_t code, uint8_t state)
{
        send_key(code, state);
}


static void on_layer_change(const struct keyboard *kbd, const struct layer *layer, uint8_t state)
{
	size_t i;
	char buf[MAX_LAYER_NAME_LEN+2];
	ssize_t bufsz;

	// int keep[ARRAY_SIZE(listeners)];
	size_t n = 0;

	if (kbd->config->layer_indicator) {
		int active_layers = 0;

		for (i = 1; i < kbd->config->nr_layers; i++)
			if (kbd->config->layers[i].type != LT_LAYOUT && kbd->layer_state[i].active) {
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

	if (layer->type == LT_LAYOUT) {
		bufsz = snprintf(buf, sizeof(buf), "/%s\n", layer->name);
		on_layout_change(layer->name);
	} else {
		bufsz = snprintf(buf, sizeof(buf), "%c%s\n", state ? '+' : '-', layer->name);
		if (layer == &kbd->config->layers[0])
			on_layout_change(layer->name);
	}

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

static const char *active_layout_name(const struct keyboard *kbd)
{
	for (size_t i = 0; i < kbd->config->nr_layers; i++)
		if (kbd->config->layers[i].type == LT_LAYOUT && kbd->layer_state[i].active)
			return kbd->config->layers[i].name;

	return kbd->config->layers[0].name;
}

static int process_keypress(struct keyboard *kbd, uint8_t code, int timestamp)
{
        struct key_event kev = {
                .code = code,
                .pressed = 1,
                .timestamp = timestamp
        };

        kbd_process_events(kbd, &kev, 1);

        kev.pressed = 0;
        return kbd_process_events(kbd, &kev, 1);
}

static int event_handler(struct event *ev)
{
	static int last_time = 0;
	static int timeout = 0;
	struct key_event kev = {0};

	// FreeRTOS evloop owns timeout waiting and emits EV_TIMEOUT after xQueueReceive times out.
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
		if (active_kbd) { // active_kbd = ev->dev->data; # now it is the only active kbd
			// struct keyboard *kbd = ev->dev->data;
			struct keyboard *kbd = active_kbd;
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
				if (kbd->scroll.active) {
					if (kbd->scroll.sensitivity == 0)
						break;
					int xticks, yticks;

					kbd->scroll.y += ev->devev->y;
					kbd->scroll.x += ev->devev->x;

					yticks = kbd->scroll.y / kbd->scroll.sensitivity;
					kbd->scroll.y %= kbd->scroll.sensitivity;

					xticks = kbd->scroll.x / kbd->scroll.sensitivity;
					kbd->scroll.x %= kbd->scroll.sensitivity;

					vkbd_mouse_scroll(vkbd, 0, -1*yticks);
					vkbd_mouse_scroll(vkbd, 0, xticks);
				} else {
					vkbd_mouse_move(vkbd, ev->devev->x, ev->devev->y);
				}
				break;
			case DEV_MOUSE_MOVE_ABS:
				vkbd_mouse_move_abs(vkbd, ev->devev->x, ev->devev->y);
				break;
			case DEV_MOUSE_SCROLL:
				if (active_kbd) {
					if (ev->devev->x > 0)
						for (i = 0;i < (size_t)ev->devev->x; i++)
							timeout = process_keypress(active_kbd, KEYD_SCROLL_RIGHT, ev->timestamp);
					if (ev->devev->x < 0)
						for (i = 0;i < (size_t)-1*ev->devev->x; i++)
							timeout = process_keypress(active_kbd, KEYD_SCROLL_LEFT, ev->timestamp);
					if (ev->devev->y > 0)
						for (i = 0;i < (size_t)ev->devev->y; i++)
							timeout = process_keypress(active_kbd, KEYD_SCROLL_UP, ev->timestamp);
					if (ev->devev->y < 0)
						for (i = 0;i < (size_t)-1*ev->devev->y; i++)
							timeout = process_keypress(active_kbd, KEYD_SCROLL_DOWN, ev->timestamp);
				}
				break;
			default:
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

static void load_configs(void)
{
	struct config *config = pvPortMalloc(sizeof *config);

	if (!config) {
		dbg("size of struct conf: %u bytes", sizeof(struct config));
		err("pvPortMalloc failed for struct config");
		return;
	}

	if (config_parse(config) < 0) {
		warn("failed to parse keyd config");
		free_config(config);
		return;
	}

	struct output output = {
		.send_key = send_key,
		.on_layer_change = on_layer_change,
	};

	if (keyd_overlay_conf) {
		config_parse_file(config, keyd_overlay_conf);
	}

	active_kbd = new_keyboard(config, &output);
	if (!active_kbd) {
		free_config(config);
		return;
	}

	on_layout_change(active_layout_name(active_kbd));
	dbg("kbd initialized");
}

static void reload(void)
{
	// size_t i;
	
	free_configs();
	load_configs();

	dbg2("free heap size: %u bytes", xPortGetFreeHeapSize());

	// for (i = 0; i < device_table_sz; i++)
	//	manage_device(&device_table[i]);

	clear_vkbd();
}

int run_daemon(void)
{
	vkbd = vkbd_init("");

	reload();

	msg("Starting keyd");
	evloop(event_handler);
	return 0;
}
