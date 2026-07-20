#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "hid_transport_sync.h"

static SemaphoreHandle_t hid_transport_mutex;

bool hid_transport_sync_init(void)
{
	/*
	 * The migrated scopes do not nest. Use a normal priority-inheritance mutex
	 * so an accidental recursive acquisition is exposed instead of hidden;
	 * owner queues will partition this common lock further.
	 */
	configASSERT(!hid_transport_mutex);
	hid_transport_mutex = xSemaphoreCreateMutex();
	return hid_transport_mutex != NULL;
}

void hid_transport_lock(void)
{
	BaseType_t ret;

	configASSERT(hid_transport_mutex);
	configASSERT(!xPortIsInsideInterrupt());
	ret = xSemaphoreTake(hid_transport_mutex, portMAX_DELAY);
	configASSERT(ret == pdPASS);
}

void hid_transport_unlock(void)
{
	BaseType_t ret;

	configASSERT(hid_transport_mutex);
	configASSERT(!xPortIsInsideInterrupt());
	ret = xSemaphoreGive(hid_transport_mutex);
	configASSERT(ret == pdPASS);
}
