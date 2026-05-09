# Examples

## Hillside 46 demo
Using the RP2040 ProMicro, left-hand part configured as. Demo video: https://www.youtube.com/watch?v=nl_YRXBFJNs

```json
{
    "log_level": 1,
    "uart0": { "rx": 13, "tx": 16 },
    "led_pin": 17,
    "scan_period": 5,
    "debounce": 9,
    "pull_up_pins": [28, 29],
    "pull_down_pins": [0],
    "gpio_rows": [5, 6, 7, 9],
    "gpio_cols": [27, 26, 22, 20, 23, 21],
    "keymap": [
        [         "`",       "q",      "w",           "e",        "r",         "t" ],
        [  "capslock",       "a",      "s",           "d",        "f",         "g" ],
        [ "leftshift",       "z",      "x",           "c",        "v",         "b" ],
        [      "noop", "leftmeta", "leftalt", "leftcontrol",  "space", "backspace" ]
    ]
}
```

Then `keyd.conf` example (save as `keyd.conf` in the root of the MSC drive, then reboot into HID mode to apply):

```ini
[global]
oneshot_timeout = 300
overload_tap_timeout = 200
chord_timeout = 50

[main]
# Tap: space, Hold: nav layer.
space = overload(nav, space)

# Tap: escape, Hold: control layer.
capslock = overload(control, esc)

# Optional: oneshot shift for easier capitalization.
leftshift = oneshot(shift)

[nav]
# While holding space:
a = left
s = down
d = up
f = right

q = home
w = pagedown
e = pageup
r = end
```
