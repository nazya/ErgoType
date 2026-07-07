#include <errno.h>

#include "device.h"
#include "keys.h"
#include "log.h"
#include "uapi/linux/input-event-codes.h"

static QueueSetHandle_t device_events;
struct device *device_table[MAX_DEVICES];
size_t device_table_sz;

void devmon_init(void)
{
	BaseType_t rc;

	device_events = xQueueCreateSet(DEVICE_EVENT_SET_LEN);
	configASSERT(device_events);
	rc = xQueueAddToSet(devmon_queue, device_events);
	configASSERT(rc == pdPASS);
}

int device_add(struct device *dev)
{
	BaseType_t add_rc;
	BaseType_t send_rc;

	add_rc = xQueueAddToSet(dev->ev_queue, device_events);
	if (add_rc != pdPASS)
		return -ENOSPC;

	send_rc = xQueueSendToBack(devmon_queue, &dev, 0);
	if (send_rc != pdPASS) {
		xQueueRemoveFromSet(dev->ev_queue, device_events);
		return -EAGAIN;
	}

	return 0;
}

void device_delete(struct device *dev)
{
	xQueueRemoveFromSet(dev->ev_queue, device_events);
	vQueueDelete(dev->ev_queue);
	dev->ev_queue = NULL;
	if (dev->destroy)
		dev->destroy(dev);
}

struct device_event *device_read_event(struct device *dev)
{
	struct input_event ev;
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
	// Port: dev->ev_queue replaces the upstream evdev fd. The queued
	// struct input_event is the firmware subset documented in device.h;
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
			devev.type = DEV_MOUSE_MOVE;
			devev.x = ev.value;
			devev.y = 0;

			break;
		case REL_Y:
			devev.type = DEV_MOUSE_MOVE;
			devev.y = ev.value;
			devev.x = 0;

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
	case EV_ABS:
		switch (ev.code) {
		case ABS_X:
			devev.type = DEV_MOUSE_MOVE_ABS;
			// devev.x = (ev.value * 1024) / (dev->_maxx - dev->_minx);
			// Port: HID absolute ranges may have non-zero logical minimum.
			devev.x = ((ev.value - dev->_minx) * 1024) / (dev->_maxx - dev->_minx);
			devev.y = 0;

			break;
		case ABS_Y:
			devev.type = DEV_MOUSE_MOVE_ABS;
			// devev.y = (ev.value * 1024) / (dev->_maxy - dev->_miny);
			// Port: HID absolute ranges may have non-zero logical minimum.
			devev.y = ((ev.value - dev->_miny) * 1024) / (dev->_maxy - dev->_miny);
			devev.x = 0;

			break;
		default:
			dbg("Unrecognized EV_ABS code: %x", ev.code);
			break;
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

QueueSetMemberHandle_t device_select(int timeout)
{
	TickType_t xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
	QueueSetMemberHandle_t ready;

	ready = xQueueSelectFromSet(device_events, xTicksToWait);
	return ready;
}
