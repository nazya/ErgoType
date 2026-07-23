#include "platform/board.h"
#include "platform/gpio.h"

#include "bsp/board.h"
#include "hardware/platform_defs.h"

extern void board_init_after_tusb(void) __attribute__((weak));

void platform_board_init(void)
{
    board_init();
}

void platform_board_init_after_usb(void)
{
    if (board_init_after_tusb)
        board_init_after_tusb();
}

bool platform_gpio_is_valid(int pin)
{
    return pin >= 0 && pin < (int)NUM_BANK0_GPIOS;
}

bool platform_gpio_is_valid_for_spi(uint8_t spi_idx, platform_gpio_spi_signal_t signal, int pin)
{
    if (!platform_gpio_is_valid(pin))
        return false;

    if (((pin / 8) & 1) != spi_idx)
        return false;

    switch (signal) {
    case PLATFORM_GPIO_SPI_SCK:
        return pin % 4 == 2;
    case PLATFORM_GPIO_SPI_MOSI:
        return pin % 4 == 3;
    case PLATFORM_GPIO_SPI_MISO:
        return pin % 4 == 0;
    }

    return false;
}

bool platform_gpio_is_valid_for_i2c(uint8_t i2c_idx, platform_gpio_i2c_signal_t signal, int pin)
{
    if (!platform_gpio_is_valid(pin))
        return false;

    const int signal_offset = signal == PLATFORM_GPIO_I2C_SCL ? 1 : 0;
    return pin % 4 == (int)(i2c_idx * 2) + signal_offset;
}

bool platform_gpio_is_valid_for_uart(uint8_t uart_idx, platform_gpio_uart_signal_t signal, int pin)
{
    if (!platform_gpio_is_valid(pin))
        return false;

    const int offset = signal == PLATFORM_GPIO_UART_RX ? 1 : 0;
    const int pin_mod = pin % 16;
    const int base = uart_idx == 0 ? 0 : 4;
    bool valid = pin_mod == base + offset || pin_mod == 12 - base + offset;
#if PICO_RP2350
    valid = valid || pin_mod == base + 2 + offset || pin_mod == 14 - base + offset;
#endif
    return valid;
}
