/* Test-only caller for the direct-root generic HID PM boundary. */
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "tusb.h"
#include "tusb/descriptors.h"

#include "linux/include/linux/hid.h"
#include "hid_transport_sync.h"
#include "tusb/cdc.h"
#include "usbhid.h"

#define PM_FIXTURE_RETRY_MS 100u
#define PM_FIXTURE_RETRY_TIMEOUT_MS 25000u
#define PM_FIXTURE_CALLBACK_TIMEOUT_MS 5000u
#define PM_FIXTURE_SUSPENDED_MS 250u

struct usbhid_pm_fixture_context {
	TaskHandle_t task;
	u32 input_generation;
	const struct hid_device *input_hid;
	bool input_ready;
	bool remote;
	bool wake_requested;
	bool wake_key_pressed;
	bool wake_input_returned;
	bool suspend_done;
	bool resume_done;
	int suspend_status;
	int resume_status;
	enum usbhid_pm_state expected;
	enum usbhid_pm_state completed_state;
	int status;
};

static struct usbhid_pm_fixture_context pm_fixture;

void usbhid_pm_fixture_input(const struct hid_device *hid, u32 generation,
			     const u8 *report, u32 length)
{
	static const u8 pressed[8] = { 0, 0, HID_KEY_F1, 0, 0, 0, 0, 0 };
	static const u8 released[8];
	TaskHandle_t task = NULL;
	bool remote = hid->vendor == USB_VID &&
		(hid->product == USB_PID_HID || hid->product == USB_PID_HID_NKRO);
	bool trigger;
	bool empty;

	if (hid->vendor == 0xcafe && hid->product == 0x11f0 &&
	    length == sizeof(pressed)) {
		trigger = !memcmp(report, pressed, sizeof(pressed));
		empty = !memcmp(report, released, sizeof(released));
	} else if (remote && hid->product == USB_PID_HID &&
		   length == sizeof(pressed)) {
		trigger = false;
		empty = !report[0];
		for (u32 i = 2; i < length; i++) {
			trigger |= report[i] >= HID_KEY_A && report[i] <= HID_KEY_Z;
			empty &= !report[i];
		}
	} else if (remote && hid->product == USB_PID_HID_NKRO &&
		   length == HID_NKRO_KEY_BYTES + 1 &&
		   report[0] == REPORT_ID_KEYBOARD) {
		trigger = false;
		for (u32 usage = HID_KEY_A; usage <= HID_KEY_Z; usage++)
			trigger |= (report[1 + usage / 8] & (1u << (usage % 8))) != 0;
		empty = true;
		for (u32 i = 1; i < length; i++)
			empty &= !report[i];
	} else {
		return;
	}

	hid_transport_lock();
	if (!pm_fixture.input_ready) {
		if (trigger) {
			pm_fixture.input_generation = generation;
			pm_fixture.input_hid = hid;
		} else if (pm_fixture.input_hid == hid &&
			   pm_fixture.input_generation == generation && empty) {
			pm_fixture.input_ready = true;
			pm_fixture.remote = remote;
			task = pm_fixture.task;
		} else {
			pm_fixture.input_generation = 0;
		}
	} else if (pm_fixture.remote && pm_fixture.wake_requested &&
		   pm_fixture.input_hid == hid &&
		   pm_fixture.input_generation == generation) {
		if (!empty)
			pm_fixture.wake_key_pressed = true;
		else if (pm_fixture.wake_key_pressed)
			pm_fixture.wake_input_returned = true;
		task = pm_fixture.task;
	}
	hid_transport_unlock();
	if (task)
		xTaskNotifyGive(task);
}

void usbhid_pm_wakeup_cb(void)
{
	TaskHandle_t task = NULL;

	hid_transport_lock();
	if (pm_fixture.remote) {
		pm_fixture.wake_requested = true;
		task = pm_fixture.task;
	}
	hid_transport_unlock();
	if (task)
		xTaskNotifyGive(task);
}

void usbhid_pm_state_changed_cb(enum usbhid_pm_state state, int status)
{
	TaskHandle_t task = NULL;

	hid_transport_lock();
	if (pm_fixture.remote) {
		if (state == USBHID_PM_SUSPENDED && !pm_fixture.suspend_done) {
			pm_fixture.suspend_status = status;
			pm_fixture.suspend_done = true;
		} else if (state == USBHID_PM_ACTIVE && !pm_fixture.resume_done) {
			pm_fixture.resume_status = status;
			pm_fixture.resume_done = true;
		}
		task = pm_fixture.task;
	}
	hid_transport_unlock();
	if (task)
		xTaskNotifyGive(task);
}

static bool pm_fixture_wait(const bool *ready, TickType_t timeout)
{
	TickType_t start = xTaskGetTickCount();

	for (;;) {
		hid_transport_lock();
		bool done = *ready;
		hid_transport_unlock();
		if (done)
			return true;
		TickType_t wait = portMAX_DELAY;
		if (timeout != portMAX_DELAY) {
			TickType_t elapsed = xTaskGetTickCount() - start;

			if (elapsed >= timeout)
				return false;
			wait = timeout - elapsed;
		}
		(void)ulTaskNotifyTake(pdTRUE, wait);
	}
}

static void pm_fixture_remote_test(void)
{
	TickType_t timeout = pdMS_TO_TICKS(PM_FIXTURE_CALLBACK_TIMEOUT_MS);

	usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
	if (!pm_fixture_wait(&pm_fixture.suspend_done, timeout) ||
	    pm_fixture.suspend_status) {
		async_msg("ERR: HID_PM_REMOTE_SUSPEND");
		goto finish;
	}

	async_msg("INFO: HID_PM_TEST_PRESS_KEY");
	(void)pm_fixture_wait(&pm_fixture.wake_requested, portMAX_DELAY);
	if (!pm_fixture_wait(&pm_fixture.resume_done, timeout) ||
	    pm_fixture.resume_status) {
		async_msg("ERR: HID_PM_REMOTE_RESUME");
		goto finish;
	}
	if (!pm_fixture_wait(&pm_fixture.wake_input_returned, timeout)) {
		async_msg("ERR: HID_PM_REMOTE_INPUT");
		goto finish;
	}
	async_msg("INFO: HID_PM_REMOTE_OK");
finish:
	usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
}

static bool pm_fixture_pending(TickType_t start, unsigned int milliseconds)
{
	return (TickType_t)(xTaskGetTickCount() - start) <
		pdMS_TO_TICKS(milliseconds);
}

static void pm_fixture_complete(enum usbhid_pm_state state, int status,
				void *context)
{
	struct usbhid_pm_fixture_context *fixture = context;

	fixture->completed_state = state;
	fixture->status = status;
	xTaskNotifyGive(fixture->task);
}

static int pm_fixture_transition(enum usbhid_pm_state target)
{
	TickType_t start = xTaskGetTickCount();

	while (pm_fixture_pending(start, PM_FIXTURE_RETRY_TIMEOUT_MS)) {
		pm_fixture.expected = target;
		pm_fixture.completed_state = target == USBHID_PM_ACTIVE ?
			USBHID_PM_SUSPENDED : USBHID_PM_ACTIVE;
		pm_fixture.status = -EBUSY;

		int ret = usbhid_pm_request(target, pm_fixture_complete,
					    &pm_fixture);
		if (ret)
			return ret;
		bool duplicate_busy = usbhid_pm_request(target, NULL, NULL) == -EBUSY;
		if (!ulTaskNotifyTake(pdTRUE,
					      pdMS_TO_TICKS(PM_FIXTURE_CALLBACK_TIMEOUT_MS))) {
			async_msg("ERR: HID_PM_TEST_CB_TIMEOUT");
			/* The accepted request still owns this task handle until callback. */
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			return -ETIMEDOUT;
		}
		if (!duplicate_busy)
			return -EINVAL;
		if (pm_fixture.completed_state != pm_fixture.expected)
			return -EINVAL;
		if (!pm_fixture.status)
			return 0;
		if (pm_fixture.status != -ENODEV && pm_fixture.status != -EBUSY)
			return pm_fixture.status;
		vTaskDelay(pdMS_TO_TICKS(PM_FIXTURE_RETRY_MS));
	}

	return -ETIMEDOUT;
}

void usbhid_pm_fixture_task(void *pvParameters)
{
	bool input_ready;
	int ret;

	(void)pvParameters;
	hid_transport_lock();
	pm_fixture.task = xTaskGetCurrentTaskHandle();
	input_ready = pm_fixture.input_ready;
	hid_transport_unlock();
	async_msg("INFO: HID_PM_TEST_WAIT_KEY");
	while (!input_ready) {
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		hid_transport_lock();
		input_ready = pm_fixture.input_ready;
		hid_transport_unlock();
	}
	(void)ulTaskNotifyTake(pdTRUE, 0);
	if (pm_fixture.remote) {
		pm_fixture_remote_test();
		/* Callbacks retain this handle even after a failed or completed test. */
		vTaskSuspend(NULL);
		return;
	}

	ret = pm_fixture_transition(USBHID_PM_SUSPENDED);
	if (ret) {
		async_msg("ERR: HID_PM_TEST_SUSPEND");
		vTaskDelete(NULL);
		return;
	}
	if (usbhid_pm_request(USBHID_PM_SUSPENDED, NULL, NULL) != -EALREADY) {
		async_msg("ERR: HID_PM_TEST_REPEAT");
		vTaskDelete(NULL);
		return;
	}

	vTaskDelay(pdMS_TO_TICKS(PM_FIXTURE_SUSPENDED_MS));
	ret = pm_fixture_transition(USBHID_PM_ACTIVE);
	if (ret) {
		async_msg("ERR: HID_PM_TEST_RESUME");
		vTaskDelete(NULL);
		return;
	}
	if (usbhid_pm_request(USBHID_PM_ACTIVE, NULL, NULL) != -EALREADY) {
		async_msg("ERR: HID_PM_TEST_ACTIVE");
		vTaskDelete(NULL);
		return;
	}

	async_msg("INFO: HID_PM_TEST_OK");
	vTaskDelete(NULL);
}
