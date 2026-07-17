#include <errno.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "devmon.h"

QueueSetHandle_t devmon_event_set;

void devmon_init(void)
{
	BaseType_t rc;

	devmon_event_set = xQueueCreateSet(DEVICE_EVENT_SET_LEN);
	configASSERT(devmon_event_set);
	rc = xQueueAddToSet(devmon_queue, devmon_event_set);
	configASSERT(rc == pdPASS);
}

int devmon_add_device(const struct port_input_dev *port_dev)
{
	struct devmon_event event = {
		.dev = *port_dev,
	};
	BaseType_t add_rc;
	BaseType_t send_rc;

	add_rc = xQueueAddToSet(port_dev->ev_queue, devmon_event_set);
	if (add_rc != pdPASS)
		return -ENOSPC;

	send_rc = xQueueSendToBack(devmon_queue, &event, portMAX_DELAY);
	if (send_rc != pdPASS) {
		xQueueRemoveFromSet(port_dev->ev_queue, devmon_event_set);
		return -EAGAIN;
	}

	return 0;
}
