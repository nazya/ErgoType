#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"

#include "keyd/port/device.h"
#include "forward.h"
#include "keys.h"
#include "uapi/linux/input-event-codes.h"

#define FORWARD_QUEUE_REMOVE_RESERVE 1u
#define FORWARD_QUEUE_REPAIR_RESERVE 1u
#define FORWARD_QUEUE_NORMAL_RESERVE \
    (FORWARD_QUEUE_REMOVE_RESERVE + FORWARD_QUEUE_REPAIR_RESERVE)

// Upstream Linux: no equivalent. This is the lossy KeyD/device-queue boundary
// after Linux input batching and proxy copy points.
struct forward_device {
	struct device dev;
	struct forward_stats stats;
	// Work3 used a queue_lock mutex here. In this callback-driven slice all
	// HID forward producers are serialized by the TinyUSB host callback path,
	// so keeping the mutex only adds allocation and a possible callback stall.
	// SemaphoreHandle_t queue_lock;
};

static void forward_destroy_device(struct device *dev)
{
	struct forward_device *fdev = (struct forward_device *)dev;

	vPortFree(fdev);
}

static BaseType_t forward_send_event_reserved(struct device *device,
                                              const struct device_event *ev,
                                              UBaseType_t reserve)
{
    BaseType_t ret;

    // xSemaphoreTake(fdev->queue_lock, portMAX_DELAY);
    // Current callback-driven HID slice has one HID forward producer, so no
    // boundary mutex is needed while forwarding to the FreeRTOS queue.
    if (uxQueueMessagesWaiting(device->ev_queue) >= DEVICE_EVENT_QUEUE_LEN - reserve)
        ret = pdFAIL;
    else
        ret = xQueueSendToBack(device->ev_queue, ev, 0);
    // xSemaphoreGive(fdev->queue_lock);

    return ret;
}

static BaseType_t forward_send_event(struct device *device,
                                     const struct device_event *ev)
{
    return forward_send_event_reserved(device, ev, FORWARD_QUEUE_NORMAL_RESERVE);
}

static BaseType_t forward_send_repair_event(struct device *device,
                                            const struct device_event *ev)
{
    return forward_send_event_reserved(device, ev, FORWARD_QUEUE_REMOVE_RESERVE);
}

static BaseType_t forward_queue_reset(struct device *device)
{
    struct device_event dropped;
    struct device_event ev = {0};
    BaseType_t ret;

    ev.type = DEV_RESET;

    // xSemaphoreTake(fdev->queue_lock, portMAX_DELAY);
    // Current callback-driven HID slice has one HID forward producer, so reset
    // repair keeps the queue reserve without taking a boundary mutex.
    if (uxQueueMessagesWaiting(device->ev_queue) >=
        DEVICE_EVENT_QUEUE_LEN - FORWARD_QUEUE_REMOVE_RESERVE)
        (void)xQueueReceive(device->ev_queue, &dropped, 0);
    if (uxQueueMessagesWaiting(device->ev_queue) >=
        DEVICE_EVENT_QUEUE_LEN - FORWARD_QUEUE_REMOVE_RESERVE)
        ret = pdFAIL;
    else
        ret = xQueueSendToBack(device->ev_queue, &ev, 0);
    // xSemaphoreGive(fdev->queue_lock);

    return ret;
}

void forward_update_device_caps(void *dev, uint8_t caps)
{
	const char *name = "usb-hid";
	struct device *device = dev;

	device->capabilities = caps;
	if ((caps & (FORWARD_CAP_KEYBOARD | FORWARD_CAP_MOUSE)) == FORWARD_CAP_KEYBOARD)
		name = "usb-keyboard";
	else if ((caps & (FORWARD_CAP_KEYBOARD | FORWARD_CAP_MOUSE)) == FORWARD_CAP_MOUSE)
		name = "usb-mouse";
	snprintf(device->name, sizeof(device->name), "%s", name);
}

void forward_add_device_caps(void *dev, uint8_t caps)
{
	struct device *device = dev;

	forward_update_device_caps(device, device->capabilities | caps);
}

void *forward_register_device(uint16_t vendor, uint16_t product, uint8_t caps)
{
	struct forward_device *fdev = pvPortMalloc(sizeof *fdev);
	struct device *dev;
	int ret;

	if (!fdev)
		return NULL;
	memset(fdev, 0, sizeof *fdev);
	dev = &fdev->dev;

	dev->ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct device_event));
	if (!dev->ev_queue) {
		vPortFree(fdev);
		return NULL;
	}
	// fdev->queue_lock = xSemaphoreCreateMutex();
	// if (!fdev->queue_lock) {
	// 	vQueueDelete(dev->ev_queue);
	// 	vPortFree(fdev);
	// 	return NULL;
	// }
	dev->destroy = forward_destroy_device;
	snprintf(dev->id, sizeof(dev->id), "%04x:%04x", vendor, product);
	forward_update_device_caps(dev, caps);

	ret = device_add(dev);
	if (ret < 0) {
		vQueueDelete(dev->ev_queue);
		// vSemaphoreDelete(fdev->queue_lock);
		vPortFree(fdev);
		return NULL;
	}

	return dev;
}

void forward_unregister_device(void *dev)
{
	struct device_event ev = {0};
	struct device *device = dev;

	ev.type = DEV_REMOVED;
	// ret = forward_send_event_reserved(device, &ev, 0);
	// configASSERT(ret == pdPASS);
	// TinyUSB unmount path must not assert/block if KeyD queue cannot accept removal.
	(void)forward_send_event_reserved(device, &ev, 0);
}

void forward_key(void *dev, uint16_t code, uint8_t pressed)
{
	struct device_event ev = {0};
	struct device *device = dev;
	struct forward_device *fdev = dev;

	switch (code) {
	case KEY_PROG1: code = KEYD_F21; break;
	case KEY_PROG2: code = KEYD_F22; break;
	case KEY_PROG3: code = KEYD_F23; break;
	case KEY_PROG4: code = KEYD_F24; break;
	case KEY_SCROLLUP: code = KEYD_SCROLL_UP; break;
	case KEY_SCROLLDOWN: code = KEYD_SCROLL_DOWN; break;
	case KEY_ALTERASE: return;
	case KEY_UNKNOWN: return;
	default: break;
	}

	// KeyD runtime is still 8-bit; do not alias Linux EV_KEY >255 into a wrong KEYD_* code.
	if (code > UINT8_MAX) {
		switch (code) {
		/*
		 * This mirrors upstream keyd/src/device.c EV_KEY >= 256 mapping.
		 * It belongs at this Linux input -> keyd boundary, not inside Linux HID.
		 */
		case KEY_FN_F1:  code = KEYD_F13; break;
		case KEY_FN_F2:  code = KEYD_F14; break;
		case KEY_FN_F3:  code = KEYD_F15; break;
		case KEY_FN_F4:  code = KEYD_F16; break;
		case KEY_FN_F5:  code = KEYD_F17; break;
		case KEY_FN_F6:  code = KEYD_F18; break;
		case KEY_FN_F7:  code = KEYD_F19; break;
		case KEY_FN_F8:  code = KEYD_F20; break;
		case KEY_FN_F9:  code = KEYD_F21; break;
		case KEY_FN_F10: code = KEYD_F22; break;
		case KEY_FN_F11: code = KEYD_F23; break;
		case KEY_FN_F12: code = KEYD_F24; break;

		case KEY_TOUCHPAD_TOGGLE: code = KEYD_F21; break;
		case KEY_FAVORITES: code = KEYD_BOOKMARKS; break;

		case KEY_NOTIFICATION_CENTER:  code = KEYD_F21; break;
		case KEY_PICKUP_PHONE:         code = KEYD_F22; break;
		case KEY_HANGUP_PHONE:         code = KEYD_F23; break;
		case KEY_LINK_PHONE:           code = KEYD_F23; break;

		case KEY_FN_RIGHT_SHIFT:       code = KEYD_F13; break;
		case KEY_KEYBOARD:             code = KEYD_F14; break;
		case KEY_REFRESH_RATE_TOGGLE:  code = KEYD_F15; break;
		case KEY_SELECTIVE_SCREENSHOT: code = KEYD_F16; break;
		case KEY_TOUCHPAD_OFF:         code = KEYD_F17; break;
		case KEY_TOUCHPAD_ON:          code = KEYD_F18; break;
		case KEY_VENDOR:               code = KEYD_F19; break;

		case KEY_KBD_LCD_MENU1:        code = KEYD_F20; break;
		case KEY_KBD_LCD_MENU2:        code = KEYD_F21; break;
		case KEY_KBD_LCD_MENU3:        code = KEYD_F22; break;
		case KEY_KBD_LCD_MENU4:        code = KEYD_F23; break;
		case KEY_KBD_LCD_MENU5:        code = KEYD_F24; break;

		case KEY_EDITOR:         code = KEYD_F13; break;
		case KEY_SPREADSHEET:    code = KEYD_F14; break;
		case KEY_GRAPHICSEDITOR: code = KEYD_F15; break;
		case KEY_PRESENTATION:   code = KEYD_F16; break;
		case KEY_DATABASE:       code = KEYD_F17; break;
		case KEY_NEWS:           code = KEYD_F18; break;
		case KEY_VOICEMAIL:      code = KEYD_F19; break;
		case KEY_ADDRESSBOOK:    code = KEYD_F20; break;
		case KEY_MESSENGER:      code = KEYD_F21; break;

		case KEY_FN:             code = KEYD_FN; break;
		case KEY_ZOOM:           code = KEYD_ZOOM; break;
		case KEY_VOICECOMMAND:   code = KEYD_VOICECOMMAND; break;
		case KEY_ACCESSIBILITY:  code = KEYD_F23; break;

		case BTN_LEFT:    code = KEYD_LEFT_MOUSE; break;
		case BTN_MIDDLE:  code = KEYD_MIDDLE_MOUSE; break;
		case BTN_RIGHT:   code = KEYD_RIGHT_MOUSE; break;
		case BTN_SIDE:    code = KEYD_MOUSE_1; break;
		case BTN_EXTRA:   code = KEYD_MOUSE_2; break;
		case BTN_BACK:    code = KEYD_MOUSE_BACK; break;
		case BTN_FORWARD: code = KEYD_MOUSE_FORWARD; break;
		case BTN_TASK:    code = KEYD_F18; break;

		case BTN_0:       code = KEYD_F13; break;
		case BTN_1:       code = KEYD_F14; break;
		case BTN_2:       code = KEYD_F15; break;
		case BTN_3:       code = KEYD_F16; break;
		case BTN_4:       code = KEYD_F17; break;
		case BTN_5:       code = KEYD_F18; break;
		case BTN_6:       code = KEYD_F19; break;
		case BTN_7:       code = KEYD_F20; break;
		case BTN_8:       code = KEYD_F21; break;
		case BTN_9:       code = KEYD_F22; break;

		default:
			return;
		}
	}

    ev.type = DEV_KEY;
    ev.code = (uint8_t)code;
    ev.pressed = pressed;
    if ((pressed ? forward_send_event(device, &ev) :
                   forward_send_repair_event(device, &ev)) != pdPASS) {
        fdev->stats.key_dropped++;
        if (!pressed) {
            fdev->stats.key_release_dropped++;
            if (forward_queue_reset(device) == pdPASS)
                fdev->stats.key_reset_queued++;
            else
                fdev->stats.key_reset_dropped++;
        }
    }
}

void forward_mouse_move(void *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};
	struct device *device = dev;
	struct forward_device *fdev = dev;

	ev.type = DEV_MOUSE_MOVE;
	ev.x = x;
	ev.y = y;
	if (forward_send_event(device, &ev) != pdPASS)
		fdev->stats.mouse_move_dropped++;
}

void forward_mouse_move_abs(void *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};
	struct device *device = dev;
	struct forward_device *fdev = dev;

	ev.type = DEV_MOUSE_MOVE_ABS;
	ev.x = x;
	ev.y = y;
	if (forward_send_event(device, &ev) != pdPASS)
		fdev->stats.mouse_move_abs_dropped++;
}

void forward_mouse_scroll(void *dev, int32_t x, int32_t y)
{
	struct device_event ev = {0};
	struct device *device = dev;
	struct forward_device *fdev = dev;

	ev.type = DEV_MOUSE_SCROLL;
	ev.x = x;
	ev.y = y;
	if (forward_send_event(device, &ev) != pdPASS)
		fdev->stats.mouse_scroll_dropped++;
}

void forward_get_stats(void *dev, struct forward_stats *stats)
{
	struct forward_device *fdev = dev;

	*stats = fdev->stats;
}
