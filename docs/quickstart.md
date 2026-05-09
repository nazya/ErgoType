# Quickstart

## Flashing
1. Download the `.uf2` file from GitHub Actions and flash your RP Pico.
2. After flashing, you will see an `ErgoType` MSC device with an empty `config.json`.

## Minimal `config.json`
Minimal configuration requires `"gpio_rows"`, `"gpio_cols"`, and `"keymap"`.

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
