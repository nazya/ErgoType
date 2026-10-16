# Lenovo and IBM USB HID Devices

## Enabling the Lenovo driver

Most devices on this page need the Lenovo driver. It is built when both of
these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lenovo.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_LENOVO 1`

ScrollPoint mice, the Pro Dock and the TrackPoint Keyboard II need two more
options, see [their note](#extra-option). Rows marked `hid-generic.c` work without
the Lenovo driver. The ISD-V4 digitizer and the Yoga Book 9i use the Wacom and
multitouch drivers instead.

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Lenovo · VID `17ef`

Driver: `hid-lenovo.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| ISD-V4 pen digitizer | `6004` | 🟢 `wacom_sys.c`, `wacom_wac.c` |
| ThinkPad USB Keyboard with TrackPoint | `6009` | 🟢 Mute LEDs: outside the scope. |
| Optical USB Mouse | `600e` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PixArt USB Mouse | `6019` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PixArt USB Mouse | `602e` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ThinkPad Compact USB Keyboard with TrackPoint | `6047` | 🟢 |
| ThinkPad Compact Bluetooth Keyboard with TrackPoint | `6048` | 🔴 Hardware limits: Bluetooth. |
| [ScrollPoint Optical Mouse](#extra-option) | `6049` | 🟢 |
| ThinkPad 10 Ultrabook Keyboard | `6062` | 🔴 Outside the scope: tablet keyboard. |
| [ThinkPad Pro Dock](#extra-option) | `6067` | 🟢 |
| ThinkPad X1 Tablet Cover | `6085` | 🔴 `hid-rmi.c`<br>Outside the scope: tablet keyboard. |
| PixArt USB Mouse | `608d` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PixArt USB Mouse | `6093` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ThinkPad X1 Tablet keyboard | `60a3` | 🔴 also `hid-multitouch.c`<br>Outside the scope: tablet keyboard. |
| ThinkPad X1 Tablet keyboard | `60a4` | 🔴 also `hid-multitouch.c`<br>Outside the scope: tablet keyboard. |
| ThinkPad X1 Tablet keyboard | `60b5` | 🔴 also `hid-multitouch.c`<br>Outside the scope: tablet keyboard. |
| ThinkPad TrackPoint Keyboard II | `60e1` | 🔴 Hardware limits: Bluetooth. |
| [ThinkPad TrackPoint Keyboard II](#thinkpad-trackpoint-keyboard-ii) | `60ee` | 🟢 Hardware limits: RAM. |
| ThinkPad X12 Detachable keyboard | `60fe` | 🔴 also `hid-multitouch.c`<br>Outside the scope: tablet keyboard. |
| Yoga Book 9i | `6161` | 🟢 `hid-multitouch.c` |
| Legion Go controller (dual DInput) | `6184` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ThinkPad X12 Detachable keyboard | `61ae` | 🔴 also `hid-multitouch.c`<br>Outside the scope: tablet keyboard. |
| Legion Go 2 controller (XInput) | `61eb` | 🔴 `hid-lenovo-go.c`<br>Outside the scope: handheld game controller. |
| Legion Go 2 controller (DInput) | `61ec` | 🔴 `hid-lenovo-go.c`<br>Outside the scope: handheld game controller. |
| Legion Go 2 controller (dual DInput) | `61ed` | 🔴 `hid-lenovo-go.c`<br>Outside the scope: handheld game controller. |
| Legion Go 2 controller (FPS mode) | `61ee` | 🔴 `hid-lenovo-go.c`<br>Outside the scope: handheld game controller. |

### IBM · VID `04b3`

Driver: `hid-lenovo.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| [ScrollPoint III](#extra-option) | `3100` | 🟢 |
| [ScrollPoint Pro](#extra-option) | `3103` | 🟢 |
| [ScrollPoint Optical](#extra-option) | `3105` | 🟢 |
| [ScrollPoint Optical 800 dpi](#extra-option) | `3108` | 🟢 |
| [ScrollPoint Optical 800 dpi Pro](#extra-option) | `3109` | 🟢 |

## Notes

<a id="thinkpad-trackpoint-keyboard-ii"></a>

### ThinkPad TrackPoint Keyboard II `17ef:60ee`

The driver supports this keyboard fully; this was checked with
`HID_MAX_USAGES=256`. With the firmware's `HID_MAX_USAGES=675`, its Consumer
Control report needs more memory than RP2040 has, so it needs a board with more
RAM. Lowering `HID_MAX_USAGES` to 256 is not a workaround: other keyboards
would then lose media and browser keys with codes above `0xff`.

It also needs the [extra options](#extra-option).

<a id="extra-option"></a>

### ScrollPoint mice, Pro Dock and TrackPoint Keyboard II

- ScrollPoint mice: `04b3:3100`, `04b3:3103`, `04b3:3105`, `04b3:3108`,
  `04b3:3109`, `17ef:6049`
- ThinkPad Pro Dock: `17ef:6067`
- ThinkPad TrackPoint Keyboard II: `17ef:60ee`

To include these devices, also uncomment both of these lines in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):

- `#define CONFIG_HID_LENOVO_ALL_DEVICES 1`
- `#define CONFIG_LEDS_CLASS 1`

The first option also enables the other rows of the Lenovo driver, such as the
tablet keyboards. Their code needs the second option.
