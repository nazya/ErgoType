# ASUS USB HID Devices

## Enabling the drivers

A driver is built when its line in [`CMakeLists.txt`](../../../CMakeLists.txt)
and its line in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h) are
uncommented:

| Driver | `CMakeLists.txt` | `hid_compat.h` |
| --- | --- | --- |
| `hid-asus.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-asus.c` | `#define CONFIG_HID_ASUS 1` |
| `hid-multitouch.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-multitouch.c` | `#define CONFIG_HID_MULTITOUCH 1` |

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### ASUSTeK · VID `0b05`

Driver: `hid-asus.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| I2C touchpad | `0101` | 🔴 Outside the scope: I2C laptop device. |
| Transformer Book T100TA keyboard | `17e0` | 🔴 Outside the scope: tablet keyboard. |
| Transformer Book T100TAF keyboard | `1807` | 🔴 Outside the scope: tablet keyboard. |
| ROG G752 keyboard | `1822` | 🟢 |
| ROG laptop keyboard | `1837` | 🔴 Outside the scope: built into a laptop. |
| Transformer Mini T101HA keyboard | `183d` | 🟢 also `hid-multitouch.c` |
| Transformer T304 keyboard | `184a` | 🟢 `hid-multitouch.c` |
| ROG laptop keyboard | `1854` | 🔴 Outside the scope: built into a laptop. |
| ROG N-KEY keyboard | `1866` | 🔴 Outside the scope: built into a laptop. |
| FX503VD keyboard | `1869` | 🔴 Outside the scope: built into a laptop. |
| ROG Flow Z13 lightbar | `18c6` | 🔴 Outside the scope: built into a tablet. |
| ROG Claymore II | `196b` | 🟢 |
| ROG XG Mobile (2022) | `1970` | 🟢 |
| ROG N-KEY keyboard (second generation) | `19b6` | 🔴 Outside the scope: built into a laptop. |
| ROG Flow Z13 folio keyboard | `1a30` | 🔴 Outside the scope: tablet keyboard. |
| ROG XG Mobile (2023) | `1a9a` | 🟢 |
| ROG Ally | `1abe` | 🔴 Outside the scope: handheld game console. |
| ROG Ally X | `1b4c` | 🔴 Outside the scope: handheld game console. |
| Transformer Book T100CHI keyboard | `8502` | 🔴 Hardware limits: Bluetooth. |
| I2C keyboard | `8585` | 🔴 Outside the scope: I2C laptop device. |

### ASUS · VID `0486`

Driver: `hid-multitouch.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Eee PC T91MT touchscreen | `0185` | 🟢 |
| Touchscreen (YFO) | `0186` | 🟢 |

### Other vendor IDs

The ASUS driver also handles these devices, which use other vendors' IDs.

| Product | VID:PID | Comments |
| --- | --- | --- |
| ASUS AK1D keyboard | `04f2:1125` (Chicony) | 🟢 |
| Medion E1239T keyboard | `048d:ce50` (ITE) | 🟢 |
| ASUS MD-5110 keyboard | `062a:5110` (Turbox) | 🟢 |
| ASUS MD-5112 keyboard | `0c45:5112` (JESS) | 🟢 |
