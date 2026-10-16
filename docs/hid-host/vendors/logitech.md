# Logitech USB HID Devices

## Enabling the drivers

A driver is built when its line in [`CMakeLists.txt`](../../../CMakeLists.txt)
and, where listed, its line in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h) are
uncommented:

| Driver | `CMakeLists.txt` | `hid_compat.h` |
| --- | --- | --- |
| `hid-logitech-dj.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-logitech-dj.c` | `#define CONFIG_HID_LOGITECH_DJ 1` |
| `hid-logitech-hidpp.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-logitech-hidpp.c` | `#define CONFIG_HID_LOGITECH_HIDPP 1` |
| `hid-lg.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lg.c` | — |
| `hid-lg-g15.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lg-g15.c` | — |
| `hid-multitouch.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-multitouch.c` | `#define CONFIG_HID_MULTITOUCH 1` |

Receivers need both `hid-logitech-dj.c` and `hid-logitech-hidpp.c`. The Nano
receivers `c52f` and `c534` need one more option, see
[their note](#nano-receivers). Rows marked `hid-generic.c` work without these
drivers.

## Devices

All devices on this page use Vendor ID `046d`.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Receivers

Driver: `hid-logitech-dj.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Unifying receiver | `c52b` | 🟢 |
| Unifying receiver | `c532` | 🟢 |
| [Nano receiver, mouse only](#nano-receivers) | `c52f` | 🟢 Hardware limits: RAM. |
| [Nano receiver](#nano-receivers) | `c534` | 🟢 Hardware limits: RAM. |
| G700 / G700s receiver | `c531` | 🟢 |
| G602 receiver | `c537` | 🟢 |
| [Lightspeed receiver](#lightspeed-receiver-c539) | `c539` | 🟢 Hardware limits: RAM. |
| Powerplay receiver | `c53a` | 🟢 |
| Lightspeed receiver | `c53f` | 🟢 |
| Lightspeed receiver | `c543` | 🟢 |
| Lightspeed receiver | `c547` | 🟢 |
| Lightspeed receiver | `c54d` | 🟢 |
| Bolt receiver | `c548` | 🟢 `hid-multitouch.c`, `hid-quirks.c` |
| 27 MHz receiver | `c513`, `c517` | 🔴 Outside the scope: obsolete 27 MHz device. |
| 27 MHz receiver, mouse only | `c51b` | 🔴 Outside the scope: obsolete 27 MHz device. |
| MX5000 Bluetooth receiver | `c70e` keyboard, `c70a` mouse | 🔴 Outside the scope: obsolete device. |
| MX5500 Bluetooth receiver | `c71b` keyboard, `c71c` mouse | 🔴 Outside the scope: obsolete device. |
| diNovo Edge Bluetooth receiver | `c713` keyboard, `c714` mouse | 🔴 Outside the scope: obsolete device. |
| diNovo Mini Bluetooth receiver | `c71f` keyboard, `c71e` mouse | 🔴 Outside the scope: obsolete device. |

### Devices connected through a receiver

These devices are not visible to `lsusb`; it shows the receiver. The wireless
ID is the device ID the receiver reports for the connected device.

Driver: `hid-logitech-hidpp.c` unless a row names another one.

| Product | Wireless ID | Comments |
| --- | --- | --- |
| Anywhere MX | `1017` | 🟢 |
| Performance MX | `101a` | 🟢 |
| M705 (firmware RQM17) | `101b` | 🟢 |
| K750 Solar Keyboard | `4002` | 🟢 |
| Wireless Touchpad | `4011` | 🟢 |
| K400 | `4024` | 🟢 |
| M560 | `402d` | 🟢 |
| G502 Lightspeed | `407f` | 🟢 |
| Wireless Touchpad T650 | `4101` | 🟢 |
| Other Unifying and Lightspeed devices | any | 🟢 |
| MX5000, diNovo Edge and MX5500 keyboards behind their Bluetooth receivers | `b305`, `b309`, `b30b` | 🔴 Outside the scope: obsolete device. |
| LX501, MX3000 and MX3200 keyboards, S510 Media Remote, other 27 MHz devices | `0049`, `0057`, `005c`, `00fe`, any | 🔴 Outside the scope: obsolete 27 MHz device. |

### Mice and keyboards with a USB cable

Driver: `hid-logitech-hidpp.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| G935 headset | `0a87` | 🟡 `hid-generic.c`<br>Buttons only: the firmware has no audio. Battery and connection status (`hid-logitech-hidpp.c`): outside the scope. |
| G900 | `c081` | 🟢 |
| G403 Wireless | `c082` | 🟢 |
| G903 | `c086` | 🟢 |
| G703 | `c087` | 🟢 |
| G Pro | `c088` | 🟢 |
| MX Vertical | `c08a` | 🟢 |
| G502 Lightspeed | `c08d` | 🟢 |
| G703 Hero | `c090` | 🟢 |
| G903 Hero | `c091` | 🟢 |
| G Pro X Superlight | `c094` | 🟢 |
| G Pro 2 Lightspeed | `c09a` | 🟢 |
| G Pro X Superlight 2 | `c09b` | 🟢 |
| G915 TKL | `c343` | 🟢 |

### Older keyboards and receivers

Driver: `hid-lg.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| UltraX receiver | `c101` | 🟢 |
| Elite Keyboard | `c30a` | 🟢 |
| [S510 receiver](#s510-receiver-c50c) | `c50c` | 🟢 Hardware limits: RAM. |
| Cordless Desktop LX500 | `c512` | 🟢 |
| SpaceTraveller | `c623` | 🔴 Outside the scope: 3D mouse. |
| SpaceNavigator | `c626` | 🔴 also `Logitech__SpaceNavigator.bpf.c`<br>Outside the scope: 3D mouse. |
| diNovo Desktop | `c704` | 🟢 |

### G-series keyboards

Driver: `hid-lg-g15.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Z-10 speakers | `0a07` | 🔴 Outside the scope: speakers. |
| G13 | `c21c` | 🟢 Backlight and LEDs: outside the scope. |
| G15 | `c222` | 🟢 Backlight and LEDs: outside the scope. |
| G11 | `c225` | 🟢 Backlight and LEDs: outside the scope. |
| G15 v2 | `c227` | 🟢 Backlight and LEDs: outside the scope. |
| G510 | `c22d` | 🟢 Backlight and LEDs: outside the scope. |
| G510 with a headset connected | `c22e` | 🟢 Backlight and LEDs: outside the scope. |

### Other USB devices

| Product | PID | Comments |
| --- | --- | --- |
| Group conference speakerphone | `0882` | 🟡 `hid-generic.c`, `hid-quirks.c`<br>Buttons only: the firmware has no audio. |
| USB mouse | `c007` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| USB mouse | `c01a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| USB mouse | `c05a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| USB mouse | `c06a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| USB mouse | `c077` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| G710+ keyboard | `c24d` | 🟢 `hid-generic.c`, `hid-quirks.c` |

### Wheels, joysticks and gamepads

Driver: `hid-lg.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Cordless RumblePad | `c20a` | 🔴 Outside the scope: game controller. |
| WingMan Formula GP | `c20e` | 🔴 Outside the scope: game controller. |
| RumblePad | `c211` | 🔴 Outside the scope: game controller. |
| Extreme 3D Pro | `c215` | 🔴 Outside the scope: game controller. |
| Dual Action | `c216` | 🔴 Outside the scope: game controller. |
| RumblePad 2 | `c218`, `c219` | 🔴 Outside the scope: game controller. |
| G29 | `c24f` | 🔴 Outside the scope: game controller. |
| G920 | `c262` | 🔴 `hid-logitech-hidpp.c`<br>Outside the scope: game controller. |
| G923 (Xbox version) | `c26e` | 🔴 `hid-logitech-hidpp.c`<br>Outside the scope: game controller. |
| WingMan Formula Force | `c283` | 🔴 Outside the scope: game controller. |
| Force 3D Pro | `c286` | 🔴 Outside the scope: game controller. |
| Flight System G940 | `c287` | 🔴 Outside the scope: game controller. |
| WingMan Formula Force GP | `c293` | 🔴 Outside the scope: game controller. |
| Driving Force | `c294` | 🔴 Outside the scope: game controller. |
| MOMO Racing | `c295` | 🔴 Outside the scope: game controller. |
| Driving Force Pro | `c298` | 🔴 Outside the scope: game controller. |
| G25 | `c299` | 🔴 Outside the scope: game controller. |
| Driving Force GT | `c29a` | 🔴 Outside the scope: game controller. |
| G27 | `c29b` | 🔴 Outside the scope: game controller. |
| Wii wheel | `c29c` | 🔴 Outside the scope: game controller. |
| MOMO Racing 2 | `ca03` | 🔴 Outside the scope: game controller. |
| Vibration wheel | `ca04` | 🔴 Outside the scope: game controller. |

### Bluetooth devices

Driver: `hid-logitech-hidpp.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Harmony Adapter for PS3 | `0306` | 🔴 `hid-sony.c`<br>Hardware limits: Bluetooth. |
| V470 Cordless Laser Mouse | `b008` | 🔴 Hardware limits: Bluetooth. |
| Wireless Touchpad T651 | `b00c` | 🔴 Hardware limits: Bluetooth. |
| MX Master | `b012` | 🔴 Hardware limits: Bluetooth. |
| M720 Triathlon | `b015` | 🔴 Hardware limits: Bluetooth. |
| MX Master 2S | `b019` | 🔴 Hardware limits: Bluetooth. |
| MX Ergo | `b01d`, `b01e` | 🔴 Hardware limits: Bluetooth. |
| MX Vertical | `b020` | 🔴 Hardware limits: Bluetooth. |
| MX Master 3 | `b023` | 🔴 Hardware limits: Bluetooth. |
| MX Anywhere 3 | `b025` | 🔴 Hardware limits: Bluetooth. |
| Signature M650 | `b02a` | 🔴 Hardware limits: Bluetooth. |
| MX Master 3S | `b034` | 🔴 Hardware limits: Bluetooth. |
| MX Anywhere 3S | `b037` | 🔴 Hardware limits: Bluetooth. |
| MX Anywhere 3SB | `b038` | 🔴 Hardware limits: Bluetooth. |
| MX Master 4 | `b042` | 🔴 Hardware limits: Bluetooth. |
| MX5000 keyboard | `b305` | 🔴 Hardware limits: Bluetooth. |
| diNovo Edge keyboard | `b309` | 🔴 Hardware limits: Bluetooth. |
| MX5500 keyboard | `b30b` | 🔴 Hardware limits: Bluetooth. |
| G915 TKL | `b35f` | 🔴 Hardware limits: Bluetooth. |
| MX Keys Mini | `b369`, `b36e` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Signature K650 | `b36f`, `b370` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Casa Pop-Up Desk | `b371`, `b374` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Pebble Keys 2 K380S | `b377` | 🔴 Hardware limits: Bluetooth. |
| MX Keys S | `b378`, `b380` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Wave Keys | `b383`, `b384` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Signature Slim K950 | `b386`, `b388` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Keys-To-Go 2 | `b38c` | 🔴 Hardware limits: Bluetooth. |
| Pop Icon Keys | `b38f` | 🔴 Hardware limits: Bluetooth. |
| Signature Slim Solar+ K980 | `b391`, `b394` (B2B) | 🔴 Hardware limits: Bluetooth. |
| K250 / K251 | `b397` | 🔴 Hardware limits: Bluetooth. |
| Signature Comfort K880 | `b39c`, `b39d` (B2B) | 🔴 Hardware limits: Bluetooth. |
| Casa touchpad | `bb00` | 🔴 `hid-multitouch.c`<br>Hardware limits: Bluetooth. |

## Notes

<a id="nano-receivers"></a>

### Nano receivers `046d:c52f`, `046d:c534`

The driver supports these receivers fully; this was checked with
`HID_MAX_USAGES=256`. With the firmware's `HID_MAX_USAGES=675`, their Consumer
Control reports alone need about 42 KB of RAM, which does not fit into RP2040
memory next to the rest of the receiver. They need a board with more RAM.

To include them, also uncomment
`#define CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS 1` in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h). This
option also enables the obsolete 27 MHz and Bluetooth receiver rows.

<a id="lightspeed-receiver-c539"></a>

### Lightspeed receiver `046d:c539`

Ordinary mice and keyboards behind this receiver fit into RP2040 memory. A
device with very large reports may not fit and then needs a board with more
RAM; a test device of that kind did not fit at `HID_MAX_USAGES=675`.

<a id="s510-receiver-c50c"></a>

### S510 receiver `046d:c50c`

The S510 keyboard declares media keys over a much wider code range than the
firmware keeps at `HID_MAX_USAGES=675`. On RP2040, keys beyond that range
produce no event. A board with more RAM can raise the limit.
