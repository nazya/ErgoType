# ErgoType Docs

This branch is work in progress. The RP2040-PiZero hardware path has not been tested on the physical board yet.

It is intended to act as a hardware HID remapper: plug an external USB keyboard/mouse into the RP2040-PiZero PIO-USB host port, remap events through keyd, and expose the board to the computer as a USB HID device.

- [Quickstart](quickstart.md)
- [Config: `config.json`](config-json.md)
- [Keymap](keymap.md)
- [Pointing / PMW3360 / PMW3389](pointing.md)
- [HID remapper overview](hid-remapper.md)
- [USB host implementation](usb-host.md)
- [Waveshare RP2040-PiZero board notes](waveshare-rp2040-pizero.md)
- [Porting to another board](board-porting.md)
- [keyd.conf support](keyd.md)
- [Examples](examples.md)
- [Debugging](debugging.md)
- [Logging / console IO](logging.md)
- [Boot modes (MSC vs HID)](modes.md)
- [Runtime architecture](runtime.md)
