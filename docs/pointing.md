# Pointing (PMW3360 / PMW3389)

Pointing runtime code lives in `pointing/` and is only started in HID mode (`main.c` only creates the pointing task when `mode == HID`).

## Config example
```json
{
  "spi0": { "sck": 6, "mosi": 3, "miso": 4, "baud": 500000 },
  "drivers": {
    "pmw3360": [
      { "role": "mousemove", "spi_idx": 0, "cs": 5, "irq": 7, "cpi": 800 }
    ],
    "pmw3389": [
      { "role": "scroll", "spi_idx": 0, "cs": 13, "irq": 14, "cpi": 800 }
    ]
  }
}
```

## Config schema (what the firmware actually parses)

### SPI buses (`spi0`, `spi1`)
Each SPI bus object uses numeric fields:
- `sck`, `mosi`, `miso` (required for the bus to be considered “present”)
- `baud` (optional, default `500000`)

The firmware validates RP2040 SPI pin muxing for bus pins (CS is *not* validated as an SPI pin; it’s just a GPIO):
- `spi0.sck`: `2`, `6`, `18`, `22`
- `spi0.mosi`: `3`, `7`, `19`, `23`
- `spi0.miso`: `0`, `4`, `16`, `20`
- `spi1.sck`: `10`, `14`, `26`
- `spi1.mosi`: `11`, `15`, `27`
- `spi1.miso`: `8`, `12`, `24`, `28`

### Drivers (`drivers.pmw3360[]`, `drivers.pmw3389[]`)
Both driver arrays share the same per-device object schema:

| Field | Type | Required | Default | Meaning |
|---|---:|:---:|---:|---|
| `spi_idx` | number | yes | `-1` | SPI bus index (`0` = `spi0`, `1` = `spi1`) |
| `cs` | number | yes | `-1` | chip-select GPIO (active low) |
| `irq` | number | yes | `-1` | motion/MOT GPIO (active low) |
| `cpi` | number | no | `800` | sensor CPI setting (driver applies at startup) |
| `role` | string | no | `"mousemove"` | `"mousemove"`/`"mouse"` = x/y, `"scroll"` = wheel/pan |

Important behavior details:
- Max sensors: `drivers.pmw3360` is capped at 2 entries, and `drivers.pmw3389` is capped at 2 entries. Extra entries are ignored.
- Pins must be unique across the entire config: the parser rejects duplicate GPIO numbers, so you can’t share `cs` or `irq` between devices (or reuse them for other configured functions).
- `role` is parsed only from a JSON string. `"mousemove"`, `"mouse"`, and `"scroll"` are recognized; anything else falls back to the default role.
- A PMW33xx device entry is skipped if its `spi_idx` bus isn’t fully configured (missing/invalid `sck`/`mosi`/`miso`).

## Runtime behavior (how motion becomes HID events)

- SPI format: PMW3360/PMW3389 transactions force SPI mode 3 (CPOL=1, CPHA=1), 8-bit, MSB-first (set before each transaction in the driver).
- SPI clock: the bus speed is set once via `spi_init(spiN, spiN.baud)` inside the pointing task.
- IRQ polarity: `irq`/MOT is configured as `GPIO_IN` with internal pull-up; interrupts are enabled on the falling edge (idle HIGH, pulse/level LOW).
- IRQ fan-in: all motion pins share a single GPIO IRQ callback; the ISR notifies the pointing task using a bitmask of “which sensor(s) moved”.
- Event batching: after wake-up, the pointing task waits 2 ticks (~2ms with the current `configTICK_RATE_HZ=1000`) before reading deltas to coalesce bursts.
- Role mapping: `"mousemove"` devices contribute to mouse x/y; `"scroll"` devices contribute to scroll x/y where x=horizontal (pan) and y=vertical (wheel).

## Current limitations (as implemented)

- No polling fallback: the pointing task blocks on IRQ notifications; if MOT isn’t wired correctly (or never toggles), you won’t get motion reports.
- Startup time: each sensor init performs a reset + SROM firmware upload (4094 bytes with per-byte delays); expect on the order of ~100ms+ per sensor before the first motion can be reported.
- Minimal fault reporting: init return values are currently ignored by the pointing task, so a non-responding sensor may fail “quietly” (aside from SPI/pin-claim errors during config parse).
- CPI stepping is driver-specific:
  - PMW3360: `cpi` is programmed in 100-CPI steps via `(cpi/100) - 1`. Values below 100 will underflow in the current implementation.
  - PMW3389: `cpi` is programmed in 50-CPI steps; values are clamped to a minimum of 50.
- There is no axis inversion/rotation in the firmware pointing path today; fix direction by physical orientation or by adjusting higher-level processing.

See also: `pointing/README.md`.
