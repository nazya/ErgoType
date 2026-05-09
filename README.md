# ErgoType
ErgoType is a project I'm developing to integrate all the keyboard features I find most valuable. Currently, it includes a port of [`keyd`](https://github.com/rvaiya/keyd) to the Raspberry Pi Pico with added functionality.

Documentation moved to `docs/` to keep this README short:
- `docs/README.md` (index)
- `docs/quickstart.md`
- `docs/config-json.md`
- `docs/keymap.md`
- `docs/pointing.md`
- `docs/keyd.md`
- `docs/examples.md`
- `docs/debugging.md`
- `docs/logging.md`

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

## Bugs and Feedback
If you encounter a bug, please feel free to file an issue.

I also welcome your feedback and suggestions! Check [`ErgoType Discord server`](https://discord.gg/BXfzjKU5).

## Acknowledgments
A big thank you to all the keyd developers and contributors, especially @rvaiya and @gkbd. Special thanks to @oyama and @hakanrw for their contributions to integrating MSC USB, FatFs operations on PICO flash memory, and FreeRTOS. I also appreciate the efforts of the FreeRTOS, TinyUSB, coreJSON, and QMK developers and contributors for enhancing this project's capabilities.
