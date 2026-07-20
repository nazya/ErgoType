#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "linux/include/linux/hid_compat.h"
#include "stdio_tusb_cdc.h"

#define HID_TIMER_NOTIFY_INDEX 0u

_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > HID_TIMER_NOTIFY_INDEX,
	       "HID timer requires task notification index 0");
/* Index 0 is shared with task-side waits; every user has a predicate. */

struct hid_timer_waiter {
	struct hid_timer_waiter *next;
	struct timer_list *timer;
	TaskHandle_t task;
	bool linked;
};

/*
 * Upstream Linux timers use per-base locks because timer operations may enter
 * from IRQ/softirq context. TinyUSB callbacks only publish report state in this
 * port; every live timer API caller is now a firmware task. A dedicated
 * priority-inheritance mutex therefore protects the timer wheel without
 * masking interrupts or sharing either the USB transport or workqueue lock.
 */
static SemaphoreHandle_t hid_timer_mutex;
static TaskHandle_t hid_timer_task_handle;
static struct hid_timer_waiter *hid_timer_waiters;
static struct timer_list *hid_timer_head;

static bool hid_timer_lock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool locked;

	/* Keep every FreeRTOS operation outside configASSERT(). */
	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return false;

	ready = hid_timer_mutex != NULL;
	if (!ready)
		async_msg("ERR: HID_TIMER_NOT_READY");
	configASSERT(ready);
	if (!ready)
		return false;

	ret = xSemaphoreTake(hid_timer_mutex, portMAX_DELAY);
	locked = ret == pdPASS;
	if (!locked)
		async_msg("ERR: HID_TIMER_LOCK_FAIL");
	configASSERT(locked);
	return locked;
}

static bool hid_timer_unlock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool unlocked;

	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return false;

	ready = hid_timer_mutex != NULL;
	if (!ready)
		async_msg("ERR: HID_TIMER_NOT_READY");
	configASSERT(ready);
	if (!ready)
		return false;

	ret = xSemaphoreGive(hid_timer_mutex);
	unlocked = ret == pdPASS;
	if (!unlocked)
		async_msg("ERR: HID_TIMER_UNLOCK_FAIL");
	configASSERT(unlocked);
	return unlocked;
}

static bool hid_timer_waiter_link_locked(struct hid_timer_waiter *waiter)
{
	bool valid = waiter && waiter->timer && waiter->task && !waiter->linked;

	if (!valid)
		return false;
	waiter->next = hid_timer_waiters;
	waiter->linked = true;
	hid_timer_waiters = waiter;
	return true;
}

static bool hid_timer_waiter_unlink_locked(struct hid_timer_waiter *waiter)
{
	struct hid_timer_waiter **link = &hid_timer_waiters;

	if (!waiter->linked)
		return true;
	while (*link && *link != waiter)
		link = &(*link)->next;
	if (!*link) {
		waiter->next = NULL;
		waiter->linked = false;
		return false;
	}
	*link = waiter->next;
	waiter->next = NULL;
	waiter->linked = false;
	return true;
}

static void hid_timer_wake_waiters_locked(struct timer_list *timer)
{
	struct hid_timer_waiter *waiter;

	/* Linux wake_up_all() is an edge; waiters recheck timer->running. */
	for (waiter = hid_timer_waiters; waiter; waiter = waiter->next) {
		if (waiter->timer == timer && !timer->running)
			(void)xTaskNotifyGiveIndexed(waiter->task,
						 HID_TIMER_NOTIFY_INDEX);
	}
}

static void hid_timer_wake_task(void)
{
	TaskHandle_t task;

	if (!hid_timer_lock())
		return;
	task = hid_timer_task_handle;
	(void)hid_timer_unlock();
	if (task)
		(void)xTaskNotifyGiveIndexed(task, HID_TIMER_NOTIFY_INDEX);
}

int hid_timer_init(void)
{
	SemaphoreHandle_t mutex;
	bool uninitialized;

	uninitialized = hid_timer_mutex == NULL;
	if (!uninitialized) {
		async_msg("ERR: HID_TIMER_INIT_TWICE");
		configASSERT(uninitialized);
		return -EBUSY;
	}
	mutex = xSemaphoreCreateMutex();
	if (!mutex)
		return -ENOMEM;

	hid_timer_head = NULL;
	hid_timer_waiters = NULL;
	hid_timer_task_handle = NULL;
	/* Publish readiness only after every durable predicate is initialized. */
	hid_timer_mutex = mutex;
	return 0;
}

int mod_timer(struct timer_list *timer, unsigned long expires)
{
	int was_pending;

	if (!hid_timer_mutex || !hid_timer_lock())
		return 0;
	was_pending = timer->pending;
	timer->expires = expires;
	if (!timer->pending) {
		timer->pending = 1;
		timer->next = hid_timer_head;
		hid_timer_head = timer;
	}
	(void)hid_timer_unlock();

	/* The linked timer is the condition; notification is only its wake edge. */
	hid_timer_wake_task();
	return was_pending;
}

static int hid_timer_delete_pending_locked(struct timer_list *timer)
{
	struct timer_list **link;
	int was_pending = timer->pending;

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
	return was_pending;
}

int timer_delete(struct timer_list *timer)
{
	int was_pending;

	if (!hid_timer_mutex || !hid_timer_lock())
		return 0;
	was_pending = hid_timer_delete_pending_locked(timer);
	(void)hid_timer_unlock();
	return was_pending;
}

int timer_delete_sync(struct timer_list *timer)
{
	struct hid_timer_waiter waiter = {
		.timer = timer,
		.task = xTaskGetCurrentTaskHandle(),
	};
	bool can_wait;
	bool linked;
	bool unlinked;
	int was_pending;

	/*
	 * Linux waits for a running timer callback here. TinyUSB disconnect only
	 * publishes cancellation to the report task, so this wait cannot block the
	 * host owner which must make USB progress.
	 */
	if (!hid_timer_mutex || !hid_timer_lock())
		return 0;
	was_pending = hid_timer_delete_pending_locked(timer);
	if (!timer->running) {
		(void)hid_timer_unlock();
		return was_pending;
	}

	/* The sole timer task cannot synchronously wait for its own callback. */
	can_wait = waiter.task != hid_timer_task_handle;
	if (!can_wait) {
		(void)hid_timer_unlock();
		async_msg("ERR: HID_TIMER_SELF_WAIT");
		configASSERT(can_wait);
		return was_pending;
	}
	linked = hid_timer_waiter_link_locked(&waiter);
	if (!linked) {
		(void)hid_timer_unlock();
		async_msg("ERR: HID_TIMER_WAITER_BAD");
		configASSERT(linked);
		return was_pending;
	}
	(void)hid_timer_unlock();

	for (;;) {
		(void)ulTaskNotifyTakeIndexed(HID_TIMER_NOTIFY_INDEX, pdTRUE,
					       portMAX_DELAY);
		/* Once linked, keep the stack waiter alive until it is unlinked. */
		while (!hid_timer_lock())
			vTaskDelay(1);
		/*
		 * A running Linux timer callback may rearm itself. Remove that new
		 * pending instance before accepting the callback-complete condition;
		 * otherwise teardown could free an object still linked in this wheel.
		 */
		was_pending |= hid_timer_delete_pending_locked(timer);
		if (!timer->running) {
			unlinked = hid_timer_waiter_unlink_locked(&waiter);
			(void)hid_timer_unlock();
			if (!unlinked) {
				async_msg("ERR: HID_TIMER_WAITER_LOST");
				configASSERT(unlinked);
			}
			return was_pending;
		}
		(void)hid_timer_unlock();
	}
}

void hid_timer_task(void *pvParameters)
{
	(void)pvParameters;

	while (!hid_timer_lock())
		vTaskDelay(1);
	hid_timer_task_handle = xTaskGetCurrentTaskHandle();
	(void)hid_timer_unlock();

	for (;;) {
		struct timer_list **link;
		struct timer_list *timer = NULL;
		TickType_t wait_ticks = portMAX_DELAY;
		unsigned long now;

		if (!hid_timer_lock()) {
			vTaskDelay(1);
			continue;
		}
		now = jiffies;
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
		(void)hid_timer_unlock();

		if (timer) {
			if (timer->function)
				timer->function(timer);

			/* running retains timer memory; never abandon its release. */
			while (!hid_timer_lock())
				vTaskDelay(1);
			timer->running = 0;
			hid_timer_wake_waiters_locked(timer);
			(void)hid_timer_unlock();
			continue;
		}

		(void)ulTaskNotifyTakeIndexed(HID_TIMER_NOTIFY_INDEX, pdTRUE,
					       wait_ticks);
	}
}
