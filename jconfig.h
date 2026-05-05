 /*
 * ErgoType - Keyboard Solutions
 *
 * © 2024 Nazarii Tupitsa (see also: LICENSE-ErgoType).
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include "log.h"

#define IS_GPIO_PIN(pin) ((pin) >= 0 && (pin) <= 29)

#define MAX_GPIOS 8 
#define MAX_ENCODERS 2
#define MAX_PMW3360 2

#define PMW3360_ROLE_MOUSEMOVE 0u
#define PMW3360_ROLE_SCROLL    1u

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
    FIELD(ws2812_pin, int8_t, -1)  \
    FIELD(erase_pin, int8_t, -1)  \
    FIELD(nr_pressed_erase, int8_t, 9)  \
    FIELD(msc_pin, int8_t, -1)  \
    FIELD(nr_pressed_msc, int8_t, 3)  \
    FIELD(scan_period, int8_t, 5)  \
    FIELD(debounce, int8_t, 9)

typedef struct {
    uint8_t gpio_cols[MAX_GPIOS];         // GPIO pins for columns
    uint8_t gpio_rows[MAX_GPIOS];         // GPIO pins for rows
    uint8_t keymap[MAX_GPIOS][MAX_GPIOS]; // Keycode mapping [row][col]
    uint8_t nr_cols;                      // Number of columns
    uint8_t nr_rows;                      // Number of rows
} matrix_t;

#define ENCODER_FIELDS \
    FIELD(a, int8_t, -1) \
    FIELD(b, int8_t, -1) \
    FIELD(c, int8_t, -1) \
    FIELD(div, int8_t, 2)

typedef struct {
    #define FIELD(name, type, default_value) type name;
    ENCODER_FIELDS
    #undef FIELD
} encoder_t;

typedef struct {
    uint8_t id;      // user-defined id (optional)
    uint8_t role;    // 0=mousemove (x/y), 1=scroll (wheel/pan)
    int8_t bus;      // 0=spi0, 1=spi1
    int8_t cs;       // chip select pin (GPIO)
    int8_t irq;      // motion pin (GPIO), active low
    uint32_t baud;   // per-device SPI baud
    uint8_t mode;    // SPI mode (0..3)
    uint16_t cpi;    // sensor CPI
} pmw3360_cfg_t;

typedef struct {
    int8_t sck;
    int8_t mosi;
    int8_t miso;
} spi_cfg_t;

// Define the config_t struct using the X-Macro
typedef struct {
    #define FIELD(name, type, default_value) type name;
    CONFIG_FIELDS
    #undef FIELD
    matrix_t matrix;
    uint8_t nr_encoders;
    encoder_t encoders[MAX_ENCODERS];

    // SPI buses (pins only). Per-device baud/mode live in drivers.
    spi_cfg_t spi[2];

    // Drivers
    uint8_t nr_pmw3360;
    pmw3360_cfg_t pmw3360[MAX_PMW3360];
} config_t;

int parse(config_t *config, const char *filename);
void dbg3config(const config_t *config);

#endif // CONFIG_H
