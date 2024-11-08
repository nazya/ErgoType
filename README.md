# ErgoType
ErgoType is a project I'm developing to integrate all the keyboard features I find most valuable. Currently, it includes a port of [`keyd`](https://github.com/rvaiya/keyd) to the Raspberry Pi Pico with added functionality.


## Goals
ErgoType aims to extend the functionality of keyd beyond Linux by creating a hardware solution for a range of platforms and devices. It also aims to combine a keyboard with a pointing device, focusing on ergonomic design.


## Features
- **Easy Setup**  
  Just define rows and columns pins and a corresponding keymap in the configuration file.

- **Easy Configuration**  
  Dynamic device configuration with 2 USB modes: mass storage device class (MSC) or human interface device class (HID).

- **Easy Firmware Access**  
  The latest firmware is automatically built and available via GitHub Actions! Simply download the firmware from the GitHub Actions page without any setup or additional packages.

- **overloadtm** (keyd related)  
  Identical to overloadt, but additionally executes a supplied macro on layer activation.


## Flashing
1. Download the `.uf2` file from GitHub Actions and flash your RP Pico.  
   After flashing, you will see 'ErgoType' MSC device with an empty `config.json` file.

2. The minimal configuration requires `"gpio_rows"`, `"gpio_cols"`, and `"keymap"`. Here’s an example:
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
   ````
    For `"keymap"` one can use either keycodes or keyd names (refer to `keycode_table` in `keyd/src/keys.c`).

4. Optionally, create keyd.conf for additional configurations.


## Configuration
The `config.json` file allows you to set the following parameters:
| Field              | Type       | Default | Description |
|--------------------|------------|---------|-------------|
| `uart_rx_pin`      | `int8`     | `-1`    | GPIO pin number for UART RX. Set to `-1` if not used. Supported pins: 1, 13, 17, 29 |
| `uart_tx_pin`      | `int8`     | `-1`    | GPIO pin number for UART TX. Set to `-1` if not used. Supported pins: 0, 12, 16, 28 |
| `log_level`        | `int8`     | `0`     | Specifies the verbosity level of logging output. |
| `led_pin`          | `int8`     | `-1`    | GPIO pin number for the status LED. |
| `erase_pin`        | `int8`     | `-1`    | GPIO pin used to trigger settings erase (active low). |
| `nr_pressed_erase` | `int8`     | `9`     | Number of simultaneously pressed keys on start up to trigger file system reinit. Only for the default `erase_pin` set.|
| `msc_pin`          | `int8`     | `-1`    | GPIO pin to switch to MSC mode (active low). |
| `nr_pressed_msc`   | `int8`     | `3`     | Number of simultaneously pressed keys on start up to switch to MSC mode. Only for the default `msc_pin` set.|
| `scan_period`      | `int8`     | `5`     | Time in milliseconds between scans for key state changes. |
| `debounce`         | `int8`     | `9`     | Time in milliseconds to stabilize key states, mitigating bouncing effects. |
| `has_diodes`       | `int8`     | `1`     | Indicates if the key matrix is diode-protected (`1` for col2rows, `0` for no). |
| `gpio_rows`        | `int[]`    | N/A     | Array of GPIO pins configured as rows in the key matrix. |
| `gpio_cols`        | `int[]`    | N/A     | Array of GPIO pins configured as columns in the key matrix. |
| `keymap`           | `string(int)[][]`| N/A | Two-dimensional array specifying the key codes or key names for each matrix position. |
| `pull_up_pins`     | `int[]`    | N/A     | Array of GPIO pins configured with internal pull-up resistors. |
| `pull_down_pins`   | `int[]`    | N/A     | Array of GPIO pins configured with internal pull-down resistors. |

Supported diodes direction -- col2row, but one can always flip `gpio_row` and `gpio_cols` and transpose a `keymap`.

### keyd
The `keyd.conf` is compatible with standard keyd configuration syntax, with the exception of **IPC**, **Unicode**, **File Inclusion**, **[ids]** section (parsed, but skipped) and **command**s (parsed, but skipped).

Check [`keyd docs`](https://github.com/rvaiya/keyd/tree/master/docs/keyd.scdoc)

## Hillside 46 Demo
Using the RP2040 ProMicro I configured the left-hand part of the keyboard. Check the demo [video](https://www.youtube.com/watch?v=nl_YRXBFJNs)
````json
{
    "log_level": 1,
    "uart_rx_pin": 13,
    "uart_tx_pin": 16,
    "led_pin": 17,
    "scan_period": 5,
    "debounce": 9,
    "has_diodes": 1,
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
````

Then I created the following `keyd.conf` file

````
[global]
oneshot_timeout = 300
overload_tap_timeout = 300
chord_timeout = 200
overloadtm_timeout = 200

[main]
space = overload(space, space)
capslock = overload(rightcontrol, escape)
leftcontrol = layer(rightronctol)

a = overloadtm(space+rightcontrol, 1, a)
s = overloadtm(space+rightcontrol, 2, s)
d = overloadtm(space+rightcontrol, 3, d)
f = overloadtm(space+rightcontrol, 4, f)
g = overloadtm(space+rightcontrol, @, g)

[rightcontrol:C]
space = overload(space, C-space)

[space]
s = macro(p rint S-9 S-0 left)
rightcontrol = overload(rightalt, escape)


[space+rightcontrol]
a = layerm(space+rightcontrol, 1)
s = layerm(space+rightcontrol, 2)
d = layerm(space+rightcontrol, 3)
f = layerm(space+rightcontrol, 4)
g = layerm(space+rightcontrol, @)
````


## Debugging
ErgoType inherits from `keyd` multiple levels of debug output (0 to 3, including an additional detailed logging level). To enable debug messages, set `uart_rx_pin` and `uart_tx_pin`.

Led blinking patterns indicate device status:
- **not mounted**: 250 ms
- **mounted**: 1000 ms
- **suspended**: 2500 ms (not implemented)

## TODO
- split keyboards and compatibility with qmk communication protocol 
- pointing devices
- pico w
- suspend mode
- arbitrary multiplex scan matrix (see [`PicoMK`](https://github.com/zli117/PicoMK))

## Bugs and Feedback
If you encounter a bug, please feel free to file an issue.

I also welcome your feedback and suggestions! Check [`ErgoType Discord server`](https://discord.gg/BXfzjKU5).

## Acknowledgments
A big thank you to all the keyd developers and contributors, especially @rvaiya and @gkbd. Special thanks to @oyama and @hakanrw for their contributions to integrating MSC USB, FatFs operations on PICO flash memory, and FreeRTOS. I also appreciate the efforts of the FreeRTOS, TinyUSB, coreJSON, and QMK developers and contributors for enhancing this project's capabilities.

