# Keymap

`keymap` is a 2D array. Each entry can be:
- a number (raw keycode), or
- a string (keyd key name; see `keycode_table` in `keyd/src/keys.c`)

Example:
```json
{
    "gpio_rows": [4, 5, 6],
    "gpio_cols": [8, 9],
    "keymap": [
        [         "`",       "q" ],
        [  "capslock",       "a" ],
        [ "leftshift",       "z" ]
    ]
}
```

