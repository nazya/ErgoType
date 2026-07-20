#include <stdbool.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "linux/include/linux/hid_compat.h"
#include "stdio_tusb_cdc.h"

#define HID_WORKQUEUE_NOTIFY_INDEX 0u

_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES >
	       HID_WORKQUEUE_NOTIFY_INDEX,
	       "HID workqueue requires task notification index 0");
/* Index 0 is shared with task-side Linux waits; every user has a predicate. */

/*
 * Upstream Linux workqueues run from kernel worker threads. This firmware
 * bridge keeps upstream workqueue call sites active while moving the callbacks
 * out of TinyUSB callbacks and into one HID workqueue task.
 */
enum hid_workqueue_wait_kind {
	HID_WORKQUEUE_WAIT_WORK,
	HID_WORKQUEUE_WAIT_QUEUE,
};

struct hid_workqueue_waiter {
	struct hid_workqueue_waiter *next;
	TaskHandle_t task;
	enum hid_workqueue_wait_kind kind;
	union {
		struct work_struct *work;
		struct workqueue_struct *wq;
	};
	bool linked;
};

/*
 * Upstream Linux: no equivalent common mutex. Kernel workqueue internals use
 * their own pool locks and wait queues. Firmware workqueue APIs run only in
 * task context, so one priority-inheritance mutex protects this execution
 * domain without masking interrupts or sharing the USB transport mutex.
 * Live parents are the KeyD, HID timer, HID lifecycle, and workqueue tasks;
 * TinyUSB callbacks only publish lifecycle/report state and never enter here.
 */
static SemaphoreHandle_t hid_workqueue_mutex;
static TaskHandle_t hid_workqueue_task_handle;
static struct hid_workqueue_waiter *hid_workqueue_waiters;
static struct workqueue_struct hid_system_workqueue;
static struct workqueue_struct *hid_workqueue_list = &hid_system_workqueue;

struct workqueue_struct *system_wq = &hid_system_workqueue;

static bool hid_workqueue_lock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool locked;

	/*
	 * Keep FreeRTOS calls out of configASSERT(): release builds use assert(),
	 * whose argument disappears with NDEBUG.
	 */
	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return false;

	ready = hid_workqueue_mutex != NULL;
	if (!ready)
		async_msg("ERR: HID_WQ_NOT_READY");
	configASSERT(ready);
	if (!ready)
		return false;

	ret = xSemaphoreTake(hid_workqueue_mutex, portMAX_DELAY);
	locked = ret == pdPASS;
	if (!locked)
		async_msg("ERR: HID_WQ_LOCK_FAIL");
	configASSERT(locked);
	if (!locked)
		return false;

	return true;
}

static bool hid_workqueue_unlock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool unlocked;

	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return false;

	ready = hid_workqueue_mutex != NULL;
	if (!ready)
		async_msg("ERR: HID_WQ_NOT_READY");
	configASSERT(ready);
	if (!ready)
		return false;

	ret = xSemaphoreGive(hid_workqueue_mutex);
	unlocked = ret == pdPASS;
	if (!unlocked)
		async_msg("ERR: HID_WQ_UNLOCK_FAIL");
	configASSERT(unlocked);
	if (!unlocked)
		return false;

	return true;
}

static bool hid_workqueue_wait_done_locked(
		const struct hid_workqueue_waiter *waiter)
{
	if (waiter->kind == HID_WORKQUEUE_WAIT_WORK)
		return !waiter->work->pending && !waiter->work->running;
	return !waiter->wq->head && !waiter->wq->running;
}

static bool hid_workqueue_waiter_link_locked(
		struct hid_workqueue_waiter *waiter)
{
	bool valid = waiter && waiter->task && !waiter->linked;

	if (!valid)
		return false;
	waiter->next = hid_workqueue_waiters;
	waiter->linked = true;
	hid_workqueue_waiters = waiter;
	return true;
}

static bool hid_workqueue_waiter_unlink_locked(
		struct hid_workqueue_waiter *waiter)
{
	struct hid_workqueue_waiter **link = &hid_workqueue_waiters;

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

static void hid_workqueue_wake_ready_locked(void)
{
	struct hid_workqueue_waiter *waiter;

	/* Linux wake_up_all() is only an edge; every waiter rechecks its state. */
	for (waiter = hid_workqueue_waiters; waiter; waiter = waiter->next) {
		if (hid_workqueue_wait_done_locked(waiter))
			(void)xTaskNotifyGiveIndexed(
				waiter->task, HID_WORKQUEUE_NOTIFY_INDEX);
	}
}

static bool hid_workqueue_wait_idle(struct hid_workqueue_waiter *waiter)
{
	TaskHandle_t caller_task;
	bool can_wait;
	bool linked;
	bool unlinked;
	bool waiter_valid;

	waiter_valid = waiter && waiter->task;
	if (!waiter_valid)
		async_msg("ERR: HID_WQ_WAITER_BAD");
	configASSERT(waiter_valid);
	if (!waiter_valid)
		return false;

	if (!hid_workqueue_lock())
		return false;
	if (hid_workqueue_wait_done_locked(waiter)) {
		(void)hid_workqueue_unlock();
		return true;
	}
	/* A single firmware worker cannot synchronously wait for its own progress. */
	caller_task = xTaskGetCurrentTaskHandle();
	can_wait = caller_task != hid_workqueue_task_handle;
	if (!can_wait) {
		(void)hid_workqueue_unlock();
		async_msg("ERR: HID_WQ_SELF_WAIT");
		configASSERT(can_wait);
		return false;
	}
	linked = hid_workqueue_waiter_link_locked(waiter);
	if (!linked) {
		(void)hid_workqueue_unlock();
		async_msg("ERR: HID_WQ_WAITER_BAD");
		configASSERT(linked);
		return false;
	}
	(void)hid_workqueue_unlock();

	for (;;) {
		(void)ulTaskNotifyTakeIndexed(HID_WORKQUEUE_NOTIFY_INDEX,
					      pdTRUE, portMAX_DELAY);
		/* Once linked, keep the stack waiter alive until it is unlinked. */
		while (!hid_workqueue_lock())
			vTaskDelay(1);
		if (hid_workqueue_wait_done_locked(waiter)) {
			unlinked = hid_workqueue_waiter_unlink_locked(waiter);
			(void)hid_workqueue_unlock();
			if (!unlinked) {
				async_msg("ERR: HID_WQ_WAITER_LOST");
				configASSERT(unlinked);
				return false;
			}
			return true;
		}
		(void)hid_workqueue_unlock();
	}
}

static void hid_workqueue_wake_task(void)
{
	TaskHandle_t task;

	if (!hid_workqueue_lock())
		return;
	task = hid_workqueue_task_handle;
	(void)hid_workqueue_unlock();
	if (task)
		(void)xTaskNotifyGiveIndexed(task, HID_WORKQUEUE_NOTIFY_INDEX);
}

int hid_workqueue_init(void)
{
	SemaphoreHandle_t mutex;
	bool uninitialized;

	uninitialized = hid_workqueue_mutex == NULL;
	if (!uninitialized) {
		async_msg("ERR: HID_WQ_INIT_TWICE");
		configASSERT(uninitialized);
		return -EBUSY;
	}
	mutex = xSemaphoreCreateMutex();
	if (!mutex)
		return -ENOMEM;

	hid_system_workqueue.head = NULL;
	hid_system_workqueue.tail = NULL;
	hid_system_workqueue.next = NULL;
	hid_system_workqueue.running = 0;
	hid_system_workqueue.destroying = 0;
	hid_workqueue_list = &hid_system_workqueue;
	hid_workqueue_task_handle = NULL;
	hid_workqueue_waiters = NULL;
	/* Publish readiness only after every durable predicate is initialized. */
	hid_workqueue_mutex = mutex;

	return 0;
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	if (!hid_workqueue_mutex)
		return false;

	if (!hid_workqueue_lock())
		return false;
	if (work->pending || work->canceling || wq->destroying) {
		(void)hid_workqueue_unlock();
		return false;
	}
	work->pending = 1;
	work->wq = wq;
	work->next = NULL;
	if (wq->tail)
		wq->tail->next = work;
	else
		wq->head = work;
	wq->tail = work;
	(void)hid_workqueue_unlock();

	hid_workqueue_wake_task();
	return true;
}

bool schedule_work(struct work_struct *work)
{
	return queue_work(system_wq, work);
}

struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
	struct workqueue_struct *wq;

	/*
	 * Linux creates a dedicated worker thread here. Firmware has one HID
	 * workqueue task, but keeps per-workqueue FIFO lists so destroy_workqueue()
	 * can drain this ownership boundary before driver state is freed.
	 */
	(void)name;
	wq = kzalloc_obj(struct workqueue_struct);
	if (!wq)
		return NULL;

	if (!hid_workqueue_lock()) {
		kfree(wq);
		return NULL;
	}
	wq->next = hid_workqueue_list;
	hid_workqueue_list = wq;
	(void)hid_workqueue_unlock();

	return wq;
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	struct workqueue_struct **cursor;
	struct hid_workqueue_waiter waiter = {
		.task = xTaskGetCurrentTaskHandle(),
		.kind = HID_WORKQUEUE_WAIT_QUEUE,
		.wq = wq,
	};

	if (!hid_workqueue_lock())
		return;
	wq->destroying = 1;
	(void)hid_workqueue_unlock();

	/* A failed wait must not turn active driver work into a use-after-free. */
	if (!hid_workqueue_wait_idle(&waiter))
		return;

	if (!hid_workqueue_lock())
		return;
	cursor = &hid_workqueue_list;
	while (*cursor) {
		if (*cursor == wq) {
			*cursor = wq->next;
			break;
		}
		cursor = &(*cursor)->next;
	}
	(void)hid_workqueue_unlock();

	kfree(wq);
}

bool flush_work(struct work_struct *work)
{
	struct hid_workqueue_waiter waiter = {
		.task = xTaskGetCurrentTaskHandle(),
		.kind = HID_WORKQUEUE_WAIT_WORK,
		.work = work,
	};
	bool was_active;

	if (!hid_workqueue_lock())
		return false;
	was_active = work->pending || work->running;
	(void)hid_workqueue_unlock();

	if (was_active && !hid_workqueue_wait_idle(&waiter))
		return false;

	return was_active;
}

bool cancel_work_sync(struct work_struct *work)
{
	struct hid_workqueue_waiter waiter = {
		.task = xTaskGetCurrentTaskHandle(),
		.kind = HID_WORKQUEUE_WAIT_WORK,
		.work = work,
	};
	bool was_active;

	/*
	 * Linux waits for queued/running work here. TinyUSB unmount callbacks
	 * enqueue HID disconnect work to a firmware task before driver remove,
	 * so remove-time callers can block here without blocking TinyUSB.
	 */
	if (!hid_workqueue_lock())
		return false;
	was_active = work->pending || work->running;
	work->canceling = 1;
	if (work->pending) {
		struct workqueue_struct *wq = work->wq;
		struct work_struct *queued = wq->head;
		struct work_struct *previous = NULL;

		while (queued != work) {
			previous = queued;
			queued = queued->next;
		}
		if (previous)
			previous->next = work->next;
		else
			wq->head = work->next;
		if (wq->tail == work)
			wq->tail = previous;
		work->next = NULL;
		work->pending = 0;
		hid_workqueue_wake_ready_locked();
	}
	(void)hid_workqueue_unlock();

	if (was_active && !hid_workqueue_wait_idle(&waiter)) {
		if (hid_workqueue_lock()) {
			work->canceling = 0;
			(void)hid_workqueue_unlock();
		}
		return false;
	}

	if (!hid_workqueue_lock())
		return false;
	work->canceling = 0;
	(void)hid_workqueue_unlock();

	return was_active;
}

void hid_delayed_work_timer(struct timer_list *timer)
{
	struct delayed_work *dwork = container_of(timer, struct delayed_work,
						  timer);
	struct workqueue_struct *wq;

	if (!hid_workqueue_lock())
		return;
	dwork->delayed_pending = 0;
	wq = dwork->wq ? dwork->wq : system_wq;
	(void)hid_workqueue_unlock();

	queue_work(wq, &dwork->work);
}

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
			unsigned long delay)
{
	if (!delay)
		return queue_work(wq, &dwork->work);

	if (!hid_workqueue_lock())
		return false;
	if (dwork->delayed_pending || dwork->work.pending ||
	    dwork->work.canceling) {
		(void)hid_workqueue_unlock();
		return false;
	}
	dwork->wq = wq;
	dwork->delayed_pending = 1;
	(void)hid_workqueue_unlock();

	mod_timer(&dwork->timer, jiffies + delay);
	return true;
}

bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
{
	return queue_delayed_work(system_wq, dwork, delay);
}

bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
		      unsigned long delay)
{
	bool was_pending;

	if (!hid_workqueue_lock())
		return false;
	was_pending = dwork->delayed_pending || dwork->work.pending;
	dwork->wq = wq;
	dwork->delayed_pending = 1;
	(void)hid_workqueue_unlock();

	if (!delay) {
		timer_delete_sync(&dwork->timer);
		if (!hid_workqueue_lock())
			return false;
		dwork->delayed_pending = 0;
		(void)hid_workqueue_unlock();
		queue_work(wq, &dwork->work);
		return was_pending;
	}

	mod_timer(&dwork->timer, jiffies + delay);
	return was_pending;
}

bool cancel_delayed_work_sync(struct delayed_work *dwork)
{
	bool was_pending;

	/*
	 * Linux waits for queued/running delayed work here. HID driver remove now
	 * runs from the usbhid lifecycle task, so work cancellation may block;
	 * timer deletion is still the firmware timer bridge boundary.
	 */
	was_pending = timer_delete_sync(&dwork->timer);
	if (!hid_workqueue_lock())
		return false;
	was_pending = was_pending || dwork->delayed_pending ||
		      dwork->work.pending;
	dwork->delayed_pending = 0;
	(void)hid_workqueue_unlock();

	cancel_work_sync(&dwork->work);
	return was_pending;
}

static struct work_struct *
hid_workqueue_take_next(struct workqueue_struct **running_wq)
{
	struct workqueue_struct *wq;

	if (!hid_workqueue_lock())
		return NULL;
	for (wq = hid_workqueue_list; wq; wq = wq->next) {
		struct work_struct *work;

		work = wq->head;
		if (!work)
			continue;

		wq->head = work->next;
		if (!wq->head)
			wq->tail = NULL;
		work->next = NULL;
		work->pending = 0;
		work->running = 1;
		wq->running = 1;
		*running_wq = wq;
		(void)hid_workqueue_unlock();

		return work;
	}
	(void)hid_workqueue_unlock();

	return NULL;
}

void hid_workqueue_task(void *pvParameters)
{
	(void)pvParameters;
	while (!hid_workqueue_lock())
		vTaskDelay(1);
	hid_workqueue_task_handle = xTaskGetCurrentTaskHandle();
	(void)hid_workqueue_unlock();

	for (;;) {
		struct work_struct *work;
		struct workqueue_struct *running_wq;

		work = hid_workqueue_take_next(&running_wq);

		if (!work) {
			(void)ulTaskNotifyTakeIndexed(HID_WORKQUEUE_NOTIFY_INDEX,
						      pdTRUE, portMAX_DELAY);
			continue;
		}

		work->func(work);

		/* Running state owns driver memory; do not abandon it on an error. */
		while (!hid_workqueue_lock())
			vTaskDelay(1);
		running_wq->running = 0;
		work->running = 0;
		hid_workqueue_wake_ready_locked();
		(void)hid_workqueue_unlock();
	}
}
