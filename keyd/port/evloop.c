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
#include "queue.h"
#include "task.h"

#include "keyd.h"
#include "log.h"

extern QueueHandle_t input_event_queue;

int evloop(int (*event_handler)(struct event *ev))
{
	int timeout = 0;
	struct event ev;
	struct device_event devev;

	TickType_t xTicksToWait =
		portMAX_DELAY; /* Block indefinitely if not otherwise specified. */

	dbg3("entering evloop");

	while (1) {
		if (xQueueReceive(input_event_queue, &devev, xTicksToWait) ==
		    pdPASS) {
			ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
			ev.type = EV_DEV_EVENT;
			ev.devev = &devev;
		} else {
			/* Timeout occurred. */
			ev.type = EV_TIMEOUT;
			ev.devev = NULL;
			ev.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
		}

		timeout = event_handler(&ev);
		xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
	}

	return 0; /* Unreachable. */
}
