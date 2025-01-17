/*
Copyright 2011, 2012, 2013 Jun Wako <wakojun@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
Copyright 2017 Alex Ong<the.onga@gmail.com>
Copyright 2021 Simon Arlott
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

// Include necessary headers for Raspberry Pi Pico and FreeRTOS
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "jconfig.h"
#include "daemon.h"
#include "keys.h"

// Debounce time in milliseconds
static uint8_t debouncing_time; 
static bool debouncing = false;


// Separate variables for diode direction
static const uint8_t row2col = 0;  // ROW2COL configuration
static uint8_t col2row = 1;  // COL2ROW configuration

// Variables for FreeRTOS queue
extern QueueHandle_t eventQueue;

// Functions for selecting and unselecting rows and columns
static void select_row(matrix_t* matrix, uint8_t row) {
    uint8_t pin = matrix->gpio_rows[row];
    gpio_set_dir(pin, GPIO_OUT);    // Set as output
    gpio_disable_pulls(pin);        // Disable pull-up/down resistors
    gpio_put(pin, 0);               // Drive row low
}

static void unselect_row(matrix_t* matrix, uint8_t row) {
    uint8_t pin = matrix->gpio_rows[row];
    gpio_set_dir(pin, GPIO_IN);     // Set as input
    gpio_pull_up(pin);              // Enable pull-up
}

static void select_col(matrix_t* matrix, uint8_t col) {
    uint8_t pin = matrix->gpio_cols[col];
    gpio_set_dir(pin, GPIO_OUT);    // Set as output
    gpio_disable_pulls(pin);        // Disable pull-up/down resistors
    gpio_put(pin, 0);               // Drive column low
}

static void unselect_col(matrix_t* matrix, uint8_t col) {
    uint8_t pin = matrix->gpio_cols[col];
    gpio_set_dir(pin, GPIO_IN);     // Set as input
    gpio_pull_up(pin);              // Enable pull-up
}

// Initialize the matrix
void matrix_init(matrix_t* matrix) {
    if (col2row) {
        // Initialize for COL2ROW diode direction
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            uint8_t pin = matrix->gpio_rows[row];
            gpio_init(pin);
            unselect_row(matrix, row);
        }
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            uint8_t pin = matrix->gpio_cols[col];
            gpio_init(pin);
            unselect_col(matrix, col);
        }
    } else if (row2col) {
        // Initialize for ROW2COL diode direction
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            uint8_t pin = matrix->gpio_cols[col];
            gpio_init(pin);
            unselect_col(matrix, col);
        }
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            uint8_t pin = matrix->gpio_rows[row];
            gpio_init(pin);
            unselect_row(matrix, row);
        }
    } else {
        // Diodeless matrix
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            uint8_t pin = matrix->gpio_rows[row];
            gpio_init(pin);
            unselect_row(matrix, row);
        }
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            uint8_t pin = matrix->gpio_cols[col];
            gpio_init(pin);
            gpio_set_dir(pin, GPIO_IN);
            gpio_pull_up(pin);
        }
    }
}

// Helper functions for ghosting detection
static inline bool popcount_more_than_one(matrix_row_t rowdata) {
    rowdata &= rowdata - 1;  // Clears the least significant bit set
    return rowdata != 0;
}

static matrix_row_t get_real_keys(matrix_t* matrix, uint8_t row, matrix_row_t rowdata) {
    matrix_row_t out = 0;
    for (uint8_t col = 0; col < matrix->nr_cols; col++) {
        if (matrix->keymap[row][col] && (rowdata & ((matrix_row_t)1 << col))) {
            out |= ((matrix_row_t)1 << col);
        }
    }
    return out;
}

static bool has_ghost_in_row(matrix_t* matrix, uint8_t row, matrix_row_t rowdata, matrix_row_t* current_matrix) {
    if (col2row || row2col)
        return false;
    rowdata = get_real_keys(matrix, row, rowdata);
    if (!popcount_more_than_one(rowdata)) {
        return false;
    }
    for (uint8_t i = 0; i < matrix->nr_rows; i++) {
        if (i != row && popcount_more_than_one(get_real_keys(matrix, i, current_matrix[i]) & rowdata)) {
            return true;
        }
    }
    return false;
}

// Matrix scanning function
void matrix_scan(matrix_t* matrix, matrix_row_t* raw_matrix) {
    if (col2row) {
        // COL2ROW scanning
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            matrix_row_t row_value = 0;
            select_row(matrix, row);
            sleep_us(20);  // Wait for the signals to stabilize
            for (uint8_t col = 0; col < matrix->nr_cols; col++) {
                uint8_t pin = matrix->gpio_cols[col];
                uint8_t pin_state = gpio_get(pin);
                if (pin_state == 0) {  // Active low
                    row_value |= ((matrix_row_t)1 << col);
                }
            }
            unselect_row(matrix, row);
            raw_matrix[row] = row_value;
        }
    } else if (row2col) {
        // ROW2COL scanning
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            matrix_row_t col_mask = ((matrix_row_t)1 << col);
            select_col(matrix, col);
            sleep_us(20);
            for (uint8_t row = 0; row < matrix->nr_rows; row++) {
                uint8_t pin = matrix->gpio_rows[row];
                uint8_t pin_state = gpio_get(pin);
                if (pin_state == 0) {  // Active low
                    raw_matrix[row] |= col_mask;
                } else {
                    raw_matrix[row] &= ~col_mask;
                }
            }
            unselect_col(matrix, col);
        }
    } else {
        // Diodeless matrix scanning
        for (uint8_t row = 0; row < matrix->nr_rows; row++) {
            matrix_row_t row_value = 0;
            uint8_t row_pin = matrix->gpio_rows[row];
            gpio_set_dir(row_pin, GPIO_OUT);
            gpio_disable_pulls(row_pin);
            gpio_put(row_pin, 0);               // Drive current row low

            for (uint8_t r = 0; r < matrix->nr_rows; r++) {
                if (r != row) {
                    uint8_t pin = matrix->gpio_rows[r];
                    gpio_set_dir(pin, GPIO_IN);
                    gpio_pull_up(pin);
                }
            }
            sleep_us(20);
            for (uint8_t col = 0; col < matrix->nr_cols; col++) {
                uint8_t pin = matrix->gpio_cols[col];
                uint8_t pin_state = gpio_get(pin);
                if (pin_state == 0) {  // Active low
                    row_value |= ((matrix_row_t)1 << col);
                }
            }
            gpio_set_dir(row_pin, GPIO_IN);
            gpio_pull_up(row_pin);
            raw_matrix[row] = row_value;
        }
    }
}

// Debounce function
bool debounce(matrix_row_t* raw_matrix, matrix_row_t* debounced_matrix, uint8_t nr_rows) {
    static matrix_row_t matrix_debouncing[MAX_GPIOS];
    static uint32_t last_update = 0;
    bool changed = false;

    for (uint8_t i = 0; i < nr_rows; i++) {
        if (matrix_debouncing[i] != raw_matrix[i]) {
            matrix_debouncing[i] = raw_matrix[i];
            debouncing = true;
            last_update = xTaskGetTickCount();
        }
    }

    if (debouncing && ((xTaskGetTickCount() - last_update) * portTICK_PERIOD_MS > debouncing_time)) {
        for (uint8_t i = 0; i < nr_rows; i++) {
            if (debounced_matrix[i] != matrix_debouncing[i]) {
                debounced_matrix[i] = matrix_debouncing[i];
                changed = true;
            }
        }
        debouncing = false;
    }

    return changed;
}

uint8_t count_pressed_keys(matrix_t* matrix) {
    matrix_row_t raw_matrix[MAX_GPIOS] = {0};
    uint8_t pressed_count = 0;

    // Scan the matrix once
    matrix_init(matrix);
    matrix_scan(matrix, raw_matrix);

    // Count the pressed keys
    for (uint8_t row = 0; row < matrix->nr_rows; row++) {
        for (uint8_t col = 0; col < matrix->nr_cols; col++) {
            if (raw_matrix[row] & ((matrix_row_t)1 << col)) {
                pressed_count++;
            }
        }
    }

    return pressed_count;
}

// Main matrix scanning task
void keyscan_task(void* pvParameters) {
    config_t* config = (config_t*)pvParameters;
    debouncing_time = config->debounce;
    debouncing = (debouncing_time > 0);
    // matrix_t* matrix = (matrix_t*)pvParameters;
    static matrix_row_t raw_matrix[MAX_GPIOS] = {0};
    static matrix_row_t debounced_matrix[MAX_GPIOS] = {0};
    static matrix_row_t previous_debounced_matrix[MAX_GPIOS] = {0};

    // struct event ev;
    struct device_event devev;
    devev.type = DEV_KEY;

    matrix_init(&config->matrix);

    while (1) {
        TickType_t last_wake_time = xTaskGetTickCount();
        // Scan the matrix
        matrix_scan(&config->matrix, raw_matrix);

        // Debounce the matrix
        bool debounced_changed = debounce(raw_matrix, debounced_matrix, config->matrix.nr_rows);

        // Process key events only if debounced state changed
        if (debounced_changed) {
            for (uint8_t row = 0; row < config->matrix.nr_rows; row++) {
                matrix_row_t changed_keys = debounced_matrix[row] ^ previous_debounced_matrix[row];
                if (changed_keys) {
                    if (has_ghost_in_row(&config->matrix, row, debounced_matrix[row], debounced_matrix)) {
                        // Ghosting detected, ignore the keys in this row
                        debounced_matrix[row] = 0;
                    } else {
                        // Handle key events here (key pressed or released)
                        for (uint8_t col = 0; col < config->matrix.nr_cols; col++) {
                            matrix_row_t col_mask = ((matrix_row_t)1 << col);
                            if (changed_keys & col_mask) {
                                devev.pressed = (debounced_matrix[row] & col_mask) != 0;
                                devev.code = config->matrix.keymap[row][col];
                                BaseType_t xStatus = xQueueSendToBack(keyscan_queue, &devev, portMAX_DELAY);
                                // if (xStatus != pdPASS) {
                                //     err("Failed to send event to queue");
                                // }
                            }
                        }
                    }
                }
                previous_debounced_matrix[row] = debounced_matrix[row];
            }
        }
        vTaskDelayUntil(&last_wake_time, config->scan_period);
    }
}