# Microsoft USB HID Devices

## Enabling the Microsoft driver

Most devices on this page need the Microsoft driver. It is built when both
of these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-microsoft.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_MICROSOFT 1`

Rows marked `hid-generic.c` work without it. SideWinder Game Voice needs one
more option, see [its note](#sidewinder-game-voice).

## Devices

All devices on this page use Vendor ID `045e`.

Driver: `hid-microsoft.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Product | PID | Comments |
| --- | --- | --- |
| [SideWinder Game Voice](#sidewinder-game-voice) | `003b` | 🟢 |
| Office Keyboard | `0048` | 🟢 |
| Wireless Optical Desktop 3.0 | `009d` | 🟢 |
| Digital Media Keyboard 7000 | `00b4` | 🟢 |
| PixArt Mouse | `00cb` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Natural Ergonomic Keyboard 4000 | `00db` | 🟢 |
| Natural Ergonomic Keyboard 4000 (Japanese) | `00dc` | 🟢 |
| Comfort Keyboard | `00e3` | 🟢 |
| Laser Keyboard 6000 | `00f9` | 🟢 |
| 8BitDo SN30 Pro+ | `02e0` | 🔴 Hardware limits: Bluetooth. |
| Xbox Wireless Controller (model 1708) | `02fd` | 🔴 Hardware limits: Bluetooth. |
| Presenter 8000 | `0701` | 🔴 Hardware limits: Bluetooth. |
| Presenter 8000 | `0713` | 🟢 |
| Natural Ergonomic Keyboard 7000 | `071d` | 🟢 |
| Digital Media Keyboard 3000 | `0730` | 🟢 |
| Digital Media Keyboard 3000 v1 | `0732` | 🟢 |
| Digital Media Keyboard 600 | `0750` | 🟢 |
| Comfort Mouse 4500 | `076c` | 🟢 |
| Mouse 0783 | `0783` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Surface Pro 2 | `0799` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Touch Cover 2 | `07a7` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Type Cover 2 | `07a9` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Power Cover | `07da` | 🟢 |
| Surface 3 Cover | `07de` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Surface Dial | `091b` | 🔴 Hardware limits: Bluetooth. |
| Xbox Elite Wireless Controller Series 2 (model 1797) | `0b05` | 🔴 Hardware limits: Bluetooth. |
| Xbox Wireless Controller (model 1914) | `0b13` | 🔴 Hardware limits: Bluetooth. |
| Xbox Wireless Controller (model 1708, BLE) | `0b20` | 🔴 Hardware limits: Bluetooth. |
| Xbox Elite Wireless Controller Series 2 (model 1797, BLE) | `0b22` | 🔴 also `Microsoft__Xbox-Elite-2.bpf.c`<br>Hardware limits: Bluetooth. |

## Notes

<a id="sidewinder-game-voice"></a>

### SideWinder Game Voice `045e:003b`

Enabling the Microsoft driver does not include this device. As a gaming
accessory it has its own option. To include it, also uncomment:

- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_MICROSOFT_ALL_DEVICES 1`
- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/ff-memless.c`

The first option also builds the Xbox controller force-feedback code, which
needs the second file.
