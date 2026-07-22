/* Firmware-only implementation of the shared transport state-domain lock. */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "tusb.h"

#include "hid_transport_sync.h"
#include "stdio_tusb_cdc.h"

static SemaphoreHandle_t hid_transport_mutex;

bool hid_transport_sync_init(void)
{
	bool uninitialized;

	/*
	 * The migrated scopes do not nest. Use a normal priority-inheritance mutex
	 * so an accidental recursive acquisition is exposed instead of hidden. The
	 * common state domain couples generation/lifetime and recovery admission;
	 * fixed-slot callback handoff itself is already task-owned and asynchronous.
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
	ready = hid_transport_mutex != NULL;
	configASSERT(ready);
	ret = xSemaphoreTake(hid_transport_mutex, portMAX_DELAY);
	locked = ret == pdPASS;
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
	ready = hid_transport_mutex != NULL;
	configASSERT(ready);
	ret = xSemaphoreGive(hid_transport_mutex);
	unlocked = ret == pdPASS;
	configASSERT(unlocked);
}
