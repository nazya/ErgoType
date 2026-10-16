# Huion, XP-Pen and UC-Logic USB HID Devices

## Enabling the drivers

The UC-Logic driver is built when these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-uclogic-core.c`,
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-uclogic-params.c`
  and `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-uclogic-rdesc.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_UCLOGIC 1`

Older UC-Logic, UGEE, UGTIZER and Trust tablets need one more option, see
[their note](#older-tablets). Rows marked with a `.bpf.c` file use a HID-BPF
program instead, see [HID-BPF programs](../README.md#hid-bpf-programs). Rows
marked `hid-generic.c` work without these drivers.

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Huion · VID `256c`

| Product | PID | Comments |
| --- | --- | --- |
| Dial 2 | `0060` | 🟢 `Huion__Dial-2.bpf.c` |
| Inspiroy 2 S | `0066` | 🟢 `Huion__Inspiroy-2-S.bpf.c` |
| Inspiroy 2 M | `0067` | 🟢 `Huion__Inspiroy-2-M.bpf.c` |
| Keydial K20 | `0069` | 🟢 `Huion__KeydialK20.bpf.c` |
| Kamvas Pro 19, Kamvas Pro 27 | `006b`, `006c` | 🟢 `hid-multitouch.c`, `Huion__Kamvas-Pro-19.bpf.c` |
| Tablets and pen displays | `006d`, `006e` | 🟢 `hid-uclogic-core.c` |
| Kamvas 13 (Gen 3) | `2008` | 🟢 `Huion__Kamvas13Gen3.bpf.c` |
| Kamvas 16 (Gen 3) | `2009` | 🟢 `Huion__Kamvas16Gen3.bpf.c` |
| Inspiroy Frego M | `2012` | 🟢 `Huion__Inspiroy-Frego-M.bpf.c` |
| Keydial K20, Inspiroy Frego M | `8251` | 🔴 `Huion__KeydialK20-Bluetooth.bpf.c`, `Huion__Inspiroy-Frego-M.bpf.c`<br>Hardware limits: Bluetooth. |

### XP-Pen · VID `28bd`

Driver: `hid-uclogic-core.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| Deco 01 | `0042` | 🟢 |
| [UGEE Rainbow CV720](#older-tablets) | `0055` | 🟢 |
| [UGEE EX07S](#older-tablets) | `0071` | 🟢 |
| [UGEE G5](#older-tablets) | `0074` | 🟢 |
| [G540](#older-tablets) | `0075` | 🟢 |
| [Star 06](#older-tablets) | `0078` | 🟢 |
| Star G640 | `0094` | 🟢 |
| ACK05 remote | `0202` | 🟢 `XPPen__ACK05.bpf.c` |
| Deco 02 | `0803` | 🟢 `XPPen__Deco02.bpf.c` |
| Deco 01 V2 | `0905` | 🟢 |
| Deco Pro S | `0909` | 🟢 |
| Artist 22R Pro | `091b` | 🟢 |
| Deco Mini 4 | `0929` | 🟢 `XPPen__DecoMini4.bpf.c` |
| Artist 24 Pro | `092d` | 🟢 |
| Deco Pro SW | `0933` | 🟢 |
| Deco Pro MW | `0934` | 🟢 |
| Deco L, Deco LW | `0935` | 🟢 |
| Artist 24 | `093a` | 🟢 `XPPen__Artist24.bpf.c` |
| Deco 01 V3 | `0947` | 🟢 `XPPen__Deco01V3.bpf.c` |
| Artist Pro 14, 16 and 19 (Gen 2) | `095a`, `095b`, `096a` | 🟢 `XPPen__ArtistPro16Gen2.bpf.c` |
| Parblo A610 Pro | `1903` | 🟢 |

### UC-Logic · VID `5543`

Driver: `hid-uclogic-core.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| [WP4030U](#older-tablets) | `0003` | 🟢 |
| [WP5540U](#older-tablets) | `0004` | 🟢 |
| [WP8060U](#older-tablets) | `0005` | 🟢 |
| [PF1209](#older-tablets) | `0042` | 🟢 |
| [UGEE tablet](#older-tablets) | `0045`, `0047`, `0081` | 🟢 |
| [Yiynova tablet](#older-tablets) | `004d` | 🟢 |
| [WP1062, TWA60](#older-tablets) | `0064` | 🟢 |
| [Huion tablet](#older-tablets) | `006e` | 🟢 |
| [TWHL850 wireless tablet](#older-tablets) | `0522` | 🟢 |
| [TWHA60](#older-tablets) | `0781` | 🟢 |
| [DrawImage G3](#older-tablets) | `3031` | 🟢 |
| KNA5 | `6001` | 🟢 `hid-generic.c`, `hid-quirks.c` |

### UGTIZER · VID `2179`

Driver: `hid-uclogic-core.c` unless a row names another one.

| Product | PID | Comments |
| --- | --- | --- |
| WP5540 | `0004` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| [GP0610](#older-tablets) | `0053` | 🟢 |
| [GT5040](#older-tablets) | `0077` | 🟢 |

### Trust · VID `145f`

| Product | PID | Comments |
| --- | --- | --- |
| [Panora tablet](#older-tablets) | `0212` | 🟢 `hid-uclogic-core.c` |
| Philips SPK6327 keyboard | `024b` | 🟢 `Trust__Philips-SPK6327.bpf.c` |

## Notes

<a id="older-tablets"></a>

### Older UC-Logic, UGEE, UGTIZER and Trust tablets

- XP-Pen and UGEE: `28bd:0055`, `28bd:0071`, `28bd:0074`, `28bd:0075`,
  `28bd:0078`
- UC-Logic: `5543:0003`, `5543:0004`, `5543:0005`, `5543:0042`,
  `5543:0045`, `5543:0047`, `5543:004d`, `5543:0064`, `5543:006e`,
  `5543:0081`, `5543:0522`, `5543:0781`, `5543:3031`
- UGTIZER: `2179:0053`, `2179:0077`
- Trust: `145f:0212`

To include these tablets, also uncomment
`#define CONFIG_HID_UCLOGIC_ALL_DEVICES 1` in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h).
