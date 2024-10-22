#include <stdint.h>
#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// #include <time.h>   // For time(

void usleep(uint32_t timeout_us) {
    // Convert microseconds to ticks
    TickType_t ticks = pdMS_TO_TICKS((timeout_us / 1000) + (timeout_us % 1000 ? 1 : 0));
    
    // Delay the task for the calculated number of ticks
    vTaskDelay(ticks);
}

void _keyd_log(int level, const char *fmt, ...)
{
	// if (level > log_level)
	// 	return;

	// va_list ap;
	// va_start(ap, fmt);
	printf(fmt);
	// va_end(ap);
}