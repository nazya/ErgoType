#ifndef POINTING_POINTER_H
#define POINTING_POINTER_H

#include "FreeRTOS.h"
#include "task.h"

void pointing_device_task(void *pvParameters);
void pointing_motion_irq_init(TaskHandle_t task_handle);

#endif
