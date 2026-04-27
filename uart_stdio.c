#include <stdbool.h>

#include "jconfig.h"

#include "pico/stdio.h"
#include "pico/stdio_uart.h"

#include "hardware/uart.h"

bool uart_stdio_init(const config_t *config)
{
    if (!config)
        return false;

    int tx = config->uart_tx_pin;
    int rx = config->uart_rx_pin;

    int uart0_tx_ok = (tx == 0 || tx == 12 || tx == 16 || tx == 28);
    int uart0_rx_ok = (rx == 1 || rx == 13 || rx == 17 || rx == 29);
    int uart1_tx_ok = (tx == 4 || tx == 8 || tx == 20 || tx == 24);
    int uart1_rx_ok = (rx == 5 || rx == 9 || rx == 21 || rx == 25);

    if (uart0_tx_ok && uart0_rx_ok) {
        stdio_uart_init_full(uart0, 115200, tx, rx);
        return true;
    }

    if (uart1_tx_ok && uart1_rx_ok) {
        stdio_uart_init_full(uart1, 115200, tx, rx);
        return true;
    }

    // Disable stdio UART if config is absent/invalid.
    stdio_set_driver_enabled(&stdio_uart, false);
    uart_deinit(uart0);
    uart_deinit(uart1);
    return false;
}
