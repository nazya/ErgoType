#include <stdbool.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "linux/include/linux/hid_compat.h"

#define HID_WORKQUEUE_WAKE_LEN 1

/*
 * Upstream Linux workqueues run from kernel worker threads. This firmware
 * bridge keeps upstream workqueue call sites active while moving the callbacks
 * out of TinyUSB callbacks and into one HID workqueue task.
 */
static QueueHandle_t hid_workqueue_wake_queue;
static struct workqueue_struct hid_system_workqueue;
static struct workqueue_struct *hid_workqueue_list = &hid_system_workqueue;

struct workqueue_struct *system_wq = &hid_system_workqueue;

int hid_workqueue_init(void)
{
	hid_system_workqueue.head = NULL;
	hid_system_workqueue.tail = NULL;
	hid_system_workqueue.next = NULL;
	hid_system_workqueue.running = 0;
	hid_system_workqueue.destroying = 0;
	hid_workqueue_list = &hid_system_workqueue;

	hid_workqueue_wake_queue = xQueueCreate(HID_WORKQUEUE_WAKE_LEN,
						sizeof(uint8_t));
	if (!hid_workqueue_wake_queue)
		return -ENOMEM;

	return 0;
}

bool queue_work(struct workqueue_struct *wq, struct work_struct *work)
{
	if (!hid_workqueue_wake_queue)
		return false;

	taskENTER_CRITICAL();
	if (work->pending || work->canceling || wq->destroying) {
		taskEXIT_CRITICAL();
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
	taskEXIT_CRITICAL();

	uint8_t wake = 1;
	(void)xQueueSendToBack(hid_workqueue_wake_queue, &wake, 0);
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

	taskENTER_CRITICAL();
	wq->next = hid_workqueue_list;
	hid_workqueue_list = wq;
	taskEXIT_CRITICAL();

	return wq;
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	struct workqueue_struct **cursor;

	taskENTER_CRITICAL();
	wq->destroying = 1;
	taskEXIT_CRITICAL();

	while (wq->head || wq->running)
		vTaskDelay(1);

	taskENTER_CRITICAL();
	cursor = &hid_workqueue_list;
	while (*cursor) {
		if (*cursor == wq) {
			*cursor = wq->next;
			break;
		}
		cursor = &(*cursor)->next;
	}
	taskEXIT_CRITICAL();

	kfree(wq);
}

bool flush_work(struct work_struct *work)
{
	bool was_active;

	taskENTER_CRITICAL();
	was_active = work->pending || work->running;
	taskEXIT_CRITICAL();

	while (work->pending || work->running)
		vTaskDelay(1);

	return was_active;
}

bool cancel_work_sync(struct work_struct *work)
{
	bool was_active;

	/*
	 * Linux waits for queued/running work here. TinyUSB unmount callbacks
	 * enqueue HID disconnect work to a firmware task before driver remove,
	 * so remove-time callers can block here without blocking TinyUSB.
	 */
	taskENTER_CRITICAL();
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
	}
	taskEXIT_CRITICAL();

	for (;;) {
		bool running;

		taskENTER_CRITICAL();
		running = work->running;
		taskEXIT_CRITICAL();
		if (!running)
			break;
		vTaskDelay(1);
	}

	taskENTER_CRITICAL();
	work->canceling = 0;
	taskEXIT_CRITICAL();

	return was_active;
}

void hid_delayed_work_timer(struct timer_list *timer)
{
	struct delayed_work *dwork = container_of(timer, struct delayed_work,
						  timer);
	struct workqueue_struct *wq;

	taskENTER_CRITICAL();
	dwork->delayed_pending = 0;
	wq = dwork->wq ? dwork->wq : system_wq;
	taskEXIT_CRITICAL();

	queue_work(wq, &dwork->work);
}

bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
			unsigned long delay)
{
	if (!delay)
		return queue_work(wq, &dwork->work);

	taskENTER_CRITICAL();
	if (dwork->delayed_pending || dwork->work.pending ||
	    dwork->work.canceling) {
		taskEXIT_CRITICAL();
		return false;
	}
	dwork->wq = wq;
	dwork->delayed_pending = 1;
	taskEXIT_CRITICAL();

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

	taskENTER_CRITICAL();
	was_pending = dwork->delayed_pending || dwork->work.pending;
	dwork->wq = wq;
	dwork->delayed_pending = 1;
	taskEXIT_CRITICAL();

	if (!delay) {
		timer_delete_sync(&dwork->timer);
		taskENTER_CRITICAL();
		dwork->delayed_pending = 0;
		taskEXIT_CRITICAL();
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
	 * runs from the usbhid disconnect task, so work cancellation may block;
	 * timer deletion is still the firmware timer bridge boundary.
	 */
	was_pending = timer_delete_sync(&dwork->timer);
	taskENTER_CRITICAL();
	was_pending = was_pending || dwork->delayed_pending ||
		      dwork->work.pending;
	dwork->delayed_pending = 0;
	taskEXIT_CRITICAL();

	cancel_work_sync(&dwork->work);
	return was_pending;
}

static struct work_struct *
hid_workqueue_take_next(struct workqueue_struct **running_wq)
{
	struct workqueue_struct *wq;

	taskENTER_CRITICAL();
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
		taskEXIT_CRITICAL();

		return work;
	}
	taskEXIT_CRITICAL();

	return NULL;
}

void hid_workqueue_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct work_struct *work;
		struct workqueue_struct *running_wq;

		work = hid_workqueue_take_next(&running_wq);

		if (!work) {
			uint8_t wake;

			xQueueReceive(hid_workqueue_wake_queue, &wake,
				      portMAX_DELAY);
			continue;
		}

		work->func(work);

		taskENTER_CRITICAL();
		running_wq->running = 0;
		work->running = 0;
		taskEXIT_CRITICAL();
	}
}
