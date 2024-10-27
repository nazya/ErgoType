#ifndef CONFIG_H
#define CONFIG_H


#define MAX_GPIOS 16 
#define TOTAL_GPIOS 25 

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
    FIELD(uart_rx_pin, int, 0)  \
    FIELD(uart_tx_pin, int, 1)  \
    FIELD(led_pin, int, -1)  \
    FIELD(scan_period, int, 5)  \
    FIELD(debounce, int, 5)  \
    FIELD(has_diodes, int, 1)

extern int log_level;

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

typedef enum {
    SUCCESS,         // Default value: 0
    NO_JSON,         // Default value: 1
    NO_GPIO_COLS,    // Default value: 2
    NO_GPIO_ROWS,    // Default value: 3
    INVALID_JSON,    // Default value: 4
    KEYMAP_ERROR
} ParseStatus;

// Function prototypes
int parse(config_t *config, const char *filename);

#endif // CONFIG_H