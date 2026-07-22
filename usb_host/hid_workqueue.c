/* Firmware-only implementation of the reduced Linux workqueue contract. */
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

static void hid_workqueue_lock(void)
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

	ready = hid_workqueue_mutex != NULL;
	configASSERT(ready);
	ret = xSemaphoreTake(hid_workqueue_mutex, portMAX_DELAY);
	locked = ret == pdPASS;
	configASSERT(locked);
}

static void hid_workqueue_unlock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool unlocked;

	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);

	ready = hid_workqueue_mutex != NULL;
	configASSERT(ready);

	ret = xSemaphoreGive(hid_workqueue_mutex);
	unlocked = ret == pdPASS;
	configASSERT(unlocked);
}

static bool hid_workqueue_wait_done_locked(
		const struct hid_workqueue_waiter *waiter)
{
	if (waiter->kind == HID_WORKQUEUE_WAIT_WORK)
		return !waiter->work->pending && !waiter->work->running;
	return !waiter->wq->head && !waiter->wq->running;
}

static void hid_workqueue_waiter_link_locked(
		struct hid_workqueue_waiter *waiter)
{
	waiter->next = hid_workqueue_waiters;
	hid_workqueue_waiters = waiter;
}

static void hid_workqueue_waiter_unlink_locked(
		struct hid_workqueue_waiter *waiter)
{
	struct hid_workqueue_waiter **link = &hid_workqueue_waiters;

	/* A stack waiter remains alive until this exact list entry is removed. */
	while (*link != waiter)
		link = &(*link)->next;
	*link = waiter->next;
	waiter->next = NULL;
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

static void hid_workqueue_wait_idle(struct hid_workqueue_waiter *waiter)
{
	TaskHandle_t caller_task;
	bool can_wait;

	hid_workqueue_lock();
	if (hid_workqueue_wait_done_locked(waiter)) {
		hid_workqueue_unlock();
		return;
	}
	/* A single firmware worker cannot synchronously wait for its own progress. */
	caller_task = xTaskGetCurrentTaskHandle();
	can_wait = caller_task != hid_workqueue_task_handle;
	configASSERT(can_wait);
	hid_workqueue_waiter_link_locked(waiter);
	hid_workqueue_unlock();

	for (;;) {
		(void)ulTaskNotifyTakeIndexed(HID_WORKQUEUE_NOTIFY_INDEX,
					      pdTRUE, portMAX_DELAY);
		/* Once linked, keep the stack waiter alive until it is unlinked. */
		hid_workqueue_lock();
		if (hid_workqueue_wait_done_locked(waiter)) {
			hid_workqueue_waiter_unlink_locked(waiter);
			hid_workqueue_unlock();
			return;
		}
		hid_workqueue_unlock();
	}
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
	TaskHandle_t task;

	if (!hid_workqueue_mutex)
		return false;

	hid_workqueue_lock();
	if (work->pending || work->cancel_depth || wq->destroying) {
		hid_workqueue_unlock();
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
	task = hid_workqueue_task_handle;
	hid_workqueue_unlock();

	if (task)
		(void)xTaskNotifyGiveIndexed(task, HID_WORKQUEUE_NOTIFY_INDEX);
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

	hid_workqueue_lock();
	wq->next = hid_workqueue_list;
	hid_workqueue_list = wq;
	hid_workqueue_unlock();

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

	hid_workqueue_lock();
	wq->destroying = 1;
	hid_workqueue_unlock();

	/* Linux destroy_workqueue() cannot return while active work owns @wq. */
	hid_workqueue_wait_idle(&waiter);

	hid_workqueue_lock();
	cursor = &hid_workqueue_list;
	while (*cursor) {
		if (*cursor == wq) {
			*cursor = wq->next;
			break;
		}
		cursor = &(*cursor)->next;
	}
	hid_workqueue_unlock();

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

	hid_workqueue_lock();
	was_active = work->pending || work->running;
	hid_workqueue_unlock();

	if (was_active)
		hid_workqueue_wait_idle(&waiter);

	return was_active;
}

bool cancel_work_sync(struct work_struct *work)
{
	struct hid_workqueue_waiter waiter = {
		.task = xTaskGetCurrentTaskHandle(),
		.kind = HID_WORKQUEUE_WAIT_WORK,
		.work = work,
	};
	bool wait_needed;
	bool was_pending;

	/*
	 * Linux waits for queued/running work here. TinyUSB unmount callbacks
	 * enqueue HID disconnect work to a firmware task before driver remove,
	 * so remove-time callers can block here without blocking TinyUSB.
	 */
	hid_workqueue_lock();
	/* Linux returns true only when this call canceled a pending instance. */
	was_pending = work->pending;
	wait_needed = was_pending || work->running;
	/* Keep queue_work() closed until every concurrent canceler has returned. */
	work->cancel_depth++;
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
	hid_workqueue_unlock();

	if (wait_needed)
		hid_workqueue_wait_idle(&waiter);

	hid_workqueue_lock();
	work->cancel_depth--;
	hid_workqueue_unlock();

	return was_pending;
}

static struct work_struct *
hid_workqueue_take_next(struct workqueue_struct **running_wq)
{
	struct workqueue_struct **link;
	struct workqueue_struct *wq;

	hid_workqueue_lock();
	link = &hid_workqueue_list;
	for (wq = *link; wq; link = &wq->next, wq = *link) {
		struct work_struct *work;
		struct workqueue_struct *tail;

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
		/*
		 * Linux worker pools do not let one ordered workqueue monopolize
		 * unrelated queues. Firmware has one worker task, so rotate the queue
		 * which supplied this item behind its siblings. A self-requeueing
		 * haptic handler then cannot starve system_wq or another device.
		 */
		if (wq->next) {
			*link = wq->next;
			tail = *link;
			while (tail->next)
				tail = tail->next;
			tail->next = wq;
			wq->next = NULL;
		}
		*running_wq = wq;
		hid_workqueue_unlock();

		return work;
	}
	hid_workqueue_unlock();

	return NULL;
}

void hid_workqueue_task(void *pvParameters)
{
	(void)pvParameters;
	hid_workqueue_lock();
	hid_workqueue_task_handle = xTaskGetCurrentTaskHandle();
	hid_workqueue_unlock();

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
		hid_workqueue_lock();
		running_wq->running = 0;
		work->running = 0;
		hid_workqueue_wake_ready_locked();
		hid_workqueue_unlock();
	}
}
