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
#define MAX_PMW3389 2
#define MAX_LED 2
#define MAX_SPI 2
#define MAX_UART 2
#define MAX_I2C 2

enum {
    SENSOR_ROLE_MOUSEMOVE = 0, // x/y
    SENSOR_ROLE_SCROLL = 1,    // wheel/pan
};

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
    FIELD(log_level, int8_t, 0)  \
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

#define UART_CFG_FIELDS \
    FIELD(rx, int8_t, -1) \
    FIELD(tx, int8_t, -1)

typedef struct {
    #define FIELD(name, type, default_value) type name;
    UART_CFG_FIELDS
    #undef FIELD
} uart_cfg_t;

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

#define PMW33XX_FIELDS(role_default) \
    FIELD(role, uint8_t, (role_default)) /* 0=mousemove (x/y), 1=scroll (wheel/pan) */ \
    FIELD(spi_idx, int8_t, -1) /* 0=spi0, 1=spi1 */ \
    FIELD(cs, int8_t, -1) /* chip select pin (GPIO) */ \
    FIELD(irq, int8_t, -1) /* motion pin (GPIO), active low */ \
    FIELD(cpi, uint16_t, 800u) /* sensor CPI */

typedef struct {
    #define FIELD(name, type, default_value) type name;
    PMW33XX_FIELDS(SENSOR_ROLE_MOUSEMOVE)
    #undef FIELD
} pmw33xx_cfg_t;

#define SPI_PIN_FIELDS \
    FIELD(sck, int8_t, -1) \
    FIELD(mosi, int8_t, -1) \
    FIELD(miso, int8_t, -1)

#define SPI_CFG_FIELDS \
    SPI_PIN_FIELDS \
    FIELD(baud, uint32_t, 500000u)

typedef struct {
    #define FIELD(name, type, default_value) type name;
    SPI_CFG_FIELDS
    #undef FIELD
} spi_cfg_t;

// I2C buses (pins + baud).
#define I2C_PIN_FIELDS \
    FIELD(sda, int8_t, -1) \
    FIELD(scl, int8_t, -1)

#define I2C_CFG_FIELDS \
    I2C_PIN_FIELDS \
    FIELD(baud, uint32_t, 400000u)

typedef struct {
    #define FIELD(name, type, default_value) type name;
    I2C_CFG_FIELDS
    #undef FIELD
} i2c_cfg_t;

// SSD1306 OLED display (I2C).
#define SSD1306_CFG_FIELDS \
    FIELD(i2c_idx, int8_t, -1)  /* 0=i2c0, 1=i2c1; unset => disabled */ \
    FIELD(addr, uint8_t, 60u)   /* 7-bit I2C address; 60=0x3c */ \
    FIELD(width, uint8_t, 128u) \
    FIELD(height, uint8_t, 64u) \
    FIELD(contrast, uint8_t, 0x3Fu) /* default: low (minimally readable) */ \
    FIELD(precharge, uint8_t, 0x22u) /* 0xD9: phase 1/2 periods */ \
    FIELD(vcomh, uint8_t, 0x20u) /* 0xDB: VCOMH deselect level */

typedef struct {
    #define FIELD(name, type, default_value) type name;
    SSD1306_CFG_FIELDS
    #undef FIELD
} ssd1306_cfg_t;

// Define the config_t struct using the X-Macro
typedef struct {
    #define FIELD(name, type, default_value) type name;
    CONFIG_FIELDS
    #undef FIELD
    uint8_t nr_leds;
    int8_t led_pins[MAX_LED];
    matrix_t matrix;
    uint8_t nr_encoders;
    encoder_t encoders[MAX_ENCODERS];

    // SPI buses (pins + baud).
    uint8_t spi_mask;
    spi_cfg_t spi[MAX_SPI];

    // UART buses (pins only). Used for stdio/logging.
    uint8_t uart_mask;
    uart_cfg_t uart[MAX_UART];

    // I2C buses (pins + baud).
    uint8_t i2c_mask;
    i2c_cfg_t i2c[MAX_I2C];

    // Optional SSD1306 display (I2C).
    ssd1306_cfg_t ssd1306;

    // Drivers
    uint8_t nr_pmw3360;
    pmw33xx_cfg_t pmw3360[MAX_PMW3360];
    uint8_t nr_pmw3389;
    pmw33xx_cfg_t pmw3389[MAX_PMW3389];
} config_t;

int parse(config_t *config, const char *filename);
void dbg3config(const config_t *config);

#endif // CONFIG_H
