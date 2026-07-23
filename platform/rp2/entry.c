#include "FreeRTOS.h"
#include "task.h"

void app_start(void);

int main(void)
{
    app_start();
    vTaskStartScheduler();

    while (1) {
    }
    return 0;
}
