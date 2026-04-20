#include "FreeRTOS.h"
#include "task.h"
#include "bsp/board.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h" // For `to_us_since_boot()`

#define CAPACITIVE_SENSOR_PIN 6    // GPIO pin for capacitive sensor input
#define UART_BAUD_RATE       115200
#define TASK_STACK_SIZE      2048
#define TASK_PRIORITY        5


#define TIMEOUT_THRESHOLD 10000

uint8_t mode = 0;  // Mode variable to toggle between different behaviors

//===========================================================================
// Read one “raw” RC time tick count. Return (end_tick − start_tick) in microseconds.
//===========================================================================
static uint32_t read_raw_rc_time(void) {
    uint32_t total = 0;  // Variable to hold the total time taken

    // 1) Discharge: Set the pin as output and drive it low
    gpio_set_dir(CAPACITIVE_SENSOR_PIN, GPIO_OUT);  // Set the pin as output
    gpio_put(CAPACITIVE_SENSOR_PIN, 0);              // Drive the pin low to discharge the capacitor
    sleep_us(10);                                   // Ensure the line is fully discharged

    // 2) Switch to input (high-impedance) to start measuring the charging time
    gpio_disable_pulls(CAPACITIVE_SENSOR_PIN);  // Properly disable internal resistors
    gpio_set_dir(CAPACITIVE_SENSOR_PIN, GPIO_IN);    // Set the pin to input (high impedance)

    // 3) Start timing (in microseconds)
    uint32_t start_time = to_us_since_boot(get_absolute_time());

    // 4) Wait until the pin reads high (external resistor has charged CR node)
    while (gpio_get(CAPACITIVE_SENSOR_PIN) == 0) {
        // Spin until the pad crosses the input threshold (pin goes high)
        total++;
        if (total > TIMEOUT_THRESHOLD) {  // Timeout condition to avoid infinite wait
            printf("Timeout reached: %u us\n", total);  // Debugging output
            return -1;  // Timeout error
        }
    }

    uint32_t end_time = to_us_since_boot(get_absolute_time());
    return (end_time - start_time); // Return the time difference in microseconds
}


//===========================================================================
// FreeRTOS task: measure raw RC-time every 100 ms and print it. Also toggle mode.
//===========================================================================
static void capacitive_task(void *pvParameters) {
    (void)pvParameters;
    printf("Entering task...\n");


    for (;;) {
        uint32_t raw_us = read_raw_rc_time();  // Measure the RC time in microseconds
        if (raw_us == -1) {
            printf("Timeout error in capacitive measurement\n");
        } else {
            printf("Raw RC time (us): %u\n", (unsigned)raw_us);  // Print the measured value in microseconds
        }

        vTaskDelay(pdMS_TO_TICKS(30));  // Delay 100 ms between measurements
    }
}

//===========================================================================
// main(): initialize GPIO, create the FreeRTOS task, and start the scheduler.
//===========================================================================
int main(void) {
    board_init();
    stdio_init_all();  // Initialize USB serial output for printf

    // Initialize the capacitive sensor pin (GPIO6)
    gpio_init(CAPACITIVE_SENSOR_PIN);
    gpio_set_dir(CAPACITIVE_SENSOR_PIN, GPIO_OUT);  // Initially set as output to discharge the capacitor

    // Create the FreeRTOS task for capacitive measurement
    xTaskCreate(
        capacitive_task,          // Task function
        "capacitive_task",        // Task name
        configMINIMAL_STACK_SIZE,          // Stack size
        NULL,                     // Parameters
        TASK_PRIORITY,            // Priority
        NULL                      // Task handle (unused)
    );

    printf("Starting FreeRTOS and Capacitive Sensor...\n");

    // Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here, but just in case:
    while (1) {
        tight_loop_contents();
    }

    return 0;
}
