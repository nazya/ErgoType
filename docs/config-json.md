# Config: `config.json`

`config.json` is the main runtime configuration file parsed by the firmware (`jconfig.c` / `jconfig.h`).

If config parsing fails (missing file / invalid JSON / no input devices), the firmware boots into **MSC** mode so you can fix the files. See: [`docs/modes.md`](modes.md).

## General rules / limits

- GPIO pins are RP2040 GPIO numbers `0..29`. A value of `-1` means “disabled” for some fields.
- Pins are claimed globally: the same GPIO must not be used by two different features (matrix pins, bus pins, `*_pin`, pull pins, pointing CS/IRQ, ...). Duplicates are logged and ignored/disabled.
- Hard limits (from `jconfig.h`):
  - `gpio_rows` / `gpio_cols`: up to 8 pins each (`MAX_GPIOS`)
  - `encoders`: up to 2 entries (`MAX_ENCODERS`)
  - `drivers.pmw3360`: up to 2 entries (`MAX_PMW3360`)
  - `drivers.pmw3389`: up to 2 entries (`MAX_PMW3389`)
  - Buses: `spi0`/`spi1`, `uart0`/`uart1`, `i2c0`/`i2c1`

This doc refers to nested fields using JSON “paths” (for example `uart0.rx` means `{ "uart0": { "rx": ... } }`).

## Scalars

| Field              | Type    | Default | Description |
|--------------------|---------|---------|-------------|
| `log_level`        | `int8`  | `0`     | Logging verbosity: `0` = only `msg`/`warn`/`err`, `1..3` enables `dbg..dbg3`. |
| `ws2812_pin`       | `int8`  | `-1`    | Optional WS2812/NeoPixel data pin. |
| `erase_pin`        | `int8`  | `-1`    | Optional GPIO pin to force filesystem re-init at boot (active-low). |
| `nr_pressed_erase` | `int8`  | `9`     | If `erase_pin` is unset/invalid, re-init filesystem when `nr_pressed >= nr_pressed_erase` at boot. Set to `0` to disable. |
| `msc_pin`          | `int8`  | `-1`    | Optional GPIO pin to force MSC mode at boot (active-low). |
| `nr_pressed_msc`   | `int8`  | `3`     | If `msc_pin` is unset/invalid, enter MSC mode when `nr_pressed >= nr_pressed_msc` at boot. Set to `0` to disable. |
| `scan_period`      | `int8`  | `5`     | Key scan period in FreeRTOS ticks (tick rate is 1000 Hz, so 1 tick = 1 ms). |
| `debounce`         | `int8`  | `9`     | Debounce time in milliseconds. Set to `0` to disable debouncing. |

## Status LEDs (`led_pins`)

`led_pins` is an optional array of GPIO pins for one or two “plain” status LEDs (active-high).
Both pins, if provided, show the same blink pattern.

```json
{ "led_pins": [17] }
```

```json
{ "led_pins": [17, 18] }
```

## Matrix + `keymap`

| Field       | Type    | Default | Description |
|-------------|---------|---------|-------------|
| `gpio_rows` | `int[]` | N/A     | Matrix row GPIO pins (max 8). Invalid/duplicate pins are skipped. |
| `gpio_cols` | `int[]` | N/A     | Matrix column GPIO pins (max 8). Invalid/duplicate pins are skipped. |
| `keymap`    | `[][]`  | N/A     | 2D array of key codes: each entry is a number (raw keycode) or a string (keyd key name). |

Notes:
- The firmware iterates `keymap[row][col]` for `row < len(gpio_rows)` and `col < len(gpio_cols)`. Extra JSON rows/cols are ignored.
- If `keymap` is missing or too small, unfilled cells stay `0` (not `noop`) because the config struct is zero-initialized. Prefer providing a full matrix.
- Key names are looked up in `keycode_table` (see `keyd/src/keys.c`). Unknown names map to `noop`.

See also: [Keymap](keymap.md).

## Scan direction

Current firmware uses a hardcoded `col2row` scan direction. You can flip `gpio_rows`/`gpio_cols` and transpose `keymap` if needed.

## Encoders

`encoders` is an optional array (max 2). Each encoder is represented as 2 switches `A`/`B` that share a common `C` and are wired into the matrix.

Each entry is an object:
- `a` (int): GPIO pin from `gpio_rows` for channel A
- `b` (int): GPIO pin from `gpio_rows` for channel B
- `c` (int): GPIO pin from `gpio_cols` for the common pin
- `div` (int, default `2`): quadrature divisor (higher = fewer taps per detent)

The firmware maps these GPIO pins to matrix indices (`row = index of a/b in gpio_rows`, `col = index of c in gpio_cols`) and generates key taps using `keymap[row][col]` for each direction.

Example:
```json
{
    "gpio_rows": [5, 6, 7],
    "gpio_cols": [20, 21],
    "keymap": [
        ["noop", "noop"],
        ["noop", "volumeup"],
        ["noop", "volumedown"]
    ],
    "encoders": [
        { "a": 6, "b": 7, "c": 21, "div": 2 }
    ]
}
```

## UART buses

Optional UART pin config (validated against RP2040 pin muxing):

| Field      | Type   | Default | Supported pins |
|------------|--------|---------|----------------|
| `uart0.rx` | `int8` | `-1`    | 1, 13, 17, 29 |
| `uart0.tx` | `int8` | `-1`    | 0, 12, 16, 28 |
| `uart1.rx` | `int8` | `-1`    | 5, 9, 21, 25 |
| `uart1.tx` | `int8` | `-1`    | 4, 8, 20, 24 |

Note: UART pins are parsed and validated, but they currently only affect optional UART stdio (`uart_stdio_init()` is commented out in `main.c`). Logging is always written to USB CDC in the current firmware.

## I2C buses

Optional I2C pin config (validated against RP2040 pin muxing):

| Field      | Type   | Default | Supported pins |
|------------|--------|---------|----------------|
| `i2c0.sda` | `int8` | `-1`    | 0, 4, 8, 12, 16, 20, 24, 28 |
| `i2c0.scl` | `int8` | `-1`    | 1, 5, 9, 13, 17, 21, 25, 29 |
| `i2c1.sda` | `int8` | `-1`    | 2, 6, 10, 14, 18, 22, 26 |
| `i2c1.scl` | `int8` | `-1`    | 3, 7, 11, 15, 19, 23, 27 |

## SSD1306 OLED display (I2C)

Optional SSD1306 OLED display over I2C. It uses a bus configured via `i2c0.*` / `i2c1.*`.

| Field             | Type     | Default | Description |
|-------------------|----------|---------|-------------|
| `ssd1306.i2c_idx` | `int8`   | `-1`    | I2C bus index (`0` = `i2c0`, `1` = `i2c1`). Omit the whole `ssd1306` section to disable. |
| `ssd1306.addr`    | `uint8`  | `60`    | 7-bit I2C address (`60` = `0x3c`). |
| `ssd1306.width`   | `uint16` | `128`   | Display width in pixels. |
| `ssd1306.height`  | `uint16` | `64`    | Display height in pixels (`32` or `64`). |

Example:
```json
{
    "i2c0": { "sda": 4, "scl": 5 },
    "ssd1306": { "i2c_idx": 0, "addr": 60, "width": 128, "height": 64 }
}
```

When enabled, the firmware uses the display as a simple status screen and shows CDC drop statistics (see `docs/logging.md`).

## SPI buses

Optional SPI bus config (bus pins are validated against RP2040 pin muxing; device CS is just a plain GPIO):

| Field       | Type     | Default  | Supported pins |
|-------------|----------|----------|----------------|
| `spi0.sck`  | `int8`   | `-1`     | 2, 6, 18, 22 |
| `spi0.mosi` | `int8`   | `-1`     | 3, 7, 19, 23 |
| `spi0.miso` | `int8`   | `-1`     | 0, 4, 16, 20 |
| `spi0.baud` | `uint32` | `500000` | (any integer) |
| `spi1.sck`  | `int8`   | `-1`     | 10, 14, 26 |
| `spi1.mosi` | `int8`   | `-1`     | 11, 15, 27 |
| `spi1.miso` | `int8`   | `-1`     | 8, 12, 24, 28 |
| `spi1.baud` | `uint32` | `500000` | (any integer) |

Example:
```json
{
    "spi0": { "sck": 6, "mosi": 3, "miso": 4, "baud": 500000 },
    "spi1": { "sck": 10, "mosi": 11, "miso": 12, "baud": 500000 }
}
```

## Pointing / PMW33xx drivers

Pointing devices are configured under:
- `drivers.pmw3360`: array of devices (max 2)
- `drivers.pmw3389`: array of devices (max 2)

Each device object supports:
- `role` (string): `"mousemove"`/`"mouse"` or `"scroll"` (defaults to `"mousemove"`)
- `spi_idx` (int): `0` for `spi0`, `1` for `spi1`
- `cs` (int): chip-select GPIO pin
- `irq` (int): motion GPIO pin (active-low)
- `cpi` (int, default `800`): CPI

If `spi_idx` refers to a SPI bus that is not configured/enabled, the device entry is ignored.

See also: [Pointing / PMW33xx](pointing.md).

See also: [Runtime architecture](runtime.md).

## Pull resistors

| Field            | Type    | Default | Description |
|------------------|---------|---------|-------------|
| `pull_up_pins`   | `int[]` | N/A     | GPIO pins to configure with internal pull-ups. |
| `pull_down_pins` | `int[]` | N/A     | GPIO pins to configure with internal pull-downs. |

These pins are `gpio_init()`'d and the pull is applied during config parsing.
