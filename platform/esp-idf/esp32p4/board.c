#include "hal/gpio_types.h"

#include "platform/board.h"
#include "platform/gpio.h"

void platform_board_init(void)
{
}

void platform_board_init_after_usb(void)
{
}

bool platform_gpio_is_valid(int pin)
{
#ifdef ERGOTYPE_BOARD_DFR1172
    switch (pin) {
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 20:
    case 21:
    case 22:
    case 23:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 37:
    case 38:
    case 48:
    case 51:
    case 52:
        return true;
    default:
        return false;
    }
#else
    if (!GPIO_IS_VALID_GPIO(pin))
        return false;

    // GPIO33..GPIO37 are used by octal flash/PSRAM on relevant ESP32-P4
    // chips/modules. GPIO49/GPIO50 are dedicated USB 2.0 PHY pins.
    if ((pin >= 33 && pin <= 37) || pin == 49 || pin == 50)
        return false;

    return true;
#endif
}

bool platform_gpio_is_valid_for_spi(uint8_t spi_idx, platform_gpio_spi_signal_t signal, int pin)
{
    (void)spi_idx;
    if (signal == PLATFORM_GPIO_SPI_MISO)
        return platform_gpio_is_valid(pin);
    return platform_gpio_is_valid(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

bool platform_gpio_is_valid_for_i2c(uint8_t i2c_idx, platform_gpio_i2c_signal_t signal, int pin)
{
    (void)i2c_idx;
    (void)signal;
    return platform_gpio_is_valid(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}

bool platform_gpio_is_valid_for_uart(uint8_t uart_idx, platform_gpio_uart_signal_t signal, int pin)
{
    (void)uart_idx;
    if (signal == PLATFORM_GPIO_UART_RX)
        return platform_gpio_is_valid(pin);
    return platform_gpio_is_valid(pin) && GPIO_IS_VALID_OUTPUT_GPIO(pin);
}
