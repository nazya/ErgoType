#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "linux/include/linux/hid_compat.h"

#define HID_TIMER_WAKE_LEN 1

/*
 * Upstream Linux timers run from the kernel timer wheel. This firmware bridge
 * keeps Linux timer_list users active without blocking TinyUSB callbacks:
 * mod_timer() only links/updates the timer, and this task invokes callbacks.
 */
static QueueHandle_t hid_timer_wake_queue;
static struct timer_list *hid_timer_head;

static void hid_timer_wake(void)
{
	uint8_t wake = 1;

	(void)xQueueSendToBack(hid_timer_wake_queue, &wake, 0);
}

int hid_timer_init(void)
{
	hid_timer_head = NULL;
	hid_timer_wake_queue = xQueueCreate(HID_TIMER_WAKE_LEN, sizeof(uint8_t));
	if (!hid_timer_wake_queue)
		return -ENOMEM;

	return 0;
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
	int was_pending;

	if (!hid_timer_wake_queue)
		return 0;

	taskENTER_CRITICAL();
	was_pending = timer->pending;
	timer->expires = expires;
	if (!timer->pending) {
		timer->pending = 1;
		timer->next = hid_timer_head;
		hid_timer_head = timer;
	}
	taskEXIT_CRITICAL();

	hid_timer_wake();
	return was_pending;
}

static int hid_timer_delete_pending(struct timer_list *timer)
{
	struct timer_list **link;
	int was_pending;

	taskENTER_CRITICAL();
	was_pending = timer->pending;
	timer->pending = 0;

	link = &hid_timer_head;
	while (*link) {
		if (*link == timer) {
			*link = timer->next;
			timer->next = NULL;
			break;
		}
		link = &(*link)->next;
	}
	taskEXIT_CRITICAL();

	return was_pending;
}

int timer_delete(struct timer_list *timer)
{
	return hid_timer_delete_pending(timer);
}

int timer_delete_sync(struct timer_list *timer)
{
	int was_pending;

	/*
	 * Linux waits for a running timer callback here. HID disconnect/remove is
	 * handed from TinyUSB callbacks to a firmware task before driver remove,
	 * so remove-time callers can wait without blocking TinyUSB.
	 */
	was_pending = hid_timer_delete_pending(timer);

	while (timer->running)
		vTaskDelay(1);

	return was_pending;
}

void hid_timer_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct timer_list **link;
		struct timer_list *timer = NULL;
		TickType_t wait_ticks = portMAX_DELAY;
		unsigned long now = jiffies;

		taskENTER_CRITICAL();
		link = &hid_timer_head;
		while (*link) {
			struct timer_list *candidate = *link;

			if (!candidate->pending) {
				*link = candidate->next;
				candidate->next = NULL;
				continue;
			}

			if (time_before_eq(candidate->expires, now)) {
				*link = candidate->next;
				candidate->next = NULL;
				candidate->pending = 0;
				candidate->running = 1;
				timer = candidate;
				break;
			}

			unsigned long delta = candidate->expires - now;
			if (wait_ticks == portMAX_DELAY || delta < wait_ticks)
				wait_ticks = delta;
			link = &candidate->next;
		}
		taskEXIT_CRITICAL();

		if (timer) {
			if (timer->function)
				timer->function(timer);

			taskENTER_CRITICAL();
			timer->running = 0;
			taskEXIT_CRITICAL();
			continue;
		}

		uint8_t wake;
		xQueueReceive(hid_timer_wake_queue, &wake, wait_ticks);
	}
}
