#include <errno.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "devmon.h"
#include "stdio_tusb_cdc.h"

QueueSetHandle_t devmon_event_set;
QueueSetHandle_t power_supply_event_set;
static uint8_t power_supply_member_count;

int devmon_init(void)
{
	BaseType_t rc;

	devmon_event_set = xQueueCreateSet(DEVICE_EVENT_SET_LEN);
	if (!devmon_event_set)
		return -ENOMEM;
	rc = xQueueAddToSet(devmon_queue, devmon_event_set);
	if (rc != pdPASS) {
		vQueueDelete(devmon_event_set);
		devmon_event_set = NULL;
		return -ENOSPC;
	}
	power_supply_event_set =
		xQueueCreateSet(PORT_POWER_SUPPLY_MAX);
	if (!power_supply_event_set) {
		vQueueDelete(devmon_event_set);
		return -ENOMEM;
	}

	return 0;
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

int devmon_add_power_supply(QueueHandle_t event_queue)
{
	uint8_t count = __atomic_load_n(&power_supply_member_count,
					__ATOMIC_ACQUIRE);

	/*
	 * Queue sets do not reject an empty member when their notification queue
	 * is already fully reserved. Admit members explicitly so any later first
	 * write still has exactly one notification slot.
	 */
	for (;;) {
		if (count == PORT_POWER_SUPPLY_MAX) {
			async_msg("ERR: POWER_SUPPLY_LIMIT");
			return -ENOSPC;
		}
		if (__atomic_compare_exchange_n(&power_supply_member_count,
						&count, count + 1u, false,
						__ATOMIC_ACQ_REL,
						__ATOMIC_ACQUIRE))
			break;
	}

	/* power_supply_register() supplies one newly created empty queue. */
	(void)xQueueAddToSet(event_queue, power_supply_event_set);
	return 0;
}

void devmon_remove_power_supply(QueueHandle_t event_queue)
{
	/* The UI owns the empty member after consuming its terminal snapshot. */
	(void)xQueueRemoveFromSet(event_queue, power_supply_event_set);
	vQueueDelete(event_queue);
	(void)__atomic_sub_fetch(&power_supply_member_count, 1u,
				 __ATOMIC_RELEASE);
}
