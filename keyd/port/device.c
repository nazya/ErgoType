#include <errno.h>

#include "device.h"

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
	static struct device_event devev;

	if (xQueueReceive(dev->ev_queue, &devev, 0) != pdPASS)
		return NULL;

	return &devev;
}

QueueSetMemberHandle_t device_select(int timeout)
{
	TickType_t xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
	QueueSetMemberHandle_t ready;

	ready = xQueueSelectFromSet(device_events, xTicksToWait);
	return ready;
}
