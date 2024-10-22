// matrix.h
#ifndef MATRIX_H
#define MATRIX_H

#include <stdint.h> 
#include "pico/stdlib.h"  // Raspberry Pi Pico SDK

// #include <stdbool.h>

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include "pico/stdlib.h"  // Raspberry Pi Pico SDK
// #include "ff.h"           // FatFS for TinyUSB
// #include "keyd.h"         // Contains keycode_table and other keyd definitions

#define MAX_GPIOS 16 
#define MAX_LINE_LENGTH 4096

typedef struct {
    uint8_t gpio_cols[MAX_GPIOS];      // GPIO pins for columns
    uint8_t gpio_rows[MAX_GPIOS];      // GPIO pins for rows
    uint8_t keymap[MAX_GPIOS][MAX_GPIOS]; // Keycode mapping [row][col]
    uint8_t num_cols;                  // Number of columns
    uint8_t num_rows;                  // Number of rows
} matrix_t;

int parse_matrix_file(matrix_t *matrix);
#endif // MATRIX_H
