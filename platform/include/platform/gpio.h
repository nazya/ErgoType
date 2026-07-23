#ifndef ERGOTYPE_PLATFORM_GPIO_H
#define ERGOTYPE_PLATFORM_GPIO_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    PLATFORM_GPIO_IN = 0,
    PLATFORM_GPIO_OUT = 1,
};

enum {
    PLATFORM_GPIO_FUNC_SIO = 0,
    PLATFORM_GPIO_FUNC_SPI = 1,
    PLATFORM_GPIO_FUNC_I2C = 2,
};

enum {
    PLATFORM_GPIO_IRQ_EDGE_FALL = 0x04u,
};

typedef enum {
    PLATFORM_GPIO_SPI_SCK,
    PLATFORM_GPIO_SPI_MOSI,
    PLATFORM_GPIO_SPI_MISO,
} platform_gpio_spi_signal_t;

typedef enum {
    PLATFORM_GPIO_I2C_SDA,
    PLATFORM_GPIO_I2C_SCL,
} platform_gpio_i2c_signal_t;

typedef enum {
    PLATFORM_GPIO_UART_TX,
    PLATFORM_GPIO_UART_RX,
} platform_gpio_uart_signal_t;

typedef void (*platform_gpio_irq_callback_t)(uint gpio, uint32_t events);

bool platform_gpio_is_valid(int pin);
bool platform_gpio_is_valid_for_spi(uint8_t spi_idx, platform_gpio_spi_signal_t signal, int pin);
bool platform_gpio_is_valid_for_i2c(uint8_t i2c_idx, platform_gpio_i2c_signal_t signal, int pin);
bool platform_gpio_is_valid_for_uart(uint8_t uart_idx, platform_gpio_uart_signal_t signal, int pin);
void platform_gpio_init(uint gpio);
void platform_gpio_set_dir(uint gpio, bool out);
void platform_gpio_pull_up(uint gpio);
void platform_gpio_pull_down(uint gpio);
void platform_gpio_disable_pulls(uint gpio);
void platform_gpio_set_function(uint gpio, int fn);
void platform_gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled);
void platform_gpio_set_irq_enabled_with_callback(
    uint gpio,
    uint32_t events,
    bool enabled,
    platform_gpio_irq_callback_t callback);

#include <platform/backend/gpio.h>

#endif
