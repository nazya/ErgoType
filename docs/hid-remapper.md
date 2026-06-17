# HID Remapper

This branch turns Waveshare RP2040-PiZero into a hardware USB HID remapper.

Status: work in progress. The RP2040-PiZero hardware path has not been tested on the physical board yet.

```text
external keyboard/mouse
-> RP2040-PiZero PIO-USB host port
-> TinyUSB host callbacks
-> usb_host/
-> keyd
-> TinyUSB device keyboard/mouse
-> computer
```

The practical use case: plug a normal USB keyboard or mouse into the board, let keyd remap it, and connect the board to the computer as the final HID device.

## Runtime path

The external device is handled by TinyUSB host on the PIO-USB root port. HID reports are parsed in `usb_host/`, converted to `struct device_event`, and pushed into the same device/event path used by onboard keyboard and pointing devices.

Keyd sees the external HID interface as another input device. Output still goes through the existing virtual keyboard/mouse backend and TinyUSB device descriptors.

## Board dependency

The feature needs two USB paths:

- one USB device path connected to the computer;
- one USB host path connected to the external keyboard/mouse.

Board-specific details are in [Waveshare RP2040-PiZero board notes](waveshare-rp2040-pizero.md). Porting steps are in [Porting to another board](board-porting.md).

## Third-party code

PIO-USB support comes from vendored `Pico-PIO-USB` files in this branch.
Only `Pico-PIO-USB/src/` and `Pico-PIO-USB/LICENSE` are tracked.

The firmware links it through `tinyusb_pico_pio_usb`.

## Current scope

Current scope is keyboard/mouse remapping. The host parser already understands more HID report shapes than boot keyboard/mouse, but the output side is still the existing keyboard/mouse HID device path.

Consumer/media/system keys need two pieces to be complete:

- host-side usage mapping from external HID reports to keyd events;
- device-side Consumer Control report support when the final output usage is not valid on the Keyboard/Keypad page.
