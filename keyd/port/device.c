#include "device.h"

static QueueSetHandle_t device_events;
struct device device_table[MAX_DEVICES];
size_t device_table_sz;

void devmon_init(void)
{
	BaseType_t rc;

	device_events = xQueueCreateSet(DEVICE_EVENT_SET_LEN);
	configASSERT(device_events);
	rc = xQueueAddToSet(devmon_queue, device_events);
	configASSERT(rc == pdPASS);
}

void device_add(const struct device *dev)
{
	BaseType_t add_rc;
	BaseType_t send_rc;

	add_rc = xQueueAddToSet(dev->events, device_events);
	configASSERT(add_rc == pdPASS);
	send_rc = xQueueSendToBack(devmon_queue, dev, portMAX_DELAY);
	configASSERT(send_rc == pdPASS);
}

void device_delete(struct device *dev)
{
	xQueueRemoveFromSet(dev->events, device_events);
	vQueueDelete(dev->events);
	dev->events = NULL;
}

struct device_event *device_read_event(struct device *dev)
{
	static struct device_event devev;

	if (xQueueReceive(dev->events, &devev, 0) != pdPASS)
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
