#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"

#include "devmon.h"
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
	struct port_input_dev port_dev;
	struct evdev_stats stats;
};

static BaseType_t evdev_send_input_reserved(struct port_input_dev *port_dev,
					    const struct input_event *ev,
					    UBaseType_t reserve)
{
	BaseType_t ret;

	if (uxQueueMessagesWaiting(port_dev->ev_queue) >= DEVICE_EVENT_QUEUE_LEN - reserve)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(port_dev->ev_queue, ev, 0);

	return ret;
}

static BaseType_t evdev_send_input(struct port_input_dev *port_dev,
				   const struct input_event *ev)
{
	return evdev_send_input_reserved(port_dev, ev, EVDEV_QUEUE_NORMAL_RESERVE);
}

static BaseType_t evdev_send_repair_input(struct port_input_dev *port_dev,
					  const struct input_event *ev)
{
	return evdev_send_input_reserved(port_dev, ev, EVDEV_QUEUE_REMOVE_RESERVE);
}

static BaseType_t evdev_queue_reset(struct port_input_dev *port_dev)
{
	struct input_event dropped;
	struct input_event ev = {0};
	BaseType_t ret;

	ev.type = DEVICE_INPUT_RESET;

	if (uxQueueMessagesWaiting(port_dev->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		(void)xQueueReceive(port_dev->ev_queue, &dropped, 0);
	if (uxQueueMessagesWaiting(port_dev->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(port_dev->ev_queue, &ev, 0);

	return ret;
}

void *evdev_register_device(const struct port_input_dev *port_dev)
{
	int ret;
	struct evdev_device *evdev = pvPortMalloc(sizeof *evdev);
	if (!evdev)
		return NULL;
	memset(evdev, 0, sizeof *evdev);
	evdev->port_dev = *port_dev;
	evdev->port_dev.ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
	if (!evdev->port_dev.ev_queue) {
		vPortFree(evdev);
		return NULL;
	}
	ret = devmon_add_device(&evdev->port_dev);
	if (ret < 0) {
		vQueueDelete(evdev->port_dev.ev_queue);
		vPortFree(evdev);
		return NULL;
	}

	return evdev;
}

void evdev_unregister_device(void *dev)
{
	struct input_event ev = {0};
	struct evdev_device *evdev = dev;
	struct port_input_dev *port_dev = &evdev->port_dev;
	struct input_event dropped;

	ev.type = DEVICE_INPUT_REMOVED;
	// ret = evdev_send_input_reserved(device, &ev, 0);
	// configASSERT(ret == pdPASS);
	// TinyUSB unmount path must not assert/block; drop one stale event if
	// needed so removal reaches keyd.
	if (uxQueueMessagesWaiting(port_dev->ev_queue) >= DEVICE_EVENT_QUEUE_LEN)
		(void)xQueueReceive(port_dev->ev_queue, &dropped, 0);
	(void)xQueueSendToBack(port_dev->ev_queue, &ev, 0);
	vPortFree(dev);
}

void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev = {0};
	struct evdev_device *evdev = dev;
	struct port_input_dev *port_dev = &evdev->port_dev;
	BaseType_t ret;

	ev.type = type;
	ev.code = code;
	ev.value = value;
	ret = (type == EV_KEY && !value) ? evdev_send_repair_input(port_dev, &ev) :
					   evdev_send_input(port_dev, &ev);
	if (ret == pdPASS)
		return;

	if (type == EV_KEY) {
		evdev->stats.key_dropped++;
		if (!value) {
			evdev->stats.key_release_dropped++;
			if (evdev_queue_reset(port_dev) == pdPASS)
				evdev->stats.key_reset_queued++;
			else
				evdev->stats.key_reset_dropped++;
		}
	} else if (type == EV_REL) {
		evdev->stats.rel_dropped++;
	} else if (type == EV_ABS) {
		evdev->stats.abs_dropped++;
	} else {
		evdev->stats.other_dropped++;
	}
}

void evdev_get_stats(void *dev, struct evdev_stats *stats)
{
	struct evdev_device *evdev = dev;

	*stats = evdev->stats;
}
