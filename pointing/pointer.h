#ifndef POINTING_POINTER_H
#define POINTING_POINTER_H

#include <stdint.h>

typedef void *pointing_motion_task_t;

void pointing_device_task(void *pvParameters);
void pointing_motion_irq_init(pointing_motion_task_t task_handle, int8_t mot_pin, uint8_t sensor_idx);

#endif
