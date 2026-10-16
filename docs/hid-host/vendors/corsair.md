# Corsair USB HID Devices

## Enabling the driver

Most devices on this page need the Corsair driver. It is built when both of
these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-corsair.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_CORSAIR 1`

Rows marked `hid-generic.c` work without it.

## Devices

All devices on this page use Vendor ID `1b1c`.

Driver: `hid-corsair.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Keyboards and mice

| Product | PID | Comments |
| --- | --- | --- |
| K90 | `1b02` | 🟢 Backlight and macro mode control: outside the scope. |
| K70, K70 RAPIDFIRE | `1b09` | 🟢 |
| K95 RGB | `1b11` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| M65 RGB | `1b12` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| K70 RGB | `1b13` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Strafe | `1b15` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| K65 RGB | `1b17` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Glaive RGB | `1b34` | 🟢 |
| K70 RGB RAPIDFIRE | `1b38` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| K65 RGB RAPIDFIRE | `1b39` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Scimitar Pro RGB | `1b3e` | 🟢 |

### Headsets

Driver: `hid-generic.c`. Battery level, sidetone and the microphone position
(`hid-corsair-void.c`): outside the scope.

| Product | PID | Comments |
| --- | --- | --- |
| Void Wireless | `0a0c`, `0a2b`, `1b23`, `1b25`, `1b27` | 🟡 Buttons only: the firmware has no audio. |
| Void USB | `0a0f`, `1b1c`, `1b29`, `1b2a` | 🟡 Buttons only: the firmware has no audio. |
| Void Surround | `0a30`, `0a31` | 🟡 Buttons only: the firmware has no audio. |
| Void Pro Wireless | `0a14`, `0a16`, `0a1a` | 🟡 Buttons only: the firmware has no audio. |
| Void Pro USB | `0a17`, `0a1d` | 🟡 Buttons only: the firmware has no audio. |
| Void Pro Surround | `0a18`, `0a1e`, `0a1f` | 🟡 Buttons only: the firmware has no audio. |
| Void Elite Wireless | `0a51`, `0a55`, `0a75` | 🟡 Buttons only: the firmware has no audio. |
| Void Elite USB | `0a52`, `0a56` | 🟡 Buttons only: the firmware has no audio. |
| Void Elite Surround | `0a53`, `0a57` | 🟡 Buttons only: the firmware has no audio. |
