# Logging / console IO

## TL;DR

- ErgoType logs (`msg`, `dbg*`, `warn`, `err`) go to the **TinyUSB CDC-ACM** interface.
- The CDC interface is present in both HID and MSC modes (composite USB device), so you can tail logs even in HID mode.
- You will not see anything until the host **opens** the CDC port (**DTR=1**) — the “minicom open effect”.
- Set `log_level` in `config.json` (`0..3`).

## Where logs go (code map)

- Logging macros live in `log.h` and call `_msg()`.
- `_msg()` in `log.c` renders one complete line (including ANSI colors) directly into the CDC backend (`stdio_tusb_cdc_write_rendered()`).
- The USB device task (`tusb_device_task.c`) runs `tud_task(); stdio_tusb_cdc_poll();`.
- `stdio_tusb_cdc_poll()` (`stdio_tusb_cdc.c`) only flushes when `tud_cdc_connected()` is true (host has asserted DTR).
- `stdio_tusb_cdc_write_rendered()` is designed to be called from FreeRTOS task context (after the scheduler starts) and must not be called from an ISR.

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

- There is one `BUFSIZE` byte ring buffer in `stdio_tusb_cdc.c` (currently `2048`). `_msg()` streams each complete rendered line into it without a second line-sized staging buffer.

- **Max write size:** if one rendered line exceeds `BUFSIZE`, the whole line is dropped.

- **Producer throttling (bounded wait):** if the port is open (DTR=1) and a rendered line doesn’t fit into the CDC ring buffer, `stdio_tusb_cdc_write_rendered()` does a short, bounded `vTaskDelay()` loop and “kicks” the USB device task so it can drain the ring. It then renders the line again into the newly available space.

- **Ring buffer overflow (drop):** if the line still doesn’t fit after throttling (or throttling is skipped), the rendered line is dropped.
  - If `ws2812_pin` is configured, the firmware emits a brief **red** blink as a drop indicator.
  - If `ssd1306` is configured, the SSD1306 screen is updated to show drop statistics (`W=` writes, `B=` bytes).

- **Flush gating (DTR):** the ring buffer is only drained when the host has opened the CDC port (`tud_cdc_connected()==true`, i.e. DTR=1). While DTR=0 we still buffer, but there is no backpressure — if the ring fills, new chunks are dropped.

- **“Minicom open effect” and the settle delay:** on a DTR rising edge, flushing is delayed by `CDC_SETTLE_MS` (50ms) to avoid some host stacks dropping the first bytes. During this window we still buffer, so a big startup burst can still overflow the ring if the port wasn’t open early enough.

- If you need full startup logs, open the CDC port *before* resetting/powering the board (otherwise the small buffer can overflow and early lines may be lost).

- Some host terminal stacks drop the first bytes right after open; if this happens mid-escape-sequence you can see artifacts like `4m...` at the start of a line. Re-opening the port usually clears this.

## UART stdio (separate from logging)

The build enables **pico stdio UART** (`pico_enable_stdio_uart(... 1)`), but ErgoType logging does **not** use `printf()`.

- `uart0.*` / `uart1.*` are parsed from `config.json` and can be used by `uart_stdio_init()` (`uart_stdio.c`) to move pico-stdio UART to those pins.
- `main.c` keeps the `uart_stdio_init(&config)` call commented out. Enabling UART for `printf()` output requires a small firmware change.
