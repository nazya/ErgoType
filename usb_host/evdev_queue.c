#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"

#include "keyd/port/device.h"
#include "evdev.h"
#include "uapi/linux/input-event-codes.h"

#define EVDEV_QUEUE_REMOVE_RESERVE 1u
#define EVDEV_QUEUE_REPAIR_RESERVE 1u
#define EVDEV_QUEUE_NORMAL_RESERVE \
    (EVDEV_QUEUE_REMOVE_RESERVE + EVDEV_QUEUE_REPAIR_RESERVE)

// Port note: upstream evdev buffers events for userspace clients. Firmware
// replaces that per-client fd buffer with this KeyD queue boundary.
struct evdev_device {
	struct device dev;
	struct evdev_stats stats;
	// Work3 used a queue_lock mutex here. In this callback-driven slice all
	// HID evdev producers are serialized by the TinyUSB host callback path,
	// so keeping the mutex only adds allocation and a possible callback stall.
	// SemaphoreHandle_t queue_lock;
};

static void evdev_destroy_device(struct device *dev)
{
	struct evdev_device *fdev = (struct evdev_device *)dev;

	vPortFree(fdev);
}

static BaseType_t evdev_send_input_reserved(struct device *device,
					      const struct input_event *ev,
					      UBaseType_t reserve)
{
	BaseType_t ret;

	// xSemaphoreTake(fdev->queue_lock, portMAX_DELAY);
	// Current callback-driven HID slice has one HID evdev producer, so no
	// boundary mutex is needed while sending to the FreeRTOS queue.
	if (uxQueueMessagesWaiting(device->ev_queue) >= DEVICE_EVENT_QUEUE_LEN - reserve)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(device->ev_queue, ev, 0);
	// xSemaphoreGive(fdev->queue_lock);

	return ret;
}

static BaseType_t evdev_send_input(struct device *device,
				     const struct input_event *ev)
{
	return evdev_send_input_reserved(device, ev, EVDEV_QUEUE_NORMAL_RESERVE);
}

static BaseType_t evdev_send_repair_input(struct device *device,
					    const struct input_event *ev)
{
	return evdev_send_input_reserved(device, ev, EVDEV_QUEUE_REMOVE_RESERVE);
}

static BaseType_t evdev_queue_reset(struct device *device)
{
	struct input_event dropped;
	struct input_event ev = {0};
	BaseType_t ret;

	ev.type = DEVICE_INPUT_RESET;

	// xSemaphoreTake(fdev->queue_lock, portMAX_DELAY);
	// Current callback-driven HID slice has one HID evdev producer, so reset
	// repair keeps the queue reserve without taking a boundary mutex.
	if (uxQueueMessagesWaiting(device->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		(void)xQueueReceive(device->ev_queue, &dropped, 0);
	if (uxQueueMessagesWaiting(device->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(device->ev_queue, &ev, 0);
	// xSemaphoreGive(fdev->queue_lock);

	return ret;
}

void evdev_update_device_caps(void *dev, uint8_t caps)
{
	const char *name = "usb-hid";
	struct device *device = dev;

	device->capabilities = caps;
	if ((caps & (EVDEV_CAP_KEYBOARD | EVDEV_CAP_MOUSE)) == EVDEV_CAP_KEYBOARD)
		name = "usb-keyboard";
	else if ((caps & (EVDEV_CAP_KEYBOARD | EVDEV_CAP_MOUSE)) == EVDEV_CAP_MOUSE)
		name = "usb-mouse";
	snprintf(device->name, sizeof(device->name), "%s", name);
}

void evdev_add_device_caps(void *dev, uint8_t caps)
{
	struct device *device = dev;

	evdev_update_device_caps(device, device->capabilities | caps);
}

void *evdev_register_device(uint16_t vendor, uint16_t product, uint8_t caps)
{
	struct evdev_device *fdev = pvPortMalloc(sizeof *fdev);
	struct device *dev;
	int ret;

	if (!fdev)
		return NULL;
	memset(fdev, 0, sizeof *fdev);
	dev = &fdev->dev;

	dev->ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
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
	dev->destroy = evdev_destroy_device;
	snprintf(dev->id, sizeof(dev->id), "%04x:%04x", vendor, product);
	evdev_update_device_caps(dev, caps);

	ret = device_add(dev);
	if (ret < 0) {
		vQueueDelete(dev->ev_queue);
		// vSemaphoreDelete(fdev->queue_lock);
		vPortFree(fdev);
		return NULL;
	}

	return dev;
}

void evdev_unregister_device(void *dev)
{
	struct input_event ev = {0};
	struct device *device = dev;

	ev.type = DEVICE_INPUT_REMOVED;
	// ret = evdev_send_input_reserved(device, &ev, 0);
	// configASSERT(ret == pdPASS);
	// TinyUSB unmount path must not assert/block if KeyD queue cannot accept removal.
	(void)evdev_send_input_reserved(device, &ev, 0);
}

void evdev_configure_abs(void *dev, int32_t minx, int32_t maxx, int32_t miny, int32_t maxy)
{
	struct device *device = dev;

	device->_minx = minx;
	device->_maxx = maxx;
	device->_miny = miny;
	device->_maxy = maxy;
}

void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev = {0};
	struct device *device = dev;
	struct evdev_device *fdev = dev;
	BaseType_t ret;

	ev.type = type;
	ev.code = code;
	ev.value = value;
	ret = (type == EV_KEY && !value) ? evdev_send_repair_input(device, &ev) :
					   evdev_send_input(device, &ev);
	if (ret == pdPASS)
		return;

	if (type == EV_KEY) {
		fdev->stats.key_dropped++;
		if (!value) {
			fdev->stats.key_release_dropped++;
			if (evdev_queue_reset(device) == pdPASS)
				fdev->stats.key_reset_queued++;
			else
				fdev->stats.key_reset_dropped++;
		}
	} else if (type == EV_REL) {
		fdev->stats.rel_dropped++;
	} else if (type == EV_ABS) {
		fdev->stats.abs_dropped++;
	} else {
		fdev->stats.other_dropped++;
	}
}

void evdev_get_stats(void *dev, struct evdev_stats *stats)
{
	struct evdev_device *fdev = dev;

	*stats = fdev->stats;
}
