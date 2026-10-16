# ELECOM USB HID Devices

## Enabling the driver

The ELECOM driver is built when both of these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-elecom.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_ELECOM 1`

## Devices

All devices on this page use Vendor ID `056e`.

Driver: `hid-elecom.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Product | PID | Comments |
| --- | --- | --- |
| BM084 Bluetooth mouse | `0061` | 🔴 Hardware limits: Bluetooth. |
| M-XGL20DLBK | `00e6` | 🟢 |
| M-XT3URBK | `00fb`, `018f` | 🟢 |
| M-XT3DRBK | `00fc`, `018c` | 🟢 |
| M-XT4DRBK | `00fd` | 🟢 |
| M-DT1URBK | `00fe` | 🟢 |
| M-DT1DRBK | `00ff` | 🟢 |
| M-HT1URBK | `010c`, `019b` | 🟢 |
| M-HT1DRBK | `010d`, `011c` | 🟢 |
| M-DT2DRBK | `018d` | 🟢 |
| M-HT1MRBK | `01aa`, `01ab` | 🟢 |
| M-HT1MRBK (Bluetooth) | `01ac` | 🔴 Hardware limits: Bluetooth. |
