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
#ifdef ERGOTYPE_BOARD_WAVESHARE_ESP32S3_PICO
    switch (pin) {
    case 1:
    case 2:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 21:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
        return true;
    default:
        return false;
    }
#else
    if (pin < 0 || pin > 48)
        return false;

    // ESP32-S3 GPIO22..GPIO25 do not exist. GPIO19/GPIO20 are native USB
    // D-/D+ for this firmware. GPIO26..GPIO37 are commonly reserved by
    // flash/PSRAM on ESP32-S3 modules. Strapping pins are intentionally kept
    // out of JSON config by default to avoid boards that no longer boot.
    if ((pin >= 22 && pin <= 37) ||
        pin == 0 ||
        pin == 3 ||
        pin == 19 ||
        pin == 20 ||
        pin == 45 ||
        pin == 46) {
        return false;
    }

    return true;
#endif
}

bool platform_gpio_is_valid_for_spi(uint8_t spi_idx, platform_gpio_spi_signal_t signal, int pin)
{
    (void)spi_idx;
    (void)signal;
    return platform_gpio_is_valid(pin);
}

bool platform_gpio_is_valid_for_i2c(uint8_t i2c_idx, platform_gpio_i2c_signal_t signal, int pin)
{
    (void)i2c_idx;
    (void)signal;
    return platform_gpio_is_valid(pin);
}

bool platform_gpio_is_valid_for_uart(uint8_t uart_idx, platform_gpio_uart_signal_t signal, int pin)
{
    (void)uart_idx;
    (void)signal;
    return platform_gpio_is_valid(pin);
}
