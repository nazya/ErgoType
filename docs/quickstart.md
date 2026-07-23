# Quickstart

## Flashing

### RP2040 / RP2350

1. Download the `.uf2` artifact from GitHub Actions.
2. Put the board in USB boot mode and copy the file to its boot volume.

### ESP32-S3 / ESP32-P4

Download the files for the exact target:

| File | Flash address | Use | `config.json` |
| --- | --- | --- | --- |
| `ErgoType-<board>-full-0x000000.bin` | `0x000000` | First flash, recovery, or after `Erase Flash` | Replaced |
| `ErgoType-<board>-app-0x010000.bin` | `0x010000` | Normal firmware update | Preserved |

Do not run `Erase Flash` for a normal app-only update.

See the board-specific instructions for
[Waveshare ESP32-S3-Pico](boards/waveshare-esp32s3-pico.md) or
[DFRobot FireBeetle 2 ESP32-P4](boards/firebeetle2-esp32p4.md).

After first programming, ErgoType starts in MSC mode and exposes an `ErgoType`
volume containing an empty `config.json`.

## Minimal `config.json`
Minimal configuration requires `"gpio_rows"`, `"gpio_cols"`, and `"keymap"`.
The following pin selection is an RP2 example; use GPIO accepted by the selected
board target on ESP.

```json
{
    "gpio_rows": [4, 5, 6],
    "gpio_cols": [8, 9],
    "keymap": [
        ["noop", 0],
        ["capslock", "a"],
        [123, "leftshift"]
    ]
}
```

For `"keymap"` you can use either numeric keycodes or keyd names (see `keycode_table` in `keyd/src/keys.c`).

Next:
- Full config reference: [`docs/config-json.md`](config-json.md)
- Keymap notes: [`docs/keymap.md`](keymap.md)
- Boot modes (MSC vs HID): [`docs/modes.md`](modes.md)
