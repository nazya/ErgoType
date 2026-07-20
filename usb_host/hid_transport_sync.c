#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "hid_transport_sync.h"
#include "stdio_tusb_cdc.h"

static SemaphoreHandle_t hid_transport_mutex;

bool hid_transport_sync_init(void)
{
	bool uninitialized;

	/*
	 * The migrated scopes do not nest. Use a normal priority-inheritance mutex
	 * so an accidental recursive acquisition is exposed instead of hidden;
	 * owner queues will partition this common lock further.
	 */
	uninitialized = hid_transport_mutex == NULL;
	if (!uninitialized) {
		async_msg("ERR: HID_LOCK_INIT_TWICE");
		configASSERT(uninitialized);
		return false;
	}
	hid_transport_mutex = xSemaphoreCreateMutex();
	return hid_transport_mutex != NULL;
}

void hid_transport_lock(void)
{
	BaseType_t ret;
	bool locked;
	bool ready;
	bool task_context;

	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return;
	ready = hid_transport_mutex != NULL;
	if (!ready) {
		async_msg("ERR: HID_LOCK_NOT_READY");
		configASSERT(ready);
		return;
	}
	ret = xSemaphoreTake(hid_transport_mutex, portMAX_DELAY);
	locked = ret == pdPASS;
	if (!locked)
		async_msg("ERR: HID_LOCK_TAKE_FAIL");
	configASSERT(locked);
}

void hid_transport_unlock(void)
{
	BaseType_t ret;
	bool ready;
	bool task_context;
	bool unlocked;

	task_context = !xPortIsInsideInterrupt();
	configASSERT(task_context);
	if (!task_context)
		return;
	ready = hid_transport_mutex != NULL;
	if (!ready) {
		async_msg("ERR: HID_LOCK_NOT_READY");
		configASSERT(ready);
		return;
	}
	ret = xSemaphoreGive(hid_transport_mutex);
	unlocked = ret == pdPASS;
	if (!unlocked)
		async_msg("ERR: HID_LOCK_GIVE_FAIL");
	configASSERT(unlocked);
}
