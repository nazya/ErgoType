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

#ifdef ERGOTYPE_ZEPHYR
#include "platform/events.h"
#include "platform/time.h"
#else
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#endif

#include "keyd.h"
#include "log.h"

#ifndef ERGOTYPE_ZEPHYR
extern QueueHandle_t input_event_queue;
#endif

int evloop(int (*event_handler)(struct event *ev))
{
	int timeout = 0;
	struct event ev;
	struct device_event devev;

#ifdef ERGOTYPE_ZEPHYR
	int32_t wait_ms = PLATFORM_EVENT_WAIT_FOREVER;
#else
	TickType_t xTicksToWait =
		portMAX_DELAY; /* Block indefinitely if not otherwise specified. */
#endif

	dbg3("entering evloop");

	while (1) {
#ifdef ERGOTYPE_ZEPHYR
		if (platform_input_event_recv(&devev, wait_ms) == 0) {
			ev.timestamp = (int)platform_uptime_ms();
			ev.type = EV_DEV_EVENT;
			ev.devev = &devev;
		} else {
			ev.type = EV_TIMEOUT;
			ev.devev = NULL;
			ev.timestamp = (int)platform_uptime_ms();
		}
#else
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
#endif

		timeout = event_handler(&ev);
#ifdef ERGOTYPE_ZEPHYR
		wait_ms = timeout > 0 ? timeout : PLATFORM_EVENT_WAIT_FOREVER;
#else
		xTicksToWait = timeout > 0 ? pdMS_TO_TICKS(timeout) : portMAX_DELAY;
#endif
	}

	return 0; /* Unreachable. */
}
