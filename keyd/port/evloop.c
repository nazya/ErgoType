/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 * © 2021 Giorgi Chavchanidze
 */

/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */

#include <stddef.h>

#include "FreeRTOS.h"
#include "task.h"

#include "keyd.h"
#include "log.h"

static long get_time_ms(void)
{
	return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

int evloop(int (*event_handler)(struct event *ev))
{
	int timeout = 0;
	struct event ev;
	QueueSetMemberHandle_t ready;
	size_t i;

	dbg3("entering evloop");

	while (1) {
		int removed = 0;

		int start_time;
		int elapsed;

		start_time = get_time_ms();
		ready = device_select(timeout);
		ev.timestamp = get_time_ms();
		elapsed = ev.timestamp - start_time;

		if (timeout > 0 && elapsed >= timeout) {
			/* Timeout occurred. */
			ev.type = EV_TIMEOUT;
			ev.dev = NULL;
			ev.devev = NULL;
			timeout = event_handler(&ev);
		} else {
			timeout -= elapsed;
		}

		if (!ready)
			continue;

		for (i = 0; i < device_table_sz; i++) {
			struct device_event *devev;
			struct device *dev = &device_table[i];

			if (dev->events != ready)
				continue;

			devev = device_read_event(dev);
			if (!devev)
				continue;

			if (devev->type == DEV_REMOVED) {
				ev.type = EV_DEV_REMOVE;
				ev.dev = dev;
				ev.devev = NULL;

				timeout = event_handler(&ev);

				device_delete(dev);
				removed = 1;
				break;
			}

			ev.type = EV_DEV_EVENT;
			ev.dev = dev;
			ev.devev = devev;

			timeout = event_handler(&ev);
		}

		if (removed) {
			size_t n = 0;

			for (i = 0; i < device_table_sz; i++)
				if (device_table[i].events)
					device_table[n++] = device_table[i];

			device_table_sz = n;
		}

		if (ready != devmon_queue)
			continue;

		struct device dev;

		if (xQueueReceive(devmon_queue, &dev, 0) != pdPASS)
			continue;

		configASSERT(device_table_sz < MAX_DEVICES);
		device_table[device_table_sz++] = dev;

		ev.type = EV_DEV_ADD;
		ev.dev = &device_table[device_table_sz - 1];
		ev.devev = NULL;
		timeout = event_handler(&ev);
	}

	return 0; /* Unreachable. */
}
