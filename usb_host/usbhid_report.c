#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include "linux/include/linux/hid.h"
#include "stdio_tusb_cdc.h"
#include "usbhid_report.h"

/*
 * Interrupt-IN executor. TinyUSB owns a report buffer only while RX is armed;
 * its callback moves ownership to a bounded copied event and returns. The
 * complete Linux HID input path runs in task context, then the TinyUSB host
 * task is asked to arm the next transfer.
 *
 * There is exactly one owner per HID interface:
 *
 *   STOPPED -> ARMED -> QUEUED -> ACTIVE -> STOPPED
 *
 * A host-defer reservation is tracked separately because it also retains the
 * hid pointer. close() only clears WANTED: an already armed transfer may stay
 * parked until one report completes, but it cannot create a second RX chain.
 */
/* One interrupt report per HID can be queued, plus the single active ctrl GET. */
#define USBHID_REPORT_QUEUE_LEN (CFG_TUH_HID + 1)

enum usbhid_report_event_kind {
	USBHID_REPORT_EVENT_INTERRUPT,
	USBHID_REPORT_EVENT_CONTROL,
};

enum usbhid_report_owner {
	USBHID_REPORT_STOPPED,
	USBHID_REPORT_ARMED,
	USBHID_REPORT_QUEUED,
	USBHID_REPORT_ACTIVE,
};

enum usbhid_report_host_action {
	USBHID_REPORT_HOST_NONE,
	USBHID_REPORT_HOST_ARM,
	USBHID_REPORT_HOST_ABORT,
};

struct usbhid_report_event {
	enum usbhid_report_event_kind kind;
	struct hid_device *hid;
	u32 generation;
	u16 len;
	bool parse;
	union {
		u8 interrupt[CFG_TUH_HID_EPIN_BUFSIZE];
		struct {
			u8 *data;
			void *context;
			usbhid_control_report_done_t done;
			TaskHandle_t parser_owner;
			u16 bufsize;
			u8 report_type;
		} control;
	} payload;
};

static QueueHandle_t usbhid_report_queue;
static bool usbhid_control_report_queued;
static struct usbhid_report_event usbhid_control_report_handoff;
static bool usbhid_control_report_handoff_ready;

static void usbhid_report_reconcile_on_host(void *data);

int usbhid_report_init(void)
{
	usbhid_report_queue = xQueueCreate(USBHID_REPORT_QUEUE_LEN,
					  sizeof(struct usbhid_report_event));
	return usbhid_report_queue ? 0 : -ENOMEM;
}

int usbhid_report_start(struct hid_device *hid)
{
	bool defer;

	if (!hid || !usbhid_report_queue)
		return -ENODEV;

	taskENTER_CRITICAL();
	if (hid->ll_transport_stopping) {
		taskEXIT_CRITICAL();
		return -ENODEV;
	}
	hid->ll_report_wanted = true;
	hid->ll_report_revision++;
	defer = hid->ll_report_owner == USBHID_REPORT_STOPPED &&
		!hid->ll_report_host_pending;
	if (defer)
		hid->ll_report_host_pending = true;
	taskEXIT_CRITICAL();

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
	return 0;
}

void usbhid_report_close(struct hid_device *hid)
{
	if (!hid)
		return;

	taskENTER_CRITICAL();
	hid->ll_report_wanted = false;
	hid->ll_report_revision++;
	taskEXIT_CRITICAL();
}

int usbhid_report_submit(struct hid_device *hid, const uint8_t *report,
			 uint16_t len, bool parse)
{
	struct usbhid_report_event event = {
		.kind = USBHID_REPORT_EVENT_INTERRUPT,
		.hid = hid,
		.len = len,
		.parse = parse,
	};
	int status = 0;

	if (!usbhid_report_queue)
		return -ENODEV;
	if (!hid)
		return -ENODEV;
	/* A completed transfer must advance ownership even if its payload is bad. */
	if (!report || len > CFG_TUH_HID_EPIN_BUFSIZE) {
		event.len = 0;
		event.parse = false;
		status = -EMSGSIZE;
	}

	if (event.len)
		memcpy(event.payload.interrupt, report, event.len);
	taskENTER_CRITICAL();
	if (hid->ll_report_owner != USBHID_REPORT_ARMED) {
		taskEXIT_CRITICAL();
		return -EBUSY;
	}
	if (hid->ll_transport_stopping || !hid->ll_report_wanted) {
		hid->ll_report_owner = USBHID_REPORT_STOPPED;
		taskEXIT_CRITICAL();
		return 0;
	}
	event.generation = hid->ll_generation;
	hid->ll_report_owner = USBHID_REPORT_QUEUED;
	taskEXIT_CRITICAL();

	if (xQueueSendToBack(usbhid_report_queue, &event, 0) != pdPASS) {
		taskENTER_CRITICAL();
		if (hid->ll_report_owner == USBHID_REPORT_QUEUED)
			hid->ll_report_owner = USBHID_REPORT_STOPPED;
		taskEXIT_CRITICAL();
		return -EBUSY;
	}

	return status;
}

int usbhid_control_report_submit(struct hid_device *hid, uint8_t report_type,
				 uint8_t *report, uint16_t bufsize,
				 uint16_t len, TaskHandle_t parser_owner,
				 usbhid_control_report_done_t done,
				 void *context)
{
	struct usbhid_report_event event = {
		.kind = USBHID_REPORT_EVENT_CONTROL,
		.hid = hid,
		.len = len,
		.parse = true,
		.payload.control = {
			.data = report,
			.context = context,
			.done = done,
			.parser_owner = parser_owner,
			.bufsize = bufsize,
			.report_type = report_type,
		},
	};

	if (!usbhid_report_queue || !hid || !report || !done)
		return -ENODEV;
	if (len > bufsize)
		return -EMSGSIZE;

	taskENTER_CRITICAL();
	if (hid->ll_transport_stopping) {
		taskEXIT_CRITICAL();
		return -ENODEV;
	}
	event.generation = hid->ll_generation;
	taskEXIT_CRITICAL();

	/*
	 * Keep one explicit control slot beyond the one interrupt slot per HID.
	 * A second completed GET is dropped instead of consuming interrupt capacity
	 * or blocking the transport owner behind parser-side synchronous I/O.
	 */
	taskENTER_CRITICAL();
	if (usbhid_control_report_queued) {
		taskEXIT_CRITICAL();
		return -EBUSY;
	}
	usbhid_control_report_queued = true;
	taskEXIT_CRITICAL();

	if (xQueueSendToBack(usbhid_report_queue, &event, 0) != pdPASS) {
		taskENTER_CRITICAL();
		usbhid_control_report_queued = false;
		taskEXIT_CRITICAL();
		return -EBUSY;
	}

	return 0;
}

void usbhid_report_stop(struct hid_device *hid)
{
	bool defer = false;

	if (!hid)
		return;

	taskENTER_CRITICAL();
	hid->ll_transport_stopping = true;
	hid->ll_report_wanted = false;
	hid->ll_report_revision++;
	if (hid->ll_report_owner == USBHID_REPORT_ARMED &&
	    !hid->ll_report_host_pending) {
		hid->ll_report_host_pending = true;
		defer = true;
	}
	taskEXIT_CRITICAL();

	if (defer)
		usbh_defer_func(usbhid_report_reconcile_on_host, hid, false);
}

void usbhid_report_unplug(struct hid_device *hid)
{
	if (!hid)
		return;

	taskENTER_CRITICAL();
	hid->ll_transport_stopping = true;
	hid->ll_report_wanted = false;
	hid->ll_report_revision++;
	/* TinyUSB closes the physical endpoint around its unmount callback. */
	if (hid->ll_report_owner == USBHID_REPORT_ARMED)
		hid->ll_report_owner = USBHID_REPORT_STOPPED;
	taskEXIT_CRITICAL();
}

bool usbhid_report_is_stopping(struct hid_device *hid)
{
	bool stopping;

	if (!hid)
		return true;

	taskENTER_CRITICAL();
	stopping = hid->ll_transport_stopping;
	taskEXIT_CRITICAL();
	return stopping;
}

static bool usbhid_report_idle(struct hid_device *hid)
{
	bool idle;

	taskENTER_CRITICAL();
	idle = hid->ll_report_owner == USBHID_REPORT_STOPPED &&
	       !hid->ll_report_host_pending;
	taskEXIT_CRITICAL();
	return idle;
}

void usbhid_report_wait_idle(struct hid_device *hid)
{
	while (!usbhid_report_idle(hid))
		vTaskDelay(1);
}

static void usbhid_report_reconcile_on_host(void *data)
{
	struct hid_device *hid = data;
	enum usbhid_report_host_action action;
	u32 revision;
	bool ok;

	for (;;) {
		taskENTER_CRITICAL();
		if (hid->ll_transport_stopping &&
		    hid->ll_report_owner == USBHID_REPORT_ARMED) {
			hid->ll_report_owner = USBHID_REPORT_STOPPED;
			action = USBHID_REPORT_HOST_ABORT;
		} else if (!hid->ll_transport_stopping &&
			   hid->ll_report_wanted &&
			   hid->ll_report_owner == USBHID_REPORT_STOPPED) {
			hid->ll_report_owner = USBHID_REPORT_ARMED;
			action = USBHID_REPORT_HOST_ARM;
		} else {
			action = USBHID_REPORT_HOST_NONE;
		}
		if (action == USBHID_REPORT_HOST_NONE) {
			hid->ll_report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}
		revision = hid->ll_report_revision;
		taskEXIT_CRITICAL();

		if (action == USBHID_REPORT_HOST_ABORT) {
			if (tuh_hid_mounted(hid->dev_addr, hid->instance) &&
			    !tuh_hid_receive_ready(hid->dev_addr, hid->instance))
				(void)tuh_hid_receive_abort(hid->dev_addr,
							   hid->instance);
			taskENTER_CRITICAL();
			hid->ll_report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}

		ok = tuh_hid_mounted(hid->dev_addr, hid->instance) &&
		     tuh_hid_receive_report(hid->dev_addr, hid->instance);
		if (ok) {
			taskENTER_CRITICAL();
			if (!hid->ll_transport_stopping) {
				hid->ll_report_host_pending = false;
				taskEXIT_CRITICAL();
				return;
			}
			taskEXIT_CRITICAL();
			continue;
		}

		async_msg("ERR: HID_RX_REARM_FAIL");
		taskENTER_CRITICAL();
		if (hid->ll_report_owner == USBHID_REPORT_ARMED)
			hid->ll_report_owner = USBHID_REPORT_STOPPED;
		if (hid->ll_report_revision == revision) {
			hid->ll_report_wanted = false;
			hid->ll_report_host_pending = false;
			taskEXIT_CRITICAL();
			return;
		}
		taskEXIT_CRITICAL();
	}
}

static void usbhid_control_report_finish(struct usbhid_report_event *event,
					 int status)
{
	taskENTER_CRITICAL();
	configASSERT(usbhid_control_report_queued);
	usbhid_control_report_queued = false;
	taskEXIT_CRITICAL();

	/* Parsing is part of upstream control-I/O completion. */
	event->payload.control.done(event->hid,
				    event->payload.control.context, status);
}

bool usbhid_control_report_process_owned(struct hid_device *hid)
{
	struct usbhid_report_event event;
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	bool process;

	if (!hid || !sema_owned_by_current(&hid->driver_input_lock))
		return false;

	taskENTER_CRITICAL();
	if (!usbhid_control_report_handoff_ready ||
	    usbhid_control_report_handoff.hid != hid ||
	    usbhid_control_report_handoff.payload.control.parser_owner != task ||
	    hid->ll_control_waiter != task) {
		taskEXIT_CRITICAL();
		return false;
	}
	event = usbhid_control_report_handoff;
	usbhid_control_report_handoff_ready = false;
	process = event.generation == hid->ll_generation &&
		  !hid->ll_transport_stopping;
	taskEXIT_CRITICAL();

	usbhid_control_report_finish(&event,
		process && event.parse ?
			hid_safe_input_report_locked(
				hid,
				(enum hid_report_type)
					event.payload.control.report_type,
				event.payload.control.data,
				event.payload.control.bufsize,
				event.len, 0) : -ENODEV);
	return true;
}

void usbhid_report_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct usbhid_report_event event;
		bool defer = false;
		bool process;

		if (xQueuePeek(usbhid_report_queue, &event,
			       portMAX_DELAY) != pdPASS)
			continue;

		if (event.kind == USBHID_REPORT_EVENT_CONTROL) {
			TaskHandle_t parser_owner =
				event.payload.control.parser_owner;
			bool acquired = false;
			int status = -ENODEV;

			taskENTER_CRITICAL();
			process = event.generation == event.hid->ll_generation &&
				  !event.hid->ll_transport_stopping;
			taskEXIT_CRITICAL();

			/*
			 * A probe-time GET belongs to the lifecycle task already holding
			 * driver_input_lock. Keep the event at the queue head until that
			 * task enters hid_hw_wait(), then hand off only this control report.
			 */
			if (process && event.parse && parser_owner &&
			    sema_owned_by_task(&event.hid->driver_input_lock,
					       parser_owner)) {
				bool waiter_ready;

				taskENTER_CRITICAL();
				waiter_ready =
					event.hid->ll_control_waiter == parser_owner &&
					!usbhid_control_report_handoff_ready;
				taskEXIT_CRITICAL();
				if (!waiter_ready) {
					vTaskDelay(1);
					continue;
				}

				if (xQueueReceive(usbhid_report_queue, &event, 0) !=
				    pdPASS)
					continue;
				taskENTER_CRITICAL();
				configASSERT(!usbhid_control_report_handoff_ready);
				usbhid_control_report_handoff = event;
				usbhid_control_report_handoff_ready = true;
				taskEXIT_CRITICAL();
				/* The owner runs one priority below this task. */
				vTaskDelay(1);
				continue;
			}

			/*
			 * Acquire the parser lock explicitly. A driver's raw_event may also
			 * return -EBUSY, so its return value must never be mistaken for lock
			 * contention and replayed.
			 */
			if (process && event.parse) {
				if (down_trylock(&event.hid->driver_input_lock)) {
					vTaskDelay(1);
					continue;
				}
				acquired = true;
			}

			if (xQueueReceive(usbhid_report_queue, &event, 0) != pdPASS) {
				if (acquired)
					up(&event.hid->driver_input_lock);
				continue;
			}
			if (acquired)
				status = hid_safe_input_report_locked(
					event.hid,
					(enum hid_report_type)
						event.payload.control.report_type,
					event.payload.control.data,
					event.payload.control.bufsize,
					event.len, 0);
			if (acquired)
				up(&event.hid->driver_input_lock);
			usbhid_control_report_finish(&event, status);
			continue;
		}

		if (xQueueReceive(usbhid_report_queue, &event, 0) != pdPASS)
			continue;

		taskENTER_CRITICAL();
		process = event.hid->ll_report_owner == USBHID_REPORT_QUEUED &&
			  event.generation == event.hid->ll_generation &&
			  event.hid->ll_report_wanted &&
			  !event.hid->ll_transport_stopping;
		if (event.hid->ll_report_owner == USBHID_REPORT_QUEUED)
			event.hid->ll_report_owner = USBHID_REPORT_ACTIVE;
		taskEXIT_CRITICAL();

		if (process && event.parse)
			(void)hid_safe_input_report(event.hid,
						HID_INPUT_REPORT,
						event.payload.interrupt,
						sizeof(event.payload.interrupt),
						event.len, 1);

		taskENTER_CRITICAL();
		if (event.hid->ll_report_owner == USBHID_REPORT_ACTIVE)
			event.hid->ll_report_owner = USBHID_REPORT_STOPPED;
		if (!event.hid->ll_transport_stopping &&
		    event.hid->ll_report_wanted &&
		    !event.hid->ll_report_host_pending) {
			event.hid->ll_report_host_pending = true;
			defer = true;
		}
		taskEXIT_CRITICAL();

		if (defer)
			usbh_defer_func(usbhid_report_reconcile_on_host,
					event.hid, false);
	}
}
