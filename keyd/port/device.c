#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"

#include "device.h"
#include "keys.h"
#include "log.h"
#include <linux/types.h>
#include <uapi/linux/input.h>

struct device *device_table[MAX_DEVICES];
size_t device_table_sz;

// Standard HID Haptics Page waveform usages; this const table stays in flash.
static const uint16_t haptic_waveforms[HAPTIC_EFFECT_COUNT] = {
	[HAPTIC_EFFECT_CLICK] = 0x1003,
	[HAPTIC_EFFECT_BUZZ] = 0x1004,
	[HAPTIC_EFFECT_RUMBLE] = 0x1005,
	[HAPTIC_EFFECT_PRESS] = 0x1006,
	[HAPTIC_EFFECT_RELEASE] = 0x1007,
};

struct haptic_state {
	// Linux FF effect IDs are allocated independently for each input device.
	int16_t effect_ids[HAPTIC_EFFECT_COUNT];
};

static uint8_t resolve_device_capabilities(const struct port_input_dev *port_dev,
					   uint32_t *num_keys,
					   int *has_rel,
					   int *has_abs)
{
	const uint32_t keyboard_mask = 1<<KEY_1  | 1<<KEY_2 | 1<<KEY_3 |
					1<<KEY_4 | 1<<KEY_5 | 1<<KEY_6 |
					1<<KEY_7 | 1<<KEY_8 | 1<<KEY_9 |
					1<<KEY_0 | 1<<KEY_Q | 1<<KEY_W |
					1<<KEY_E | 1<<KEY_R | 1<<KEY_T |
					1<<KEY_Y;
	const unsigned long *mask = port_dev->keybit;
	uint8_t capabilities = 0;
	int has_brightness_key;

	// if (ioctl(fd, EVIOCGBIT(EV_KEY, (BTN_LEFT/32+1)*4), mask) < 0) {
	// 	perror("ioctl: ev_key");
	// 	return 0;
	// }
	// Port: the firmware input producer supplied an EV_KEY bitmap snapshot.

	// if (ioctl(fd, EVIOCGBIT(EV_REL, 1), relmask) < 0) {
	// 	perror("ioctl: ev_rel");
	// 	return 0;
	// }
	// Port: the firmware input producer supplied an EV_REL bitmap snapshot.

	// if (ioctl(fd, EVIOCGBIT(EV_ABS, 1), absmask) < 0) {
	// 	perror("ioctl: ev_abs");
	// 	return 0;
	// }
	// Port: the firmware input producer supplied an EV_ABS bitmap snapshot.

	// *num_keys = 0;
	// for (i = 0; i < sizeof(mask)/sizeof(mask[0]); i++)
	// 	*num_keys += __builtin_popcount(mask[i]);
	// Port: count/read the supplied snapshots here where upstream keyd does ioctl.
	*num_keys = 0;
	for (unsigned int i = 0; i < INPUT_BITS_TO_LONGS(KEY_CNT); i++)
		*num_keys += __builtin_popcountl(port_dev->keybit[i]);
	*has_rel = 0;
	for (unsigned int i = 0; i < INPUT_BITS_TO_LONGS(REL_CNT); i++)
		*has_rel |= port_dev->relbit[i] != 0;
	*has_abs = 0;
	for (unsigned int i = 0; i < INPUT_BITS_TO_LONGS(ABS_CNT); i++)
		*has_abs |= port_dev->absbit[i] != 0;

	if (*num_keys)
		capabilities |= CAP_KEY;

	if (*has_rel || *has_abs)
		capabilities |= CAP_MOUSE;

	if (*has_abs)
		capabilities |= CAP_MOUSE_ABS;

	/*
	 * If the device can emit KEY_BRIGHTNESSUP we treat it as a keyboard.
	 *
	 * This is mainly to accommodate laptops with brightness buttons which create
	 * a different device node from the main keyboard for some hotkeys.
	 *
	 * NOTE: This will subsume anything that can emit a brightness key and may produce
	 * false positives which need to be explcitly excluded by the user if they use
	 * the wildcard id.
	 */
	has_brightness_key = mask[KEY_BRIGHTNESSUP/32] &
			     (1 << (KEY_BRIGHTNESSUP % 32));

	if (((mask[0] & keyboard_mask) == keyboard_mask) || has_brightness_key)
		capabilities |= CAP_KEYBOARD;

	return capabilities;
}

int device_init(const struct port_input_dev *port_dev, struct device *dev)
{
	uint32_t num_keys;
	int has_rel;
	int has_abs;
	uint8_t capabilities;

	memset(dev, 0, sizeof *dev);

	capabilities = resolve_device_capabilities(port_dev, &num_keys, &has_rel, &has_abs);

	// if (ioctl(fd, EVIOCGNAME(sizeof(dev->name)), dev->name) == -1) {
	// 	keyd_log("ERROR: could not fetch device name of %s\n", dev->path);
	// 	return -1;
	// }
	// Port: no input fd/name ioctl exists; use the stable firmware VID:PID id
	// as the visible device name until USB string descriptors are wired.
	snprintf(dev->id, sizeof(dev->id), "%04x:%04x", port_dev->vendor, port_dev->product);
	snprintf(dev->name, sizeof(dev->name), "%s", dev->id);

	if (!capabilities)
		return -EINVAL;

	if (capabilities & CAP_MOUSE_ABS) {
	// if (ioctl(fd, EVIOCGABS(ABS_X), &absinfo) < 0) {
	// 	perror("ioctl");
	// 	return -1;
	// }
	// Port: firmware input producer already copied ABS_X range.
		dev->_minx = port_dev->abs_x.minimum;
		dev->_maxx = port_dev->abs_x.maximum;

	// if (ioctl(fd, EVIOCGABS(ABS_Y), &absinfo) < 0) {
	// 	perror("ioctl");
	// 	return -1;
	// }
	// Port: firmware input producer already copied ABS_Y range.
		dev->_miny = port_dev->abs_y.minimum;
		dev->_maxy = port_dev->abs_y.maximum;
	}

	// dev->capabilities = capabilities;
	// Port: same assignment, after fd-less ioctl replacement above.
	dev->capabilities = capabilities;
	dev->data = NULL;
	dev->ev_queue = port_dev->ev_queue;
	dev->writer = port_dev->writer;
	if (port_dev->name)
		snprintf(dev->name, sizeof(dev->name), "%s", port_dev->name);
	if (port_dev->has_haptic)
		haptic_init(dev);

	return 0;
}

struct device_event *device_read_event(struct device *dev)
{
	// struct input_event ev;
	// Firmware queues store timestamp-free port events instead of Linux events.
	struct port_input_event ev;
	static struct device_event devev;

	// assert(dev->fd != -1);

	// if (read(dev->fd, &ev, sizeof(ev)) < 0) {
	// 	if (errno == EAGAIN) {
	// 		return NULL;
	// 	} else {
	// 		dev->fd = -1;
	// 		devev.type = DEV_REMOVED;
	// 		return &devev;
	// 	}
	// }
	// Port: dev->ev_queue replaces the upstream input fd. The queued
	// struct port_input_event is the firmware subset documented in devmon.h;
	// removal/reset are explicit sentinel events because there is no read()
	// error path.
	// if (xQueueReceive(dev->ev_queue, &devev, 0) != pdPASS)
	// 	return NULL;
	if (xQueueReceive(dev->ev_queue, &ev, 0) != pdPASS)
		return NULL;

	switch (ev.type) {
	case DEVICE_INPUT_REMOVED:
		devev.type = DEV_REMOVED;
		return &devev;
	case DEVICE_INPUT_RESET:
		devev.type = DEV_RESET;
		return &devev;
	case EV_REL:
		switch (ev.code) {
		case REL_WHEEL:
			devev.type = DEV_MOUSE_SCROLL;
			devev.y = ev.value;
			devev.x = 0;

			break;
		case REL_HWHEEL:
			devev.type = DEV_MOUSE_SCROLL;
			devev.y = 0;
			devev.x = ev.value;

			break;
		case REL_X:
			/*
			 * Queue and emit a single event on SYN to account for
			 * programs which are particular about input grouping.
			 */
			dev->_pending_rel_x += ev.value;

			return NULL;
			break;
		case REL_Y:
			dev->_pending_rel_y += ev.value;

			return NULL;
			break;
//		case REL_WHEEL_HI_RES:
//			/* TODO: implement me */
//			return NULL;
//		case REL_HWHEEL_HI_RES:
//			/* TODO: implement me */
//			return NULL;
		default:
			dbg("Unrecognized EV_REL code: %d", ev.code);
			return NULL;
		}

		break;
	case EV_SYN:
		if (dev->_pending_rel_x || dev->_pending_rel_y) {
			devev.type = DEV_MOUSE_MOVE;
			devev.y = dev->_pending_rel_y;
			devev.x = dev->_pending_rel_x;

			dev->_pending_rel_y = 0;
			dev->_pending_rel_x = 0;
		} else {
			return NULL;
		}
		break;
	case EV_ABS:
		switch (ev.code) {
		case ABS_X:
			devev.type = DEV_MOUSE_MOVE_ABS;
			// devev.x = (ev.value * 1024) / (dev->_maxx - dev->_minx);
			// Port: HID absolute ranges may have non-zero logical minimum.
			devev.x = ((ev.value - dev->_minx) * 1024) /
				  (dev->_maxx - dev->_minx);
			devev.y = 0;

			break;
		case ABS_Y:
			devev.type = DEV_MOUSE_MOVE_ABS;
			// devev.y = (ev.value * 1024) / (dev->_maxy - dev->_miny);
			// Port: HID absolute ranges may have non-zero logical minimum.
			devev.y = ((ev.value - dev->_miny) * 1024) /
				  (dev->_maxy - dev->_miny);
			devev.x = 0;

			break;
		case ABS_MT_SLOT:
		case ABS_MT_TOUCH_MAJOR:
		case ABS_MT_TOUCH_MINOR:
		case ABS_MT_WIDTH_MAJOR:
		case ABS_MT_WIDTH_MINOR:
		case ABS_MT_ORIENTATION:
		case ABS_MT_POSITION_X:
		case ABS_MT_POSITION_Y:
		case ABS_MT_TOOL_TYPE:
		case ABS_MT_BLOB_ID:
		case ABS_MT_PRESSURE:
		case ABS_MT_DISTANCE:
		case ABS_MT_TOOL_X:
		case ABS_MT_TOOL_Y:
		case ABS_MT_TRACKING_ID:
			// Upstream KeyD has no Type-B consumer; Linux pointer emulation
			// already delivers the usable stream here as ABS_X/Y.
			return NULL;
		default:
			dbg("Unrecognized EV_ABS code: %x", ev.code);
			// break;
			// Upstream KeyD 0cbe717b: unsupported ABS must not reuse devev.
			return NULL;
		}

		break;
	case EV_KEY:
		/*
		 * KEYD_* codes <256 correspond to their evdev
		 * counterparts.
		 */

		/* Ignore repeat events. */
		if (ev.value == 2)
			return NULL;

		// Port: keep the upstream keyd mapping point, but retain the wider
		// port table already used by this firmware for newer/media keys.
		switch (ev.code) {
		case KEY_PROG1: ev.code = KEYD_F21; break;
		case KEY_PROG2: ev.code = KEYD_F22; break;
		case KEY_PROG3: ev.code = KEYD_F23; break;
		case KEY_PROG4: ev.code = KEYD_F24; break;
		case KEY_SCROLLUP: ev.code = KEYD_SCROLL_UP; break;
		case KEY_SCROLLDOWN: ev.code = KEYD_SCROLL_DOWN; break;
		case KEY_ALTERASE: return NULL;
		case KEY_UNKNOWN: return NULL;
		default: break;
		}

		if (ev.code >= 256) {
			switch (ev.code) {
			/*
			 * Shifted fn keys on laptops which support it.
			 *
			 * NOTE:
			 *
			 * Shifted function keys, some laptops (e.g thinkpads) will map
			 * these to exotic media keys instead.
			 */
			case KEY_FN_F1:  ev.code = KEYD_F13; break;
			case KEY_FN_F2:  ev.code = KEYD_F14; break;
			case KEY_FN_F3:  ev.code = KEYD_F15; break;
			case KEY_FN_F4:  ev.code = KEYD_F16; break;
			case KEY_FN_F5:  ev.code = KEYD_F17; break;
			case KEY_FN_F6:  ev.code = KEYD_F18; break;
			case KEY_FN_F7:  ev.code = KEYD_F19; break;
			case KEY_FN_F8:  ev.code = KEYD_F20; break;
			case KEY_FN_F9:  ev.code = KEYD_F21; break;
			case KEY_FN_F10: ev.code = KEYD_F22; break;
			case KEY_FN_F11: ev.code = KEYD_F23; break;
			case KEY_FN_F12: ev.code = KEYD_F24; break;

			case KEY_TOUCHPAD_TOGGLE: ev.code = KEYD_F21; break;
			case KEY_FAVORITES: ev.code = KEYD_BOOKMARKS; break;

			/* Thinkpad fn shifted f9-f11 */
			case KEY_NOTIFICATION_CENTER:  ev.code = KEYD_F21; break;
			case KEY_PICKUP_PHONE:         ev.code = KEYD_F22; break;
			case KEY_HANGUP_PHONE:         ev.code = KEYD_F23; break;
			case KEY_LINK_PHONE:           ev.code = KEYD_F23; break;

			/* Misc (think/idea)pad fn keys */
			case KEY_FN_RIGHT_SHIFT:       ev.code = KEYD_F13; break;
			case KEY_KEYBOARD:             ev.code = KEYD_F14; break;
			case KEY_REFRESH_RATE_TOGGLE:  ev.code = KEYD_F15; break;
			case KEY_SELECTIVE_SCREENSHOT: ev.code = KEYD_F16; break;
			case KEY_TOUCHPAD_OFF:         ev.code = KEYD_F17; break;
			case KEY_TOUCHPAD_ON:          ev.code = KEYD_F18; break;
			case KEY_VENDOR:               ev.code = KEYD_F19; break;

			/* Menu keys found below LCD screens on some devices (i.e additional function keys) */
			case KEY_KBD_LCD_MENU1:        ev.code = KEYD_F20; break;
			case KEY_KBD_LCD_MENU2:        ev.code = KEYD_F21; break;
			case KEY_KBD_LCD_MENU3:        ev.code = KEYD_F22; break;
			case KEY_KBD_LCD_MENU4:        ev.code = KEYD_F23; break;
			case KEY_KBD_LCD_MENU5:        ev.code = KEYD_F24; break;

			/* Misc keys found on various laptops */
			case KEY_EDITOR:         ev.code = KEYD_F13; break;
			case KEY_SPREADSHEET:    ev.code = KEYD_F14; break;
			case KEY_GRAPHICSEDITOR: ev.code = KEYD_F15; break;
			case KEY_PRESENTATION:   ev.code = KEYD_F16; break;
			case KEY_DATABASE:       ev.code = KEYD_F17; break;
			case KEY_NEWS:           ev.code = KEYD_F18; break;
			case KEY_VOICEMAIL:      ev.code = KEYD_F19; break;
			case KEY_ADDRESSBOOK:    ev.code = KEYD_F20; break;
			case KEY_MESSENGER:      ev.code = KEYD_F21; break;

			case KEY_FN:             ev.code = KEYD_FN; break;
			case KEY_ZOOM:           ev.code = KEYD_ZOOM; break;
			case KEY_VOICECOMMAND:   ev.code = KEYD_VOICECOMMAND; break;

			/* Copilot key on newer kernels */
			case KEY_ACCESSIBILITY: ev.code = KEYD_F23; break;

			/* Mouse buttons */
			case BTN_LEFT:    ev.code = KEYD_LEFT_MOUSE; break;
			case BTN_MIDDLE:  ev.code = KEYD_MIDDLE_MOUSE; break;
			case BTN_RIGHT:   ev.code = KEYD_RIGHT_MOUSE; break;
			case BTN_SIDE:    ev.code = KEYD_MOUSE_1; break;
			case BTN_EXTRA:   ev.code = KEYD_MOUSE_2; break;
			case BTN_BACK:    ev.code = KEYD_MOUSE_BACK; break;
			case BTN_FORWARD: ev.code = KEYD_MOUSE_FORWARD; break;
			case BTN_TASK:    ev.code = KEYD_F18; break;

			case BTN_0:       ev.code = KEYD_F13; break;
			case BTN_1:       ev.code = KEYD_F14; break;
			case BTN_2:       ev.code = KEYD_F15; break;
			case BTN_3:       ev.code = KEYD_F16; break;
			case BTN_4:       ev.code = KEYD_F17; break;
			case BTN_5:       ev.code = KEYD_F18; break;
			case BTN_6:       ev.code = KEYD_F19; break;
			case BTN_7:       ev.code = KEYD_F20; break;
			case BTN_8:       ev.code = KEYD_F21; break;
			case BTN_9:       ev.code = KEYD_F22; break;

			default:
				// keyd_log("r{ERROR:} unsupported evdev code: 0x%x\n", ev.code);
				// Port logger has no keyd_log(); keep the same unsupported
				// key boundary visible through the shared firmware logger.
				dbg("unsupported evdev code: 0x%x", ev.code);
				return NULL;
			}
		}

		devev.type = DEV_KEY;
		devev.code = ev.code;
		devev.pressed = ev.value;

		dbg2("key %s %s", KEY_NAME(devev.code), devev.pressed ? "down" : "up");

		break;
	case EV_LED:
		devev.type = DEV_LED;
		devev.code = ev.code;
		devev.pressed = ev.value;

		break;
	default:
		if (ev.type)
			dbg2("unrecognized evdev event type: %d %d %ld",
			     ev.type, ev.code, (long)ev.value);
		return NULL;
	}

	return &devev;
}

void device_set_led(const struct device *dev, int led, int state)
{
	// struct input_event ev = {
	// Firmware writers accept the timestamp-free port event used by devmon.
	struct port_input_event ev = {
		.type = EV_LED,
		.code = led,
		.value = state
	};

	// xwrite(dev->fd, &ev, sizeof ev);
	// Firmware has no input fd; route the output event through the producer
	// callback, which mirrors evdev_write() -> input_inject_event().
	if (dev->writer.write)
		dev->writer.write(dev->writer.client, &ev, 1);
}

int device_haptic_upload(struct device *dev, enum haptic_effect_index effect,
			 const struct ff_effect *upload)
{
	// Firmware has no evdev ioctl; upload maps to the producer FF callback.
	if (!dev->writer.upload_ff)
		return -ENOSYS;

	struct haptic_state *state = dev->haptic;
	struct ff_effect local = *upload;
	local.id = state->effect_ids[effect];
	int ret = dev->writer.upload_ff(dev->writer.client, &local);
	if (ret == 0)
		state->effect_ids[effect] = local.id;

	return ret;
}

void haptic_init(struct device *dev)
{
	struct haptic_state *state = pvPortMalloc(sizeof *state);

	if (!state) {
		err("haptic state allocation failed");
		return;
	}

	dev->haptic = state;
	for (size_t i = 0; i < HAPTIC_EFFECT_COUNT; i++) {
		struct ff_effect upload = {
			.type = FF_HAPTIC,
			.id = -1,
			.u.haptic = {
				.hid_usage = haptic_waveforms[i],
				.intensity = 100,
			},
		};

		state->effect_ids[i] = -1;
		device_haptic_upload(dev, i, &upload);
	}
}

void haptic_cleanup(struct device *dev)
{
	vPortFree(dev->haptic);
	dev->haptic = NULL;
}

int device_haptic_play(const struct device *dev,
		       enum haptic_effect_index effect, int value)
{
	const struct haptic_state *state = dev->haptic;

	if (!state || state->effect_ids[effect] < 0)
		return -ENOENT;

	// struct input_event ev = {
	// Firmware writers accept the timestamp-free port event used by devmon.
	struct port_input_event ev = {
		.type = EV_FF,
		.code = state->effect_ids[effect],
		.value = value
	};

	// EV_FF play/stop is an input event, so use the same evdev writer path
	// as EV_LED.
	if (!dev->writer.write)
		return -ENOSYS;

	int ret = dev->writer.write(dev->writer.client, &ev, 1);
	return ret < 0 ? ret : 0;
}

int device_haptic_erase(struct device *dev, enum haptic_effect_index effect)
{
	struct haptic_state *state = dev->haptic;
	int ret;

	if (!state || state->effect_ids[effect] < 0)
		return 0;

	// Firmware has no evdev ioctl; erase maps to the producer FF callback.
	if (!dev->writer.erase_ff)
		return -ENOSYS;

	ret = dev->writer.erase_ff(dev->writer.client, state->effect_ids[effect]);
	if (ret == 0)
		state->effect_ids[effect] = -1;

	return ret;
}
