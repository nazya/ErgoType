# Logging / console IO

## TL;DR

- ErgoType logs (`msg`, `dbg*`, `warn`, `err`) go to the **TinyUSB CDC-ACM** interface.
- The CDC interface is present in both HID and MSC modes (composite USB device), so you can tail logs even in HID mode.
- You will not see anything until the host **opens** the CDC port (**DTR=1**) — the “minicom open effect”.
- Set `log_level` in `config.json` (`0..3`).

## Where logs go (code map)

- Logging macros live in `log.h` and call `_msg()`.
- `_msg()` in `log.c` formats the line (including ANSI colors) and writes bytes into the CDC backend (`stdio_tusb_cdc_write()`).
- The USB device task (`tusb_device_task.c`) runs `tud_task(); stdio_tusb_cdc_poll();`.
- `stdio_tusb_cdc_poll()` (`stdio_tusb_cdc.c`) only flushes when `tud_cdc_connected()` is true (host has asserted DTR).
- `stdio_tusb_cdc_write()` currently requires the FreeRTOS scheduler to be running (and must not be called from an ISR), so very-early boot / ISR logs may be dropped.

## Viewing logs

Linux:

```sh
# Find the port
ls -l /dev/serial/by-id/

# Open it (baud rate is ignored for USB CDC, but most tools require one)
minicom -c on -D /dev/ttyACM0
# or: screen /dev/ttyACM0 115200
```

macOS:

```sh
ls /dev/cu.usbmodem*
screen /dev/cu.usbmodemXXXX 115200
```

Windows:

- Find the COM port in Device Manager.
- Open it with PuTTY (Connection type: Serial).

## `log_level`

`config.json` controls verbosity:

- `0`: `msg`, `warn`, `err`
- `1`: `dbg` (+ level 0)
- `2`: `dbg2` (+ levels 0–1)
- `3`: `dbg3` (+ levels 0–2). `main.c` calls `dbg3config(&config)` at startup, so `3` can be very spammy.

## Buffering, drops, and “garbled” ANSI

- The CDC backend buffers up to `BUFSIZE` bytes (`stdio_tusb_cdc.h`, currently 2048) while the port is closed or the host is slow.
- If an incoming write doesn’t fit, the incoming chunk is dropped. If `ws2812_pin` is configured, the firmware emits a brief **red** blink as a drop indicator.
- If you need full startup logs, open the CDC port *before* resetting/powering the board (otherwise the small buffer can overflow and early lines may be lost).
- Some host terminal stacks drop the first bytes right after open; if this happens mid-escape-sequence you can see artifacts like `4m...` at the start of a line. Re-opening the port usually clears this.

## UART stdio (separate from logging)

The build enables **pico stdio UART** (`pico_enable_stdio_uart(... 1)`), but ErgoType logging does **not** use `printf()`.

- `uart0.*` / `uart1.*` are parsed from `config.json` and can be used by `uart_stdio_init()` (`uart_stdio.c`) to move pico-stdio UART to those pins.
- As of 2026-05-09, `main.c` keeps the `uart_stdio_init(&config)` call commented out. Enabling UART for `printf()` output requires a small firmware change.
