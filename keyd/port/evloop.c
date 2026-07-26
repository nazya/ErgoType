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
#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"
#include "task.h"

#include "keyd.h"
#include "log.h"
#include "stdio_tusb_cdc.h"

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
		int handled_device = 0;
		int removed = 0;

		int start_time;
		int elapsed;
		TickType_t wait_ticks;

		start_time = get_time_ms();
		wait_ticks = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
		ready = xQueueSelectFromSet(devmon_event_set, wait_ticks);
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
			struct device *dev = device_table[i];

			if (dev->ev_queue != ready)
				continue;

			devev = device_read_event(dev);
			if (!devev)
				continue;

			if (devev->type == DEV_REMOVED) {
				ev.type = EV_DEV_REMOVE;
				ev.dev = dev;
				ev.devev = NULL;

				timeout = event_handler(&ev);

				xQueueRemoveFromSet(dev->ev_queue, devmon_event_set);
				vQueueDelete(dev->ev_queue);
				dev->ev_queue = NULL;
				// Firmware boundary: evdev leaves the detached client allocated;
				// KeyD owns its final free after consuming the removal event.
				if (dev->writer.client) {
					vPortFree(dev->writer.client);
					dev->writer.client = NULL;
				}
				vPortFree(dev);
				device_table[i] = NULL;
				handled_device = 1;
				removed = 1;
				break;
			}

			ev.type = EV_DEV_EVENT;
			ev.dev = dev;
			ev.devev = devev;

			timeout = event_handler(&ev);
			handled_device = 1;
		}

		if (removed) {
			size_t n = 0;

			for (i = 0; i < device_table_sz; i++)
				if (device_table[i])
					device_table[n++] = device_table[i];

			device_table_sz = n;
		}

		if (ready != devmon_queue && handled_device)
			continue;

		struct devmon_event devmon_ev;

		if (xQueueReceive(devmon_queue, &devmon_ev, 0) != pdPASS)
			continue;

		// Virtual output requests enter KeyD without a source input device.
		if (devmon_ev.is_virtual) {
			struct device_event devev = {
				.code = devmon_ev.code,
				.pressed = devmon_ev.value,
			};

			switch (devmon_ev.type) {
			case DEVMON_LED:
				devev.type = DEV_LED;
				break;
			case DEVMON_HAPTIC:
				devev.type = DEV_HAPTIC;
				break;
			}

			ev.type = EV_DEV_EVENT;
			ev.dev = NULL; // Virtual output event has no physical source device.
			ev.devev = &devev;
			timeout = event_handler(&ev);
			continue;
		}

		/*
		 * RELEASE BLOCKER: ADD already transferred the live queue/client to
		 * KeyD. Admission must move before publication before these failures
		 * can reject only this device. Keep explicit checks because Release
		 * builds compile configASSERT() out under NDEBUG.
		 */
		if (device_table_sz >= MAX_DEVICES) {
			async_msg("ERR: KEYD_DEVICE_LIMIT");
			vTaskSuspend(NULL);
			continue;
		}

		struct device *dev = pvPortMalloc(sizeof *dev);
		if (!dev) {
			async_msg("ERR: KEYD_DEVICE_NOMEM");
			vTaskSuspend(NULL);
			continue;
		}

		int ret = device_init(&devmon_ev.dev, dev);
		if (ret) {
			vPortFree(dev);
			async_msg("ERR: KEYD_DEVICE_INIT");
			vTaskSuspend(NULL);
			continue;
		}

		device_table[device_table_sz++] = dev;

		ev.type = EV_DEV_ADD;
		ev.dev = dev;
		ev.devev = NULL;
		timeout = event_handler(&ev);
	}

	return 0; /* Unreachable. */
}
