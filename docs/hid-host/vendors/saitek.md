# Saitek and Mad Catz USB HID Devices

## Enabling the driver

Most devices on this page need the Saitek driver. It is built when both of
these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-saitek.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_SAITEK 1`

Rows marked `hid-generic.c` work without it.

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Saitek · VID `06a3`

Driver: `hid-saitek.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| X52 joystick (second version) | `0255` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PS1000 gamepad | `0621` | 🟢 |
| X52 joystick | `075c` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| X52 Pro joystick | `0762` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| X65 joystick | `0b6a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| R.A.T.7 (old) | `0ccb` | 🟢 |
| R.A.T.7 Contagion | `0ccd` | 🟢 |
| M.M.O.7 | `0cd0` | 🟢 |
| R.A.T.7 | `0cd7` | 🟢 |
| R.A.T.9 | `0cfa` | 🟢 |
| Rumblepad | `ff17` | 🟢 `hid-generic.c`, `hid-quirks.c` |

### Mad Catz · VID `0738`

Driver: `hid-saitek.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| R.A.T.5 | `1705` | 🟢 |
| R.A.T.9 | `1709` | 🟢 |
| M.M.O.7 | `1713` | 🟢 |
| PS4 Stratocaster guitar | `8261` | 🔴 `hid-sony.c`<br>Hardware limits: Bluetooth. |
