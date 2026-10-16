# Genius (KYE) USB HID Devices

## Enabling the driver

Most devices on this page need the KYE driver. It is built when both of these
lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kye.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_KYE 1`

Rows marked `hid-generic.c` work without it.

## Devices

All devices on this page use Vendor ID `0458`.

Driver: `hid-kye.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Mice and keyboards

| Product | PID | Comments |
| --- | --- | --- |
| Ergo 525V mouse | `0087` | 🟢 |
| PixArt USB optical mouse | `0137` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Gila gaming mouse | `0138` | 🟢 |
| Manticore keyboard | `0153` | 🟢 |
| GX Imperator keyboard | `4018` | 🟢 |
| Bluetooth device | `0058` | 🔴 Hardware limits: Bluetooth. |

### Tablets

| Product | PID | Comments |
| --- | --- | --- |
| G-Pen 560 | `5003` | 🔴 Separate non-HID driver in Linux. |
| EasyPen M406 | `5005` | 🟢 |
| EasyPen M506 | `500f` | 🟢 |
| EasyPen i405X | `5010` | 🟢 |
| MousePen i608X | `5011` | 🟢 |
| EasyPen M406W | `5012` | 🟢 |
| EasyPen M610X | `5013` | 🟢 |
| EasyPen 340 | `5014` | 🟢 |
| PenSketch M912 | `5015` | 🟢 |
| MousePen M508WX | `5016` | 🟢 |
| MousePen M508X | `5017` | 🟢 |
| EasyPen M406XE | `5019` | 🟢 |
| MousePen i608X v2 | `501a` | 🟢 |
| PenSketch T609A | `501b` | 🟢 |
