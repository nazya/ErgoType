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

#define MAX_LINE_LENGTH 4096


#define MAX_GPIOS 16 

#if (MAX_GPIOS <= 8)
typedef uint8_t matrix_row_t;
#elif (MAX_GPIOS <= 16)
typedef uint16_t matrix_row_t;
#elif (MAX_GPIOS <= 32)
typedef uint32_t matrix_row_t;
#else
#    error "MAX_GPIOS: invalid value"
#endif


typedef struct {
    int gpio_cols[MAX_GPIOS];      // GPIO pins for columns
    int gpio_rows[MAX_GPIOS];      // GPIO pins for rows
    uint8_t keymap[MAX_GPIOS][MAX_GPIOS]; // Keycode mapping [row][col]
    uint8_t nr_cols;                  // Number of columns
    uint8_t nr_rows;                  // Number of rows
} matrix_t;

// int parse_matrix_file(matrix_t *matrix);
#endif // MATRIX_H
