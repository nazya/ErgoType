# Apple USB HID Devices

## Enabling the drivers

A driver is built when its line in [`CMakeLists.txt`](../../../CMakeLists.txt)
and its line in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h) are
uncommented:

| Driver | `CMakeLists.txt` | `hid_compat.h` |
| --- | --- | --- |
| `hid-apple.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-apple.c` | `#define CONFIG_HID_APPLE 1` |
| `hid-magicmouse.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-magicmouse.c` | `#define CONFIG_HID_MAGICMOUSE 1` |

## Devices

All devices on this page use Vendor ID `05ac`.

Driver: `hid-apple.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Product | PID | Comments |
| --- | --- | --- |
| PowerBook keyboard and trackpad (Fountain) | `020e` ANSI, `020f` ISO | 🔴 Outside the scope: built into a Mac. |
| PowerBook keyboard and trackpad (Geyser) | `0214` ANSI, `0215` ISO, `0216` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Geyser 3) | `0217` ANSI, `0218` ISO, `0219` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Geyser 4) | `021a` ANSI, `021b` ISO, `021c` JIS | 🔴 Outside the scope: built into a Mac. |
| Aluminum Keyboard Mini | `021d` ANSI, `021e` ISO, `021f` JIS | 🟢 |
| Aluminum Keyboard | `0220` ANSI, `0221` ISO, `0222` JIS | 🟢 |
| MacBook keyboard and trackpad (Wellspring) | `0223` ANSI, `0224` ISO, `0225` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Geyser 4 HF) | `0229` ANSI, `022a` ISO, `022b` JIS | 🔴 Outside the scope: built into a Mac. |
| Wireless Keyboard (2007) | `022c` ANSI, `022d` ISO, `022e` JIS | 🔴 Hardware limits: Bluetooth. |
| MacBook keyboard and trackpad (Wellspring 2) | `0230` ANSI, `0231` ISO, `0232` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 3) | `0236` ANSI, `0237` ISO, `0238` JIS | 🔴 Outside the scope: built into a Mac. |
| Wireless Keyboard (2009) | `0239` ANSI, `023a` ISO, `023b` JIS | 🔴 Hardware limits: Bluetooth. |
| MacBook keyboard and trackpad (Wellspring 4) | `023f` ANSI, `0240` ISO, `0241` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 4A) | `0242` ANSI, `0243` ISO, `0244` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 5) | `0245` ANSI, `0246` ISO, `0247` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 6A) | `0249` ANSI, `024a` ISO, `024b` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 6) | `024c` ANSI, `024d` ISO, `024e` JIS | 🔴 Outside the scope: built into a Mac. |
| Aluminum Keyboard Rev B | `024f` ANSI, `0250` ISO, `0251` JIS | 🟢 |
| MacBook keyboard and trackpad (Wellspring 5A) | `0252` ANSI, `0253` ISO, `0254` JIS | 🔴 Outside the scope: built into a Mac. |
| Wireless Keyboard (2011) | `0255` ANSI, `0256` ISO, `0257` JIS | 🔴 Hardware limits: Bluetooth. |
| MacBook keyboard and trackpad (Wellspring 7A) | `0259` ANSI, `025a` ISO, `025b` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 7) | `0262` ANSI, `0263` ISO, `0264` JIS | 🔴 Outside the scope: built into a Mac. |
| Magic Trackpad 2 | `0265` | 🟢 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| Magic Keyboard (2015) | `0267` | 🟢 Hardware limits: Bluetooth. |
| Magic Mouse 2 | `0269` | 🟢 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| Magic Keyboard with Numeric Keypad (2015) | `026c` | 🟢 Hardware limits: Bluetooth. |
| MacBook keyboard and trackpad (Wellspring 9) | `0272` ANSI, `0273` ISO, `0274` JIS | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (T2 models) | `0278`, `027a`, `027b`, `027c`, `027d`, `027e`, `027f`, `0280`, `0340` | 🔴 Outside the scope: built into a Mac. |
| MacBook keyboard and trackpad (Wellspring 8) | `0290` ANSI, `0291` ISO, `0292` JIS | 🔴 Outside the scope: built into a Mac. |
| Magic Keyboard with Touch ID (2021) | `029a` | 🟢 Hardware limits: Bluetooth. |
| Magic Keyboard (2021) | `029c` | 🟢 Hardware limits: Bluetooth. |
| Magic Keyboard with Touch ID and Numeric Keypad (2021) | `029f` | 🟢 Hardware limits: Bluetooth. |
| Mighty Mouse | `0304` | 🟢 |
| PowerBook trackpad (Fountain, Geyser) | `030a`, `030b` | 🔴 Outside the scope: built into a Mac. |
| Magic Mouse | `030d` | 🔴 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| Magic Trackpad | `030e` | 🔴 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| Magic Keyboard (2024) | `0320` | 🟢 Hardware limits: Bluetooth. |
| Magic Keyboard with Touch ID (2024) | `0321` | 🟢 Hardware limits: Bluetooth. |
| Magic Keyboard with Touch ID and Numeric Keypad (2024) | `0322` | 🟢 Hardware limits: Bluetooth. |
| Magic Mouse (USB-C) | `0323` | 🟢 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| Magic Trackpad (USB-C) | `0324` | 🟢 `hid-magicmouse.c`<br>Hardware limits: Bluetooth. |
| IR receiver | `1440`, `8240`, `8241`, `8242`, `8243` | 🔴 `hid-appleir.c`<br>Outside the scope: built into a Mac. |
| Touch Bar backlight | `8102` | 🔴 also `hid-appletb-bl.c`<br>Outside the scope: built into a Mac. |
| Touch Bar | `8302` | 🔴 `hid-appletb-kbd.c`, `hid-multitouch.c`<br>Outside the scope: built into a Mac. |
