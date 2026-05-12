# Debugging

## Logs / serial console (USB CDC)

ErgoType’s `msg()`/`dbg()`/`err()` output is sent over the **TinyUSB CDC-ACM** interface (the board shows up as a USB serial port, e.g. `ttyACM0`).

Important: output is only flushed when the host has **opened** the CDC port (**DTR=1**). This is the usual “*minicom open effect*”: nothing appears until you open the port in a terminal.

- Set `log_level` in `config.json` (`0..3`) to control verbosity.
- Logs include ANSI color escapes; for `minicom` enable colors with `-c on`.

Quick start (Linux):

```sh
ls -l /dev/serial/by-id/
minicom -c on -D /dev/ttyACM0
```

See also: [`docs/logging.md`](logging.md).

## Status LEDs (`led_pins`)

The GPIO status LEDs are driven as a 32-step pattern advanced every 100 ms (see `ui/ui.c`).
If two pins are provided, both LEDs show the same pattern.

- USB **unmounted**: fast blink (`LED_PATTERN_FAST`, ~200 ms toggle)
- USB **mounted**: off (`pattern=0`)
- USB **suspended**: “heartbeat” (one 100 ms on-pulse every ~3.2 s)
- HID **CapsLock**: if the host sends the keyboard LED output report, the plain LED mirrors CapsLock (solid on/off).

## WS2812 status LED (`ws2812_pin`)

If `ws2812_pin` is set, a single WS2812/NeoPixel can be used as a status LED.

- **MSC mode**: **blue** = unmounted/idle, **red** = mounted/active, **green** = safe-ejected.
- **CDC log drops**: a brief **red** blink is emitted when the CDC log buffer overflows.

WS2812 brightness is currently fixed in code via the hardcoded color constants in `ui/ui.h` (`WS2812_RED/GREEN/BLUE`).
