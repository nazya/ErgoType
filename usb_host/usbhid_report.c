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
#define USBHID_REPORT_QUEUE_LEN CFG_TUH_HID

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
	struct hid_device *hid;
	u32 generation;
	u16 len;
	bool parse;
	u8 data[CFG_TUH_HID_EPIN_BUFSIZE];
};

static QueueHandle_t usbhid_report_queue;

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
	if (!report || len > sizeof(event.data)) {
		event.len = 0;
		event.parse = false;
		status = -EMSGSIZE;
	}

	if (event.len)
		memcpy(event.data, report, event.len);
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

void usbhid_report_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct usbhid_report_event event;
		bool defer = false;
		bool process;

		if (xQueueReceive(usbhid_report_queue, &event,
				  portMAX_DELAY) != pdPASS)
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
						HID_INPUT_REPORT, event.data,
						sizeof(event.data), event.len, 1);

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
