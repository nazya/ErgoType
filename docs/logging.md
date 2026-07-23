# Logging / console IO

## TL;DR

- ErgoType logs (`msg`, `dbg*`, `warn`, `err`) go to the **TinyUSB CDC-ACM** interface.
- The CDC interface is present in both HID and MSC modes (composite USB device), so you can tail logs even in HID mode.
- You will not see anything until the host **opens** the CDC port (**DTR=1**) — the “minicom open effect”.
- Set `log_level` in `config.json` (`0..3`).

## Where logs go (code map)

- Logging macros live in `log.h` and call `_msg()`.
- `_msg()` in `log.c` formats the line (including ANSI colors) and writes bytes into the CDC backend (`platform_cdc_write()`).
- The USB device task (`platform/rp2/usb_tinyusb.c` or `platform/esp-idf/common/usb_tinyusb.c`) runs `tud_task(); platform_cdc_poll();`.
- `platform_cdc_poll()` (`platform/tinyusb/cdc_tinyusb.c`) only flushes when `tud_cdc_connected()` is true (host has asserted DTR).
- `platform_cdc_write()` is designed to be called from FreeRTOS task context (after the scheduler starts) and must not be called from an ISR.

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

- There are **two** relevant buffers (both use `PLATFORM_CDC_BUFFER_SIZE` from `platform/cdc.h`, currently `2048`):
  - `_msg()` (`log.c`) formats a log line into a per-call staging buffer and flushes it on `\n` (it writes `\r\n` to CDC).
  - `platform_cdc_write()` (`platform/tinyusb/cdc_tinyusb.c`) appends bytes into a ring buffer while the port is closed or the host is slow.

- **Max write size:** `platform_cdc_write()` clamps each call to at most `PLATFORM_CDC_BUFFER_SIZE` bytes. If some caller tries to write more than that in one call, the tail is dropped.

- **Very long lines:** if a *single formatted line* exceeds the `_msg()` staging buffer (roughly `PLATFORM_CDC_BUFFER_SIZE-2` bytes before the newline), the logger forces a `\r\n` flush and starts a new line. The byte that overflowed is dropped. Practically: very long log lines will be split/truncated and are not reliable.

- **Producer throttling (bounded wait):** if the port is open (DTR=1) and an incoming write doesn’t fit into the CDC ring buffer, `platform_cdc_write()` does a short, bounded `vTaskDelay()` loop and “kicks” the USB device task so it can drain the ring.

- **Ring buffer overflow (drop):** if the chunk still doesn’t fit after throttling (or throttling is skipped), the incoming chunk is dropped.
  - If `ws2812_pin` is configured, the firmware emits a brief **red** blink as a drop indicator.
  - If `ssd1306` is configured, the SSD1306 screen is updated to show drop statistics (`W=` writes, `B=` bytes).

- **Flush gating (DTR):** the ring buffer is only drained when the host has opened the CDC port (`tud_cdc_connected()==true`, i.e. DTR=1). While DTR=0 we still buffer, but there is no backpressure — if the ring fills, new chunks are dropped.

- **“Minicom open effect” and the settle delay:** on a DTR rising edge, flushing is delayed by `CDC_SETTLE_MS` (50ms) to avoid some host stacks dropping the first bytes. During this window we still buffer, so a big startup burst can still overflow the ring if the port wasn’t open early enough.

- If you need full startup logs, open the CDC port *before* resetting/powering the board (otherwise the small buffer can overflow and early lines may be lost).

- Some host terminal stacks drop the first bytes right after open; if this happens mid-escape-sequence you can see artifacts like `4m...` at the start of a line. Re-opening the port usually clears this.

## Platform console output (separate from logging)

ErgoType logging does not use the platform `printf()` console.

- RP2 enables Pico stdio UART. The optional implementation is in
  `platform/rp2/uart_stdio.c`, while `main.c` keeps its call disabled.
- ESP boot, panic, and direct `printf()` output use the ESP-IDF console selected
  by that target's `sdkconfig`. The P4 target also enables USB-Serial/JTAG as a
  secondary console.

Neither console path replaces the ErgoType TinyUSB CDC logger described above.
