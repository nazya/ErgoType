 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "log.h"

extern char errstr[ERRSTR_LENGTH];
extern int log_level;

#define IS_GPIO_PIN(pin) ((pin) >= 0 && (pin) <= 29)

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

// Include the config field definitions
#define CONFIG_FIELDS \
    FIELD(uart_rx_pin, int8_t, -1)  \
    FIELD(uart_tx_pin, int8_t, -1)  \
    FIELD(log_level, int8_t, 0)  \
    FIELD(led_pin, int8_t, -1)  \
    FIELD(erase_pin, int8_t, -1)  \
    FIELD(nr_pressed_erase, int8_t, 9)  \
    FIELD(msc_pin, int8_t, -1)  \
    FIELD(nr_pressed_msc, int8_t, 3)  \
    FIELD(scan_period, int8_t, 5)  \
    FIELD(debounce, int8_t, 9)  \
    FIELD(has_diodes, int8_t, 1)


typedef struct {
    int gpio_cols[MAX_GPIOS];      // GPIO pins for columns
    int gpio_rows[MAX_GPIOS];      // GPIO pins for rows
    uint8_t keymap[MAX_GPIOS][MAX_GPIOS]; // Keycode mapping [row][col]
    uint8_t nr_cols;                  // Number of columns
    uint8_t nr_rows;                  // Number of rows
} matrix_t;

// Define the config_t struct using the X-Macro
typedef struct {
    #define FIELD(name, type, default_value) type name;
    CONFIG_FIELDS
    #undef FIELD
    matrix_t matrix;
} config_t;

// Function prototypes
int parse(config_t *config, const char *filename);

#endif // CONFIG_H