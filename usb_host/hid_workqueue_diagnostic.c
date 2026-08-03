/* Test-only public-API coverage for the reduced HID workqueue and ktime port. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "devmon.h"
#include "linux/include/linux/hid_compat.h"
#include "linux/include/linux/power_supply.h"
#include "stdio_tusb_cdc.h"

#define HID_TIME_TEST_CORE1 ((UBaseType_t)(1u << 1))
#define HID_TIME_TEST_CANCEL_PRIORITY (tskIDLE_PRIORITY + 5)
#define HID_TIME_TEST_FIRST_CANCEL_PRIORITY (tskIDLE_PRIORITY + 6)
#define HID_TIME_TEST_GATE_TIMEOUT_MS 5000u
#define HID_TIME_TEST_TASK_TIMEOUT_MS 7000u
#define HID_TIME_TEST_FIRST_POWER_TIMEOUT_MS 15000u
#define HID_TIME_TEST_POWER_TIMEOUT_MS 40000u
#define HID_TIME_TEST_WRAP_MARGIN_TICKS pdMS_TO_TICKS(2000u)
#define HID_TIME_TEST_WRAP_EARLY_TICKS pdMS_TO_TICKS(500u)

enum hid_time_test_item {
	HID_TIME_WRAP_WORK,
	HID_TIME_CANCEL_WORK,
	HID_TIME_HOLD_WORK,
	HID_TIME_BLOCKER_WORK,
	HID_TIME_TARGET_WORK,
	HID_TIME_RUNNING_NONSYNC_WORK,
	HID_TIME_RUNNING_WORK,
	HID_TIME_REQUEUE_WORK,
	HID_TIME_CONCURRENT_WORK,
	HID_TIME_DESTROY_WORK,
	HID_TIME_WORK_COUNT,
};

enum hid_time_test_failure {
	HID_TIME_TEST_OK,
	HID_TIME_TEST_ALLOC,
	HID_TIME_TEST_KTIME,
	HID_TIME_TEST_CANCEL,
	HID_TIME_TEST_PROMOTED,
	HID_TIME_TEST_RUNNING,
	HID_TIME_TEST_REQUEUE,
	HID_TIME_TEST_CONCURRENT,
	HID_TIME_TEST_DESTROY,
	HID_TIME_TEST_POWER,
};

struct hid_time_test_work {
	struct delayed_work dwork;
	struct workqueue_struct *queue;
	SemaphoreHandle_t entered;
	SemaphoreHandle_t release;
	SemaphoreHandle_t completed;
	volatile uint32_t callbacks;
	volatile uint8_t requeue_until;
	volatile bool block_first;
	volatile bool gate_timed_out;
	volatile bool signal_failed;
	volatile bool requeue_attempted;
	volatile bool requeue_result;
	volatile bool callback_exited;
	TickType_t requeue_tick;
	TickType_t callback_tick[2];
};

struct hid_time_test_context;

struct hid_time_test_canceler {
	struct hid_time_test_context *context;
	struct hid_time_test_work *item;
	volatile bool execute;
	volatile bool result;
	volatile bool returned_after_callback;
	bool probe_requeue;
	volatile bool probe_requeue_result;
};

struct hid_time_test_flusher {
	struct hid_time_test_context *context;
	struct hid_time_test_work *item;
};

struct hid_time_test_context {
	struct hid_time_test_work work[HID_TIME_WORK_COUNT];
	SemaphoreHandle_t wrap_done;
	SemaphoreHandle_t hold_entered;
	SemaphoreHandle_t hold_release;
	SemaphoreHandle_t blocker_entered;
	SemaphoreHandle_t blocker_release;
	SemaphoreHandle_t nonsync_entered;
	SemaphoreHandle_t nonsync_release;
	SemaphoreHandle_t nonsync_done;
	SemaphoreHandle_t running_entered;
	SemaphoreHandle_t running_release;
	SemaphoreHandle_t requeue_done;
	SemaphoreHandle_t concurrent_entered;
	SemaphoreHandle_t concurrent_release;
	SemaphoreHandle_t cancel_go;
	SemaphoreHandle_t cancel_started;
	SemaphoreHandle_t cancel_done;
	SemaphoreHandle_t flush_done;
	SemaphoreHandle_t observer_ready;
	SemaphoreHandle_t power_done;
	SemaphoreHandle_t destroy_done;
	struct hid_time_test_canceler canceler[2];
	struct hid_time_test_flusher flusher[2];
	volatile bool power_ok;
	bool retain_context;
	TickType_t wrap_start;
	TickType_t wrap_delay;
};

static bool hid_time_test_take(SemaphoreHandle_t semaphore, uint32_t timeout_ms)
{
	return xSemaphoreTake(semaphore, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

static bool hid_time_test_give(SemaphoreHandle_t semaphore)
{
	return xSemaphoreGive(semaphore) == pdPASS;
}

static void hid_time_test_work_callback(struct work_struct *work)
{
	struct hid_time_test_work *item =
		container_of(to_delayed_work(work),
			     struct hid_time_test_work, dwork);
	uint32_t callback_index = item->callbacks;

	item->callbacks = callback_index + 1u;
	if (callback_index < ARRAY_SIZE(item->callback_tick))
		item->callback_tick[callback_index] = xTaskGetTickCount();

	if (callback_index == 0u && item->entered &&
	    !hid_time_test_give(item->entered))
		item->signal_failed = true;

	if (callback_index == 0u && item->block_first &&
	    !hid_time_test_take(item->release,
				HID_TIME_TEST_GATE_TIMEOUT_MS))
		item->gate_timed_out = true;

	if (item->callbacks < item->requeue_until) {
		item->requeue_tick = xTaskGetTickCount();
		item->requeue_result =
			queue_delayed_work(item->queue, &item->dwork,
					   pdMS_TO_TICKS(2u));
		item->requeue_attempted = true;
	}

	item->callback_exited = true;
	if (item->completed && !hid_time_test_give(item->completed))
		item->signal_failed = true;
}

static void hid_time_test_init_work(struct hid_time_test_work *item,
				    SemaphoreHandle_t entered,
				    SemaphoreHandle_t release,
				    SemaphoreHandle_t completed)
{
	INIT_DELAYED_WORK(&item->dwork, hid_time_test_work_callback);
	item->queue = system_wq;
	item->entered = entered;
	item->release = release;
	item->completed = completed;
}

static void hid_time_test_delete_semaphore(SemaphoreHandle_t semaphore)
{
	if (semaphore)
		vSemaphoreDelete(semaphore);
}

static void hid_time_test_context_deinit(struct hid_time_test_context *context)
{
	hid_time_test_delete_semaphore(context->wrap_done);
	hid_time_test_delete_semaphore(context->hold_entered);
	hid_time_test_delete_semaphore(context->hold_release);
	hid_time_test_delete_semaphore(context->blocker_entered);
	hid_time_test_delete_semaphore(context->blocker_release);
	hid_time_test_delete_semaphore(context->nonsync_entered);
	hid_time_test_delete_semaphore(context->nonsync_release);
	hid_time_test_delete_semaphore(context->nonsync_done);
	hid_time_test_delete_semaphore(context->running_entered);
	hid_time_test_delete_semaphore(context->running_release);
	hid_time_test_delete_semaphore(context->requeue_done);
	hid_time_test_delete_semaphore(context->concurrent_entered);
	hid_time_test_delete_semaphore(context->concurrent_release);
	hid_time_test_delete_semaphore(context->cancel_go);
	hid_time_test_delete_semaphore(context->cancel_started);
	hid_time_test_delete_semaphore(context->cancel_done);
	hid_time_test_delete_semaphore(context->flush_done);
	hid_time_test_delete_semaphore(context->observer_ready);
	hid_time_test_delete_semaphore(context->power_done);
	hid_time_test_delete_semaphore(context->destroy_done);
	vPortFree(context);
}

static bool hid_time_test_context_init(struct hid_time_test_context *context)
{
	memset(context, 0, sizeof(*context));

	context->wrap_done = xSemaphoreCreateBinary();
	context->hold_entered = xSemaphoreCreateBinary();
	context->hold_release = xSemaphoreCreateBinary();
	context->blocker_entered = xSemaphoreCreateBinary();
	context->blocker_release = xSemaphoreCreateBinary();
	context->nonsync_entered = xSemaphoreCreateBinary();
	context->nonsync_release = xSemaphoreCreateBinary();
	context->nonsync_done = xSemaphoreCreateCounting(2u, 0u);
	context->running_entered = xSemaphoreCreateBinary();
	context->running_release = xSemaphoreCreateBinary();
	context->requeue_done = xSemaphoreCreateCounting(2u, 0u);
	context->concurrent_entered = xSemaphoreCreateBinary();
	context->concurrent_release = xSemaphoreCreateBinary();
	context->cancel_go = xSemaphoreCreateCounting(2u, 0u);
	context->cancel_started = xSemaphoreCreateCounting(2u, 0u);
	context->cancel_done = xSemaphoreCreateCounting(2u, 0u);
	context->flush_done = xSemaphoreCreateCounting(2u, 0u);
	context->observer_ready = xSemaphoreCreateBinary();
	context->power_done = xSemaphoreCreateBinary();
	context->destroy_done = xSemaphoreCreateBinary();
	if (!context->wrap_done || !context->hold_entered ||
	    !context->hold_release || !context->blocker_entered ||
	    !context->blocker_release || !context->nonsync_entered ||
	    !context->nonsync_release || !context->nonsync_done ||
	    !context->running_entered ||
	    !context->running_release || !context->requeue_done ||
	    !context->concurrent_entered || !context->concurrent_release ||
	    !context->cancel_go || !context->cancel_started ||
	    !context->cancel_done || !context->flush_done ||
	    !context->observer_ready ||
	    !context->power_done || !context->destroy_done)
		return false;

	hid_time_test_init_work(&context->work[HID_TIME_WRAP_WORK], NULL,
				NULL, context->wrap_done);
	hid_time_test_init_work(&context->work[HID_TIME_CANCEL_WORK], NULL,
				NULL, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_HOLD_WORK],
				context->hold_entered,
				context->hold_release, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_BLOCKER_WORK],
				context->blocker_entered,
				context->blocker_release, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_TARGET_WORK], NULL,
				NULL, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_RUNNING_NONSYNC_WORK],
				context->nonsync_entered,
				context->nonsync_release,
				context->nonsync_done);
	hid_time_test_init_work(&context->work[HID_TIME_RUNNING_WORK],
				context->running_entered,
				context->running_release, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_REQUEUE_WORK], NULL,
				NULL, context->requeue_done);
	hid_time_test_init_work(&context->work[HID_TIME_CONCURRENT_WORK],
				context->concurrent_entered,
				context->concurrent_release, NULL);
	hid_time_test_init_work(&context->work[HID_TIME_DESTROY_WORK], NULL,
				NULL, NULL);
	return true;
}

static bool hid_time_test_power_values(
	const struct port_power_supply_snapshot *snapshot)
{
	return snapshot->fields ==
			(PORT_POWER_SUPPLY_HAS_STATUS |
			 PORT_POWER_SUPPLY_HAS_ONLINE |
			 PORT_POWER_SUPPLY_HAS_CAPACITY |
			 PORT_POWER_SUPPLY_HAS_PRESENT) &&
	       snapshot->status == POWER_SUPPLY_STATUS_DISCHARGING &&
	       snapshot->capacity == 50 && snapshot->present &&
	       snapshot->online &&
	       strcmp(snapshot->name,
		      "hid-KTIME-BATTERY-1-battery-1") == 0 &&
	       strcmp(snapshot->model,
		      "ErgoType Ktime Battery Test") == 0;
}

static void hid_time_test_power_observer(void *pvParameters)
{
	struct hid_time_test_context *context = pvParameters;
	struct port_power_supply_snapshot event;
	QueueHandle_t event_queue;
	QueueHandle_t owned_queue = NULL;
	TickType_t first_tick = 0;
	uint32_t proxy_id = 0;
	unsigned int added_count = 0;
	unsigned int changed_count = 0;
	unsigned int removed_count = 0;
	bool ok = true;

	if (!hid_time_test_give(context->observer_ready))
		ok = false;

	event_queue = xQueueSelectFromSet(
		power_supply_event_set,
		pdMS_TO_TICKS(HID_TIME_TEST_FIRST_POWER_TIMEOUT_MS));
	if (!event_queue || xQueueReceive(event_queue, &event, 0) != pdPASS) {
		ok = false;
		goto out;
	}

	owned_queue = event_queue;
	first_tick = xTaskGetTickCount();
	proxy_id = event.proxy_id;
	if (event.event_type == PORT_POWER_SUPPLY_ADDED) {
		TickType_t ticks_to_wrap = (TickType_t)(0u - first_tick);

		added_count++;
		if (!hid_time_test_power_values(&event) ||
		    ticks_to_wrap < pdMS_TO_TICKS(1500u) ||
		    ticks_to_wrap > pdMS_TO_TICKS(9500u))
			ok = false;
	} else {
		ok = false;
		if (event.event_type == PORT_POWER_SUPPLY_CHANGED)
			changed_count++;
		else if (event.event_type == PORT_POWER_SUPPLY_REMOVED)
			removed_count++;
	}

	if (event.event_type == PORT_POWER_SUPPLY_REMOVED) {
		devmon_remove_power_supply(owned_queue);
		owned_queue = NULL;
		goto out;
	}

	while ((TickType_t)(xTaskGetTickCount() - first_tick) <
	       pdMS_TO_TICKS(HID_TIME_TEST_POWER_TIMEOUT_MS)) {
		TickType_t elapsed = xTaskGetTickCount() - first_tick;
		TickType_t remaining =
			pdMS_TO_TICKS(HID_TIME_TEST_POWER_TIMEOUT_MS) - elapsed;

		event_queue = xQueueSelectFromSet(power_supply_event_set,
						 remaining);
		if (!event_queue)
			break;
		if (xQueueReceive(event_queue, &event, 0) != pdPASS) {
			ok = false;
			continue;
		}
		if (event_queue != owned_queue || event.proxy_id != proxy_id) {
			ok = false;
			if (event.event_type == PORT_POWER_SUPPLY_REMOVED)
				devmon_remove_power_supply(event_queue);
			continue;
		}

		elapsed = xTaskGetTickCount() - first_tick;
		switch (event.event_type) {
		case PORT_POWER_SUPPLY_ADDED:
			added_count++;
			ok = false;
			break;
		case PORT_POWER_SUPPLY_CHANGED:
			changed_count++;
			if (!hid_time_test_power_values(&event) ||
			    elapsed < pdMS_TO_TICKS(30000u) ||
			    elapsed > pdMS_TO_TICKS(34000u))
				ok = false;
			break;
		case PORT_POWER_SUPPLY_REMOVED:
			removed_count++;
			devmon_remove_power_supply(owned_queue);
			owned_queue = NULL;
			goto out;
		}
	}

out:
	if (owned_queue)
		ok = false;
	if (added_count != 1u || changed_count != 1u ||
	    removed_count != 1u)
		ok = false;
	context->power_ok = ok;
	(void)hid_time_test_give(context->power_done);
	vTaskDelete(NULL);
}

static void hid_time_test_canceler_task(void *pvParameters)
{
	struct hid_time_test_canceler *canceler = pvParameters;
	struct hid_time_test_context *context = canceler->context;

	if (!hid_time_test_take(context->cancel_go,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		(void)hid_time_test_give(context->cancel_done);
		vTaskDelete(NULL);
		return;
	}
	if (!canceler->execute) {
		(void)hid_time_test_give(context->cancel_done);
		vTaskDelete(NULL);
		return;
	}

	(void)hid_time_test_give(context->cancel_started);
	canceler->result =
		cancel_delayed_work_sync(&canceler->item->dwork);
	if (canceler->probe_requeue)
		canceler->probe_requeue_result = queue_delayed_work(
			canceler->item->queue, &canceler->item->dwork,
			pdMS_TO_TICKS(1000u));
	canceler->returned_after_callback = canceler->item->callback_exited;
	(void)hid_time_test_give(context->cancel_done);
	vTaskDelete(NULL);
}

static bool hid_time_test_create_canceler(
	struct hid_time_test_context *context, unsigned int index,
	struct hid_time_test_work *item, UBaseType_t priority,
	bool probe_requeue)
{
	struct hid_time_test_canceler *canceler = &context->canceler[index];
	BaseType_t result;

	memset(canceler, 0, sizeof(*canceler));
	canceler->context = context;
	canceler->item = item;
	canceler->execute = true;
	canceler->probe_requeue = probe_requeue;
	result = xTaskCreateAffinitySet(
		hid_time_test_canceler_task, "hid-time-cancel",
		configMINIMAL_STACK_SIZE, canceler,
		priority, HID_TIME_TEST_CORE1, NULL);
	return result == pdPASS;
}

static void hid_time_test_flusher_task(void *pvParameters)
{
	struct hid_time_test_flusher *flusher = pvParameters;
	struct hid_time_test_context *context = flusher->context;

	(void)flush_work(&flusher->item->dwork.work);
	(void)hid_time_test_give(context->flush_done);
	vTaskDelete(NULL);
}

static bool hid_time_test_create_flusher(
	struct hid_time_test_context *context, unsigned int index,
	struct hid_time_test_work *item)
{
	struct hid_time_test_flusher *flusher = &context->flusher[index];

	flusher->context = context;
	flusher->item = item;
	return xTaskCreateAffinitySet(
		hid_time_test_flusher_task, "hid-time-flush",
		configMINIMAL_STACK_SIZE, flusher,
		HID_TIME_TEST_CANCEL_PRIORITY, HID_TIME_TEST_CORE1,
		NULL) == pdPASS;
}

static bool hid_time_test_flush_bounded(
	struct hid_time_test_context *context,
	struct hid_time_test_work *item)
{
	if (!hid_time_test_create_flusher(context, 0u, item) ||
	    !hid_time_test_take(context->flush_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		context->retain_context = true;
		return false;
	}
	return true;
}

static bool hid_time_test_start_wrap(struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_WRAP_WORK];
	TickType_t ticks_to_wrap;

	if (!ktime_after((ktime_t)2u, (ktime_t)1u) ||
	    ktime_after((ktime_t)1u, (ktime_t)2u) ||
	    ktime_after((ktime_t)1u, (ktime_t)1u) ||
	    !ktime_after((ktime_t)0u, (ktime_t)UINT32_MAX) ||
	    ktime_after((ktime_t)UINT32_MAX, (ktime_t)0u))
		return false;

	context->wrap_start = xTaskGetTickCount();
	if ((context->wrap_start & ((TickType_t)1u << 31)) == 0u)
		return false;
	ticks_to_wrap = (TickType_t)(0u - context->wrap_start);
	context->wrap_delay = ticks_to_wrap + HID_TIME_TEST_WRAP_MARGIN_TICKS;
	return queue_delayed_work(system_wq, &item->dwork,
				  context->wrap_delay);
}

static bool hid_time_test_cancel_before_deadline(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_CANCEL_WORK];
	bool queued;
	bool canceled;
	bool canceled_again;

	queued = queue_delayed_work(system_wq, &item->dwork,
				    pdMS_TO_TICKS(100u));
	canceled = cancel_delayed_work(&item->dwork);
	canceled_again = cancel_delayed_work(&item->dwork);
	vTaskDelay(pdMS_TO_TICKS(120u));
	if (!queued || !canceled || canceled_again || item->callbacks != 0u) {
		context->retain_context = true;
		return false;
	}

	queued = queue_delayed_work(system_wq, &item->dwork,
				    pdMS_TO_TICKS(100u));
	canceled = cancel_delayed_work_sync(&item->dwork);
	canceled_again = cancel_delayed_work_sync(&item->dwork);
	vTaskDelay(pdMS_TO_TICKS(120u));
	if (!queued || !canceled || canceled_again || item->callbacks != 0u) {
		context->retain_context = true;
		return false;
	}
	return true;
}

static bool hid_time_test_promoted_cancel(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *hold = &context->work[HID_TIME_HOLD_WORK];
	struct hid_time_test_work *blocker =
		&context->work[HID_TIME_BLOCKER_WORK];
	struct hid_time_test_work *target =
		&context->work[HID_TIME_TARGET_WORK];
	unsigned int flushers_expected = 0;
	unsigned int flushers_created = 0;
	unsigned int flushers_done = 0;
	bool hold_queued = false;
	bool blocker_queued = false;
	bool ok = true;

	hold->block_first = true;
	blocker->block_first = true;
	if (!queue_work(system_wq, &hold->dwork.work)) {
		ok = false;
		goto out;
	}
	hold_queued = true;
	if (!hid_time_test_take(context->hold_entered,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	if (!queue_work(system_wq, &blocker->dwork.work)) {
		ok = false;
		goto out;
	}
	blocker_queued = true;
	if (!queue_delayed_work(system_wq, &target->dwork,
				pdMS_TO_TICKS(2u))) {
		ok = false;
		goto out;
	}
	vTaskDelay(pdMS_TO_TICKS(10u));
	if (!hid_time_test_give(context->hold_release) ||
	    !hid_time_test_take(context->blocker_entered,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	if (!cancel_delayed_work_sync(&target->dwork) ||
	    target->callbacks != 0u)
		ok = false;

out:
	(void)hid_time_test_give(context->hold_release);
	(void)hid_time_test_give(context->blocker_release);
	(void)cancel_delayed_work_sync(&target->dwork);
	if (hold_queued) {
		flushers_expected++;
		if (hid_time_test_create_flusher(context, flushers_created,
						 hold))
			flushers_created++;
		else
			ok = false;
	}
	if (blocker_queued) {
		flushers_expected++;
		if (hid_time_test_create_flusher(context, flushers_created,
						 blocker))
			flushers_created++;
		else
			ok = false;
	}
	while (flushers_done < flushers_created) {
		if (!hid_time_test_take(context->flush_done,
					HID_TIME_TEST_TASK_TIMEOUT_MS)) {
			context->retain_context = true;
			return false;
		}
		flushers_done++;
	}
	if (flushers_created != flushers_expected)
		context->retain_context = true;
	return ok && !hold->gate_timed_out && !blocker->gate_timed_out &&
	       !hold->signal_failed && !blocker->signal_failed &&
	       target->callbacks == 0u;
}

static bool hid_time_test_running_nonsync_cancel(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item =
		&context->work[HID_TIME_RUNNING_NONSYNC_WORK];
	bool canceled;
	bool cleanup_canceled;

	item->block_first = true;
	item->requeue_until = 2u;
	if (!queue_work(system_wq, &item->dwork.work))
		return false;
	if (!hid_time_test_take(context->nonsync_entered,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		(void)hid_time_test_give(context->nonsync_release);
		context->retain_context = true;
		return false;
	}

	canceled = cancel_delayed_work(&item->dwork);
	if (!hid_time_test_give(context->nonsync_release)) {
		context->retain_context = true;
		return false;
	}
	if (!hid_time_test_take(context->nonsync_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS) ||
	    !hid_time_test_take(context->nonsync_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		context->retain_context = true;
		return false;
	}

	if (!hid_time_test_flush_bounded(context, item))
		return false;
	cleanup_canceled = cancel_delayed_work_sync(&item->dwork);
	return !canceled && item->callbacks == 2u &&
	       item->requeue_attempted && item->requeue_result &&
	       item->callback_exited && !item->gate_timed_out &&
	       !item->signal_failed &&
	       (TickType_t)(item->callback_tick[1] - item->requeue_tick) >=
			pdMS_TO_TICKS(2u) &&
	       !cleanup_canceled;
}

static bool hid_time_test_running_cancel(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_RUNNING_WORK];
	bool canceler_created = false;
	bool canceler_started = false;
	bool canceler_done = false;
	bool cleanup_canceled;
	bool ok = true;

	item->block_first = true;
	item->requeue_until = 2u;
	if (!hid_time_test_create_canceler(
			context, 0u, item, HID_TIME_TEST_CANCEL_PRIORITY,
			false))
		return false;
	canceler_created = true;
	if (!queue_work(system_wq, &item->dwork.work) ||
	    !hid_time_test_take(context->running_entered,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	if (!hid_time_test_give(context->cancel_go) ||
	    !hid_time_test_take(context->cancel_started,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	canceler_started = true;
	if (!hid_time_test_give(context->running_release) ||
	    !hid_time_test_take(context->cancel_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	canceler_done = true;

out:
	(void)hid_time_test_give(context->running_release);
	if (canceler_created && !canceler_started) {
		context->canceler[0].execute = false;
		(void)hid_time_test_give(context->cancel_go);
	}
	if (canceler_created && !canceler_done)
		canceler_done = hid_time_test_take(
			context->cancel_done, HID_TIME_TEST_TASK_TIMEOUT_MS);
	if (!canceler_done) {
		context->retain_context = true;
		return false;
	}
	cleanup_canceled = cancel_delayed_work_sync(&item->dwork);
	return ok && item->callbacks == 1u && item->requeue_attempted &&
	       !item->requeue_result && item->callback_exited &&
	       !item->gate_timed_out && !item->signal_failed &&
	       !context->canceler[0].result &&
	       context->canceler[0].returned_after_callback &&
	       !cleanup_canceled;
}

static bool hid_time_test_normal_requeue(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_REQUEUE_WORK];
	bool cleanup_canceled;

	item->requeue_until = 2u;
	if (!queue_work(system_wq, &item->dwork.work))
		return false;
	if (!hid_time_test_take(context->requeue_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		context->retain_context = true;
		return false;
	}
	if (!hid_time_test_take(context->requeue_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		context->retain_context = true;
		return false;
	}

	if (!hid_time_test_flush_bounded(context, item))
		return false;
	cleanup_canceled = cancel_delayed_work_sync(&item->dwork);
	return item->callbacks == 2u && item->requeue_attempted &&
	       item->requeue_result && !item->signal_failed &&
	       (TickType_t)(item->callback_tick[1] - item->requeue_tick) >=
			pdMS_TO_TICKS(2u) &&
	       !cleanup_canceled;
}

static bool hid_time_test_concurrent_cancel(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item =
		&context->work[HID_TIME_CONCURRENT_WORK];
	unsigned int created = 0;
	unsigned int started = 0;
	unsigned int done = 0;
	bool cleanup_canceled;
	bool ok = true;

	item->block_first = true;
	item->requeue_until = 2u;
	for (unsigned int i = 0; i < 2u; ++i) {
		UBaseType_t priority = i == 0u ?
			HID_TIME_TEST_FIRST_CANCEL_PRIORITY :
			HID_TIME_TEST_CANCEL_PRIORITY;

		if (!hid_time_test_create_canceler(context, i, item,
						 priority, i == 0u)) {
			ok = false;
			goto out;
		}
		created++;
	}
	if (!queue_work(system_wq, &item->dwork.work) ||
	    !hid_time_test_take(context->concurrent_entered,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		ok = false;
		goto out;
	}
	for (unsigned int i = 0; i < created; ++i) {
		if (!hid_time_test_give(context->cancel_go) ||
		    !hid_time_test_take(context->cancel_started,
					HID_TIME_TEST_TASK_TIMEOUT_MS)) {
			ok = false;
			goto out;
		}
		started++;
	}
	if (!hid_time_test_give(context->concurrent_release))
		ok = false;
	for (unsigned int i = 0; i < created; ++i) {
		if (!hid_time_test_take(context->cancel_done,
					HID_TIME_TEST_TASK_TIMEOUT_MS)) {
			ok = false;
			goto out;
		}
		done++;
	}

out:
	(void)hid_time_test_give(context->concurrent_release);
	for (unsigned int i = started; i < created; ++i) {
		context->canceler[i].execute = false;
		(void)hid_time_test_give(context->cancel_go);
	}
	while (done < created) {
		if (!hid_time_test_take(context->cancel_done,
					HID_TIME_TEST_TASK_TIMEOUT_MS)) {
			context->retain_context = true;
			return false;
		}
		done++;
	}
	cleanup_canceled = cancel_delayed_work_sync(&item->dwork);
	if (!ok || item->callbacks != 1u || !item->requeue_attempted ||
	    item->requeue_result || item->gate_timed_out ||
	    item->signal_failed || context->canceler[0].result ||
	    context->canceler[1].result ||
	    context->canceler[0].probe_requeue_result ||
	    !context->canceler[0].returned_after_callback ||
	    !context->canceler[1].returned_after_callback ||
	    cleanup_canceled)
		return false;

	item->block_first = false;
	item->requeue_until = 0u;
	if (!queue_delayed_work(system_wq, &item->dwork,
				pdMS_TO_TICKS(1000u)))
		return false;
	return cancel_delayed_work_sync(&item->dwork) &&
	       item->callbacks == 1u;
}

static void hid_time_test_destroyer_task(void *pvParameters)
{
	struct hid_time_test_context *context = pvParameters;
	struct hid_time_test_work *item =
		&context->work[HID_TIME_DESTROY_WORK];

	destroy_workqueue(item->queue);
	(void)hid_time_test_give(context->destroy_done);
	vTaskDelete(NULL);
}

static bool hid_time_test_destroy_delayed(
	struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_DESTROY_WORK];
	struct workqueue_struct *queue =
		create_singlethread_workqueue("hid-time-destroy");
	BaseType_t task_created;

	if (!queue)
		return false;
	item->queue = queue;
	item->requeue_until = 2u;
	if (!queue_delayed_work(queue, &item->dwork,
				pdMS_TO_TICKS(60000u))) {
		destroy_workqueue(queue);
		return false;
	}

	/* Firmware promotes timer-side work before destroy; Linux requires cancel. */
	task_created = xTaskCreateAffinitySet(
		hid_time_test_destroyer_task, "hid-time-destroy",
		configMINIMAL_STACK_SIZE, context, tskIDLE_PRIORITY + 4,
		HID_TIME_TEST_CORE1, NULL);
	if (task_created != pdPASS) {
		(void)cancel_delayed_work_sync(&item->dwork);
		destroy_workqueue(queue);
		return false;
	}
	if (!hid_time_test_take(context->destroy_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		context->retain_context = true;
		return false;
	}
	return item->callbacks == 1u && item->requeue_attempted &&
	       !item->requeue_result && !item->signal_failed;
}

static bool hid_time_test_finish_wrap(struct hid_time_test_context *context)
{
	struct hid_time_test_work *item = &context->work[HID_TIME_WRAP_WORK];
	TickType_t elapsed = xTaskGetTickCount() - context->wrap_start;
	TickType_t early = context->wrap_delay - HID_TIME_TEST_WRAP_EARLY_TICKS;
	bool cleanup_canceled;

	if (elapsed < early)
		vTaskDelay(early - elapsed);
	if (item->callbacks != 0u) {
		if (hid_time_test_flush_bounded(context, item))
			(void)cancel_delayed_work_sync(&item->dwork);
		return false;
	}
	if (!hid_time_test_take(context->wrap_done,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		if (hid_time_test_flush_bounded(context, item))
			(void)cancel_delayed_work_sync(&item->dwork);
		return false;
	}
	if (!hid_time_test_flush_bounded(context, item))
		return false;
	cleanup_canceled = cancel_delayed_work_sync(&item->dwork);
	return item->callbacks == 1u &&
	       item->callback_tick[0] < context->wrap_start &&
	       (TickType_t)(item->callback_tick[0] - context->wrap_start) >=
			context->wrap_delay &&
	       !item->signal_failed &&
	       !cleanup_canceled;
}

static void hid_time_test_report(enum hid_time_test_failure failure)
{
	switch (failure) {
	case HID_TIME_TEST_OK:
		async_msg("INFO: HID_TIME_WORK_TEST_OK");
		break;
	case HID_TIME_TEST_ALLOC:
		async_msg("ERR: HID_TIME_WORK_TEST_A");
		break;
	case HID_TIME_TEST_KTIME:
		async_msg("ERR: HID_TIME_WORK_TEST_B");
		break;
	case HID_TIME_TEST_CANCEL:
		async_msg("ERR: HID_TIME_WORK_TEST_C");
		break;
	case HID_TIME_TEST_PROMOTED:
		async_msg("ERR: HID_TIME_WORK_TEST_D");
		break;
	case HID_TIME_TEST_RUNNING:
		async_msg("ERR: HID_TIME_WORK_TEST_E");
		break;
	case HID_TIME_TEST_REQUEUE:
		async_msg("ERR: HID_TIME_WORK_TEST_F");
		break;
	case HID_TIME_TEST_CONCURRENT:
		async_msg("ERR: HID_TIME_WORK_TEST_G");
		break;
	case HID_TIME_TEST_DESTROY:
		async_msg("ERR: HID_TIME_WORK_TEST_H");
		break;
	case HID_TIME_TEST_POWER:
		async_msg("ERR: HID_TIME_WORK_TEST_P");
		break;
	}
}

void hid_workqueue_diagnostic_task(void *pvParameters)
{
	struct hid_time_test_context *context;
	enum hid_time_test_failure failure = HID_TIME_TEST_OK;
	BaseType_t observer_created;
	bool observer_done;
	bool wrap_started;

	(void)pvParameters;
	context = pvPortMalloc(sizeof(*context));
	if (!context) {
		hid_time_test_report(HID_TIME_TEST_ALLOC);
		vTaskDelete(NULL);
		return;
	}
	if (!hid_time_test_context_init(context)) {
		hid_time_test_context_deinit(context);
		hid_time_test_report(HID_TIME_TEST_ALLOC);
		vTaskDelete(NULL);
		return;
	}

	observer_created = xTaskCreateAffinitySet(
		hid_time_test_power_observer, "hid-time-power",
		configMINIMAL_STACK_SIZE, context, tskIDLE_PRIORITY + 4,
		HID_TIME_TEST_CORE1, NULL);
	if (observer_created != pdPASS) {
		hid_time_test_context_deinit(context);
		hid_time_test_report(HID_TIME_TEST_ALLOC);
		vTaskDelete(NULL);
		return;
	}
	if (!hid_time_test_take(context->observer_ready,
				HID_TIME_TEST_TASK_TIMEOUT_MS)) {
		hid_time_test_report(HID_TIME_TEST_ALLOC);
		vTaskDelete(NULL);
		return;
	}

	wrap_started = hid_time_test_start_wrap(context);
	if (!wrap_started)
		failure = HID_TIME_TEST_KTIME;
	else if (!hid_time_test_cancel_before_deadline(context))
		failure = HID_TIME_TEST_CANCEL;
	else if (!hid_time_test_promoted_cancel(context))
		failure = HID_TIME_TEST_PROMOTED;
	else if (!hid_time_test_running_nonsync_cancel(context) ||
		 !hid_time_test_running_cancel(context))
		failure = HID_TIME_TEST_RUNNING;
	else if (!hid_time_test_normal_requeue(context))
		failure = HID_TIME_TEST_REQUEUE;
	else if (!hid_time_test_concurrent_cancel(context))
		failure = HID_TIME_TEST_CONCURRENT;
	else if (!hid_time_test_destroy_delayed(context))
		failure = HID_TIME_TEST_DESTROY;

	if (wrap_started && !context->retain_context &&
	    !hid_time_test_finish_wrap(context) &&
	    failure == HID_TIME_TEST_OK)
		failure = HID_TIME_TEST_KTIME;

	observer_done = hid_time_test_take(
		context->power_done,
		HID_TIME_TEST_FIRST_POWER_TIMEOUT_MS +
		HID_TIME_TEST_POWER_TIMEOUT_MS);
	if (!observer_done) {
		if (failure == HID_TIME_TEST_OK)
			failure = HID_TIME_TEST_POWER;
		hid_time_test_report(failure);
		vTaskDelete(NULL);
		return;
	}
	if (!context->power_ok && failure == HID_TIME_TEST_OK)
		failure = HID_TIME_TEST_POWER;

	if (!context->retain_context)
		hid_time_test_context_deinit(context);
	hid_time_test_report(failure);
	vTaskDelete(NULL);
}
