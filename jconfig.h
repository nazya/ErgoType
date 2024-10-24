#ifndef CONFIG_H
#define CONFIG_H
#include "matrix.h"

typedef enum {
    SUCCESS,         // Default value: 0
    NO_JSON,         // Default value: 1
    NO_GPIO_COLS,    // Default value: 2
    NO_GPIO_ROWS,    // Default value: 3
    INVALID_JSON,    // Default value: 4
    KEYMAP_ERROR
} ParseStatus;

// Include the config field definitions
#define CONFIG_FIELDS \
    FIELD(uart_rx_pin, int, 0)  \
    FIELD(uart_tx_pin, int, 1)  \
    FIELD(led_pin, int, -1)  \
    FIELD(scan_period, int, 5)  \
    FIELD(debounce, int, 5)  \
    FIELD(has_diodes, int, 1)

// Define the cfg struct using the X-Macro
typedef struct {
    #define FIELD(name, type, default_value) type name;
    CONFIG_FIELDS
    #undef FIELD
    matrix_t matrix;
} cfg;

// Function prototypes
int parse(cfg *config, const char *filename);
// void init_default_cfg(cfg *config);
// int set_cfg_value(cfg *config, const char *field_name, int value);

#endif // CONFIG_H