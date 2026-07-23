#include <stdbool.h>

#include "jconfig.h"

#include "pico/stdio.h"
#include "pico/stdio_uart.h"

#include "hardware/uart.h"

bool uart_stdio_init(const config_t *config)
{
    if (config->uart_mask & 0x01u) {
        stdio_uart_init_full(uart0, 115200, config->uart[0].tx, config->uart[0].rx);
        return true;
    }

    if (config->uart_mask & 0x02u) {
        stdio_uart_init_full(uart1, 115200, config->uart[1].tx, config->uart[1].rx);
        return true;
    }

    // Disable stdio UART if config is absent/invalid.
    stdio_set_driver_enabled(&stdio_uart, false);
    uart_deinit(uart0);
    uart_deinit(uart1);
    return false;
}
