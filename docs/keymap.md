# Keymap

`keymap` is a 2D array. Each entry can be:
- a number (keyd keycode: `KEYD_*` value from `keyd/src/keys.h`), or
- a string (keyd key name; see `keycode_table` in `keyd/src/keys.c`)

Example:
```json
{
    "gpio_rows": [4, 5, 6],
    "gpio_cols": [8, 9],
    "keymap": [
        [         "`",       "q" ],
        [  "capslock",        30 ],
        [          42,       "z" ]
    ]
}
```

In the example above:
- Strings: ``"`"`` and `"q"` are keyd key names.
- Numbers: `30` is `KEYD_A`, `42` is `KEYD_LEFTSHIFT` (see `keyd/src/keys.h`).
