# Sony USB HID Devices

## Enabling the driver

The VAIO mice need the Sony driver. It is built when this line in
[`CMakeLists.txt`](../../../CMakeLists.txt) is uncommented:

- `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sony.c`

It needs no line in `hid_compat.h`.

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Sony · VID `054c`

Driver: `hid-sony.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Buzz controller | `0002` | 🔴 Outside the scope: game controller. |
| VAIO VGX mouse | `024b` | 🟢 |
| PlayStation 3 controller | `0268` | 🔴 Outside the scope: game controller. |
| PlayStation 3 Blu-ray remote | `0306` | 🔴 Hardware limits: Bluetooth. |
| VAIO VGP mouse | `0374` | 🟢 |
| PlayStation Move controller | `03d5` | 🔴 Outside the scope: game controller. |
| PlayStation Navigation controller | `042f` | 🔴 Outside the scope: game controller. |
| DualShock 4 | `05c4`, `09cc` | 🔴 `hid-playstation.c`<br>Outside the scope: game controller. |
| DualShock 4 USB wireless adapter | `0ba0` | 🔴 `hid-playstation.c`<br>Outside the scope: game controller. |
| DualSense | `0ce6` | 🔴 `hid-playstation.c`<br>Outside the scope: game controller. |
| DualSense Edge | `0df2` | 🔴 `hid-playstation.c`<br>Outside the scope: game controller. |
| Wireless Buzz controller | `1000` | 🔴 Outside the scope: game controller. |

### Sony rhythm game instruments · VID `12ba`

Driver: `hid-sony.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Guitar Hero guitar (PS3) | `0100` | 🔴 Outside the scope: game controller. |
| Guitar Hero drums (PS3) | `0120` | 🔴 Outside the scope: game controller. |
| DJ Hero turntable (PS3) | `0140` | 🔴 Outside the scope: game controller. |
| Rock Band guitar (PS3) | `0200` | 🔴 Outside the scope: game controller. |
| Rock Band drums (PS3) | `0210`, `0218` (MIDI Pro Adapter) | 🔴 Outside the scope: game controller. |
| Guitar Hero Live dongle (PS3, Wii U) | `074b` | 🔴 Outside the scope: game controller. |
| Rock Band 3 keyboard (PS3) | `2330`, `2338` (MIDI Pro Adapter) | 🔴 Outside the scope: game controller. |
| Rock Band 3 Mustang guitar (PS3) | `2430`, `2438` (MIDI Pro Adapter) | 🔴 Outside the scope: game controller. |
| Rock Band 3 Squier guitar (PS3, MIDI Pro Adapter) | `2538` | 🔴 Outside the scope: game controller. |

### Other vendor IDs

The Sony driver also handles these devices, which use other vendors' IDs.

Driver: `hid-sony.c` unless a row names another one.

| Product | VID:PID | Comments |
| --- | --- | --- |
| CRKD Gibson SG guitar (PS4) | `3651:1500`, `3651:5500` (dongle) | 🔴 Outside the scope: game controller. |
| CRKD Gibson SG guitar (PS5) | `3651:1600`, `3651:5600` (dongle) | 🔴 Outside the scope: game controller. |
| Harmonix Rock Band instruments (Wii) | `1bad:0004`, `1bad:0005`, `1bad:3010`, `1bad:3110`, `1bad:3138`, `1bad:3330`, `1bad:3338`, `1bad:3430`, `1bad:3438`, `1bad:3538` | 🔴 Outside the scope: game controller. |
| PDP Jaguar guitar (PS4) | `0e6f:0173` | 🔴 Hardware limits: Bluetooth. |
| PDP Riffmaster guitar | `0e6f:0249` (PS5), `0e6f:024a` (PS4) | 🔴 Outside the scope: game controller. |
| RedOctane Guitar Hero dongles | `1430:07bb` (Live, PS4), `1430:474c` | 🔴 Outside the scope: game controller. |
| Sino Lite PS3-compatible controller | `1345:3008` | 🔴 Outside the scope: game controller. |
| SMK PlayStation 3 Blu-ray remote | `0609:0306` | 🔴 Hardware limits: Bluetooth. |
| SMK NSG-MR5U and NSG-MR7U remotes | `0609:0368`, `0609:0369` | 🔴 Hardware limits: Bluetooth. |

The Mad Catz Stratocaster guitar is on the [Saitek and Mad Catz](saitek.md)
page and the Logitech Harmony Adapter for PS3 on the [Logitech](logitech.md)
page.
