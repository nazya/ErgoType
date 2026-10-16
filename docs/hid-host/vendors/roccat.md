# Roccat USB HID Devices

## Enabling the driver

The Kone needs the Roccat Kone driver. It is built when both of these lines are
uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-roccat-kone.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_ROCCAT_KONE 1`

Rows marked `hid-generic.c` work without it.

## Devices

All devices on this page use Vendor ID `1e7d`.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

The Linux Roccat drivers let the vendor's tool change the settings stored in
the device (profiles, DPI levels, macros, lights) and receive notifications.
Keys, buttons, pointer and wheel do not depend on them, and the DPI button on a
mouse keeps working because the mouse switches DPI itself. The firmware
therefore handles models other than the Kone with `hid-generic.c`.

| Product | PID | Comments |
| --- | --- | --- |
| Pyra (wired) | `2c24` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-pyra.c`): outside the scope. |
| Lua | `2c2e` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-lua.c`): outside the scope. |
| Kone | `2ced` | 🟢 `hid-roccat-kone.c`<br>Configuration: outside the scope. |
| Pyra (wireless) | `2cf6` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-pyra.c`): outside the scope. |
| Kova+ | `2d50` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-kovaplus.c`): outside the scope. |
| Kone+ | `2d51` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-koneplus.c`): outside the scope. |
| Savu | `2d5a` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-savu.c`): outside the scope. |
| KonePure Optical | `2db4` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-konepure.c`): outside the scope. |
| KonePure | `2dbe` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-konepure.c`): outside the scope. |
| Kone XTD | `2e22` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-koneplus.c`): outside the scope. |
| Arvo | `30d4` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-arvo.c`): outside the scope. |
| Ryos MK | `3138` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-ryos.c`): outside the scope. |
| Isku | `319c` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-isku.c`): outside the scope. |
| Ryos MK Glow | `31ce` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-ryos.c`): outside the scope. |
| Ryos MK Pro | `3232` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-ryos.c`): outside the scope. |
| Isku FX | `3264` | 🟢 `hid-generic.c`<br>Configuration (`hid-roccat-isku.c`): outside the scope. |
