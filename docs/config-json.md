# `config.json`

The runtime reads `config.json` from LittleFS. Every physical nRF52840 GPIO is
addressed globally as `0..47`; negative values mean unset where supported, and
values outside that range are rejected. GPIOs are claimed once across matrix,
buses, LEDs and pointing devices.

Core fields are `gpio_rows`, `gpio_cols` (up to 8 each), `keymap`, `encoders`
(up to 2), `scan_period` in milliseconds, `debounce` in milliseconds,
`overlay_conf`, `led_pins` (up to 2), `ws2812_pin`, pull-up/down pin arrays and
`log_level` (`0..3`). Legacy erase/mode-selection fields remain parsed for
config compatibility but have no startup action on this BLE-only target.

Logical `spi0` and `spi1` accept arbitrary valid `sck`, `mosi`, and `miso` GPIOs
plus `baud`; they map to SPIM2 and SPIM3. Logical `i2c0` and `i2c1` accept valid
`sda`, `scl`, and `baud` GPIOs and map to TWIM0 and TWIM1. UART fields remain
parser-compatible GPIO reservations but have no active UART backend.

`drivers.pmw3360` and `drivers.pmw3389` are arrays of up to two objects with
`role` (`mousemove`/`mouse` or `scroll`), `spi_idx`, `cs`, active-low `irq`, and
`cpi`. CPI is clamped to at least 100 and rounded down to the sensor step (100
for PMW3360, 50 for PMW3389). `ssd1306` selects `i2c_idx`, address, dimensions,
contrast, precharge and VCOMH.

`move_accel` and `scroll_accel` accept `none`, `flat`, `adaptive`, or `custom`.
For flat/adaptive, `scale` is `1..10000` and `speed` is `-scale..scale`; flat
uses factor `1 + speed/scale` (with the implementation's small positive floor),
while adaptive feeds the normalized speed into its velocity curve. Custom uses
`custom_step` and 2..64 non-negative `custom_points` with linear interpolation.

```json
{
  "gpio_rows": [2, 3],
  "gpio_cols": [4, 5],
  "keymap": [["a", "b"], ["leftshift", "space"]],
  "spi0": {"sck": 13, "mosi": 14, "miso": 15, "baud": 2000000},
  "drivers": {"pmw3360": [{"spi_idx": 0, "cs": 16, "irq": 17, "cpi": 800}]},
  "move_accel": {"profile": "flat", "scale": 100, "speed": 25}
}
```
