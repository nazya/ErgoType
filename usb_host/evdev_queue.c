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

/*
 * Upstream evdev buffers events for userspace clients. Firmware replaces that
 * per-client fd buffer with this device queue boundary.
 */
struct evdev_device {
	struct device dev;
	struct evdev_stats stats;
	// This callback-driven HID slice has one HID evdev producer per input
	// handle, so keeping a boundary mutex only adds allocation and a possible
	// callback stall.
	// SemaphoreHandle_t queue_lock;
};

static void evdev_destroy_device(struct device *dev)
{
	struct evdev_device *edev = (struct evdev_device *)dev;

	vPortFree(edev);
}

static void evdev_copy_input_info(struct device_input_info *dst,
				  const struct evdev_input_info *src)
{
	memcpy(dst->keymask, src->keymask, sizeof(dst->keymask));
	dst->num_keys = src->num_keys;
	dst->relmask = src->relmask;
	dst->absmask = src->absmask;
	dst->minx = src->minx;
	dst->maxx = src->maxx;
	dst->miny = src->miny;
	dst->maxy = src->maxy;
}

static BaseType_t evdev_send_input_reserved(struct device *device,
					    const struct input_event *ev,
					    UBaseType_t reserve)
{
	BaseType_t ret;

	// xSemaphoreTake(edev->queue_lock, portMAX_DELAY);
	// Current callback-driven HID slice has one HID evdev producer per queue,
	// so no boundary mutex is needed while sending to the FreeRTOS queue.
	if (uxQueueMessagesWaiting(device->ev_queue) >= DEVICE_EVENT_QUEUE_LEN - reserve)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(device->ev_queue, ev, 0);
	// xSemaphoreGive(edev->queue_lock);

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

	// xSemaphoreTake(edev->queue_lock, portMAX_DELAY);
	// Current callback-driven HID slice has one HID evdev producer per queue,
	// so reset repair keeps the queue reserve without taking a boundary mutex.
	if (uxQueueMessagesWaiting(device->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		(void)xQueueReceive(device->ev_queue, &dropped, 0);
	if (uxQueueMessagesWaiting(device->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(device->ev_queue, &ev, 0);
	// xSemaphoreGive(edev->queue_lock);

	return ret;
}

void *evdev_register_device(uint16_t vendor, uint16_t product,
			    const struct evdev_input_info *info)
{
	struct device_input_info device_info = {0};
	struct evdev_device *edev;
	struct device *dev;
	int ret;

	edev = pvPortMalloc(sizeof *edev);
	if (!edev)
		return NULL;
	memset(edev, 0, sizeof *edev);
	dev = &edev->dev;

	evdev_copy_input_info(&device_info, info);
	ret = device_init(dev, vendor, product, &device_info);
	if (ret < 0) {
		vPortFree(edev);
		return NULL;
	}

	dev->ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
	if (!dev->ev_queue) {
		vPortFree(edev);
		return NULL;
	}
	// edev->queue_lock = xSemaphoreCreateMutex();
	// if (!edev->queue_lock) {
	// 	vQueueDelete(dev->ev_queue);
	// 	vPortFree(edev);
	// 	return NULL;
	// }
	dev->destroy = evdev_destroy_device;

	ret = device_add(dev);
	if (ret < 0) {
		vQueueDelete(dev->ev_queue);
		// vSemaphoreDelete(edev->queue_lock);
		vPortFree(edev);
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
	// TinyUSB unmount path must not assert/block if the queue cannot accept
	// removal.
	(void)evdev_send_input_reserved(device, &ev, 0);
}

void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev = {0};
	struct device *device = dev;
	struct evdev_device *edev = dev;
	BaseType_t ret;

	ev.type = type;
	ev.code = code;
	ev.value = value;
	ret = (type == EV_KEY && !value) ? evdev_send_repair_input(device, &ev) :
					   evdev_send_input(device, &ev);
	if (ret == pdPASS)
		return;

	if (type == EV_KEY) {
		edev->stats.key_dropped++;
		if (!value) {
			edev->stats.key_release_dropped++;
			if (evdev_queue_reset(device) == pdPASS)
				edev->stats.key_reset_queued++;
			else
				edev->stats.key_reset_dropped++;
		}
	} else if (type == EV_REL) {
		edev->stats.rel_dropped++;
	} else if (type == EV_ABS) {
		edev->stats.abs_dropped++;
	} else {
		edev->stats.other_dropped++;
	}
}

void evdev_get_stats(void *dev, struct evdev_stats *stats)
{
	struct evdev_device *edev = dev;

	*stats = edev->stats;
}
