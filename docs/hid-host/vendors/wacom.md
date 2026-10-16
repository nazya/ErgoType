# Wacom USB HID Devices

## Enabling the driver

The Wacom driver is built when both of these lines in
[`CMakeLists.txt`](../../../CMakeLists.txt) are uncommented:

- `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/wacom_wac.c`
- `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/wacom_sys.c`

It needs no line in `hid_compat.h`. The Art Pen fix for the Intuos Pro 2 M is a
HID-BPF program, see [HID-BPF programs](../README.md#hid-bpf-programs).

## Devices

All devices on this page use Vendor ID `056a`.

Driver: `wacom_sys.c`, `wacom_wac.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

### Pen tablets

| Product | PID | Comments |
| --- | --- | --- |
| Penpartner | `0000` | 🟢 |
| Graphire | `0010` | 🟢 |
| Graphire2 4x5 | `0011` | 🟢 |
| Graphire2 5x7 | `0012` | 🟢 |
| Graphire3 | `0013` | 🟢 |
| Graphire3 6x8 | `0014` | 🟢 |
| Graphire4 4x5 | `0015` | 🟢 |
| Graphire4 6x8 | `0016` | 🟢 |
| BambooFun 4x5 | `0017` | 🟢 |
| BambooFun 6x8 | `0018` | 🟢 |
| Bamboo1 Medium | `0019` | 🟢 |
| Intuos 4x5 | `0020` | 🟢 |
| Intuos 6x8 | `0021` | 🟢 |
| Intuos 9x12 | `0022` | 🟢 |
| Intuos 12x12 | `0023` | 🟢 |
| Intuos 12x18 | `0024` | 🟢 |
| Intuos5 touch S | `0026` | 🟢 |
| Intuos5 touch M | `0027` | 🟢 |
| Intuos5 touch L | `0028` | 🟢 |
| Intuos5 S | `0029` | 🟢 |
| Intuos5 M | `002a` | 🟢 |
| Intuos2 4x5 | `0041` | 🟢 |
| Intuos2 6x8 | `0042`, `0047` | 🟢 |
| Intuos2 9x12 | `0043` | 🟢 |
| Intuos2 12x12 | `0044` | 🟢 |
| Intuos2 12x18 | `0045` | 🟢 |
| Volito | `0060` | 🟢 |
| PenStation2 | `0061` | 🟢 |
| Volito2 4x5 | `0062` | 🟢 |
| Volito2 2x3 | `0063` | 🟢 |
| PenPartner2 | `0064` | 🟢 |
| Bamboo | `0065` | 🟢 |
| Bamboo1 | `0069` | 🟢 |
| Bamboo1 4x6 | `006a` | 🟢 |
| Bamboo1 5x8 | `006b` | 🟢 |
| Intuos3 4x5 | `00b0` | 🟢 |
| Intuos3 6x8 | `00b1` | 🟢 |
| Intuos3 9x12 | `00b2` | 🟢 |
| Intuos3 12x12 | `00b3` | 🟢 |
| Intuos3 12x19 | `00b4` | 🟢 |
| Intuos3 6x11 | `00b5` | 🟢 |
| Intuos3 4x6 | `00b7` | 🟢 |
| Intuos4 4x6 | `00b8` | 🟢 |
| Intuos4 6x9 | `00b9` | 🟢 |
| Intuos4 8x13 | `00ba` | 🟢 |
| Intuos4 12x19 | `00bb` | 🟢 |
| Intuos4 WL | `00bc` | 🟢 |
| Bamboo 2FG | `00d0` | 🟢 |
| Bamboo 2FG 4x5 | `00d1` | 🟢 |
| Bamboo Craft | `00d2` | 🟢 |
| Bamboo 2FG 6x8 | `00d3` | 🟢 |
| Bamboo Pen | `00d4` | 🟢 |
| Bamboo Pen 6x8 | `00d5` | 🟢 |
| BambooPT 2FG 4x5 | `00d6` | 🟢 |
| BambooPT 2FG Small | `00d7` | 🟢 |
| Bamboo Comic 2FG | `00d8` | 🟢 |
| Bamboo 2FG 4x5 SE | `00da` | 🟢 |
| Bamboo 2FG 6x8 SE | `00db` | 🟢 |
| Bamboo Connect | `00dd` | 🟢 |
| Bamboo 16FG 4x5 | `00de` | 🟢 |
| Bamboo 16FG 6x8 | `00df` | 🟢 |
| Bamboo One S | `0300` | 🟢 |
| Bamboo One M | `0301` | 🟢 |
| Intuos PT S | `0302` | 🟢 |
| Intuos PT M | `0303` | 🟢 |
| Intuos S | `030e` | 🟢 |
| Intuos Pro S | `0314` | 🟢 |
| Intuos Pro M | `0315` | 🟢 |
| Intuos Pro L | `0317` | 🟢 |
| Intuos P M | `0323` | 🟢 |
| Intuos S 2 | `033b` | 🟢 |
| Intuos PT S 2 | `033c` | 🟢 |
| Intuos P M 2 | `033d` | 🟢 |
| Intuos PT M 2 | `033e` | 🟢 |
| One by Wacom S | `037a` | 🟢 |
| One by Wacom M | `037b` | 🟢 |

### Pen displays

| Product | PID | Comments |
| --- | --- | --- |
| Cintiq Partner | `0003` | 🟢 |
| PL400 | `0030` | 🟢 |
| PL500 | `0031` | 🟢 |
| PL600 | `0032` | 🟢 |
| PL600SX | `0033` | 🟢 |
| PL550 | `0034` | 🟢 |
| PL800 | `0035` | 🟢 |
| PL700 | `0037` | 🟢 |
| PL510 | `0038` | 🟢 |
| DTU710 | `0039` | 🟢 |
| Cintiq 21UX | `003f` | 🟢 |
| DTK2241 | `0057` | 🟢 |
| [DTH2242](#paired-pen-and-touch) | `0059`, `005d` | 🟡 Paired pen and touch: not tested. |
| [Cintiq 22HDT](#paired-pen-and-touch) | `005b`, `005e` | 🟡 Paired pen and touch: not tested. |
| DTF720 | `00c0` | 🟢 |
| DTF720a | `00c2` | 🟢 |
| DTF521 | `00c4` | 🟢 |
| Cintiq 20WSX | `00c5` | 🟢 |
| Cintiq 12WX | `00c6` | 🟢 |
| DTU1931 | `00c7` | 🟢 |
| Cintiq 21UX2 | `00cc` | 🟢 |
| DTU2231 | `00ce` | 🟢 |
| DTU1631 | `00f0` | 🟢 |
| Cintiq 24HD | `00f4` | 🟢 |
| [Cintiq 24HD touch](#paired-pen-and-touch) | `00f6`, `00f8` | 🟡 Paired pen and touch: not tested. |
| Cintiq 22HD | `00fa` | 🟢 |
| DTU1031 | `00fb` | 🟢 |
| Cintiq 13HD | `0304` | 🟢 |
| Cintiq 27QHD | `032a` | 🟢 |
| [Cintiq 27QHD touch](#paired-pen-and-touch) | `032b`, `032c` | 🟡 Paired pen and touch: not tested. |
| DTU1031X | `032f` | 🟢 |
| [Cintiq 13HD touch](#paired-pen-and-touch) | `0333`, `0335` | 🟡 Paired pen and touch: not tested. |
| DTU1141 | `0336` | 🟢 |
| DTK1651 | `0343` | 🟢 |

### Built-in tablet PC digitizers

| Product | PID | Comments |
| --- | --- | --- |
| ISDv4 90 | `0090` | 🟢 |
| ISDv4 93 | `0093` | 🟢 |
| ISDv4 97 | `0097` | 🟢 |
| ISDv4 9A | `009a` | 🟢 |
| ISDv4 9F | `009f` | 🟢 |
| ISDv4 E2 | `00e2` | 🟢 |
| ISDv4 E3 | `00e3` | 🟢 |
| ISDv4 E5 | `00e5` | 🟢 |
| ISDv4 E6 | `00e6` | 🟢 |
| ISDv4 EC | `00ec` | 🟢 |
| ISDv4 ED | `00ed` | 🟢 |
| ISDv4 EF | `00ef` | 🟢 |
| ISDv4 100 | `0100` | 🟢 |
| ISDv4 101 | `0101` | 🟢 |
| ISDv4 10D | `010d` | 🟢 |
| ISDv4 10E | `010e` | 🟢 |
| ISDv4 10F | `010f` | 🟢 |
| ISDv4 116 | `0116` | 🟢 |
| ISDv4 12C | `012c` | 🟢 |
| [ISDv5 307](#paired-pen-and-touch) | `0307` | 🟡 Paired pen and touch: not tested. |
| [ISDv5 309](#paired-pen-and-touch) | `0309` | 🟡 Paired pen and touch: not tested. |
| [ISDv5 30A](#paired-pen-and-touch) | `030a` | 🟡 Paired pen and touch: not tested. |
| [ISDv5 30C](#paired-pen-and-touch) | `030c` | 🟡 Paired pen and touch: not tested. |
| [ISDv5 325](#paired-pen-and-touch) | `0325` | 🟡 Paired pen and touch: not tested. |
| [ISDv5 326](#paired-pen-and-touch) | `0326` | 🟡 Paired pen and touch: not tested. |
| ISDv4 4001 | `4001` | 🟢 |
| ISDv4 4004 | `4004` | 🟢 |
| ISDv4 5000 | `5000` | 🟢 |
| ISDv4 5002 | `5002` | 🟢 |

### Other devices

| Product | PID | Comments |
| --- | --- | --- |
| Wireless Receiver | `0084` | 🟢 Only with tablets marked 🟢 on this page. |
| USB Bamboo PAD | `0318` | 🟢 |
| Wireless Bamboo PAD | `0319` | 🟢 |
| Express Key Remote | `0331` | 🟢 |
| Intuos Pro 2 M with Art Pen | `0357` | 🟢 also `Wacom__ArtPen.bpf.c` |
| [Pen displays with pen and touch in one device](#pen-and-touch-in-one-device) | `03c0`, `03c4`, `03cb`, `03d0`, `03ec`, `03f0` | 🟢 Hardware limits: RAM. |
| [Other Wacom USB devices](#other-wacom-usb-devices) | any | 🟢 Except pen and touch mode switching. |

### Bluetooth and built-in non-USB devices

| Product | PID | Comments |
| --- | --- | --- |
| Graphire BT | `0081` | 🔴 Hardware limits: Bluetooth. |
| Intuos4 WL | `00bd` | 🔴 Hardware limits: Bluetooth. |
| Intuos Pro M | `0360` | 🔴 Hardware limits: Bluetooth. |
| Intuos Pro L | `0361` | 🔴 Hardware limits: Bluetooth. |
| Intuos BT S | `0377`, `03c6` | 🔴 Hardware limits: Bluetooth. |
| Intuos BT M | `0379`, `03c8` | 🔴 Hardware limits: Bluetooth. |
| Intuos Pro S | `0393`, `03dd` | 🔴 Hardware limits: Bluetooth. |
| I2C digitizers | any | 🔴 Outside the scope: I2C laptop device. |
| Other Bluetooth devices | any | 🔴 Hardware limits: Bluetooth. |
| PCI digitizers | any | 🔴 Outside the scope: built into a laptop. |

## Notes

<a id="paired-pen-and-touch"></a>

### Pen displays with separate pen and touch devices

- DTH2242: `056a:0059`, `056a:005d`
- Cintiq 22HDT: `056a:005b`, `056a:005e`
- Cintiq 24HD touch: `056a:00f6`, `056a:00f8`
- Cintiq 27QHD touch: `056a:032b`, `056a:032c`
- Cintiq 13HD touch: `056a:0333`, `056a:0335`
- ISDv5: `056a:0307` with `056a:0309`, `056a:030a` with `056a:030c`,
  `056a:0325` with `056a:0326`

Each of these devices shows up as two USB devices, one for the pen and one for
touch, and the Wacom driver links the two. Testing that needs two emulated
devices behind a USB hub, which has not been done yet. Until then the firmware
hands these devices to `hid-generic` instead of the Wacom driver. The Wacom
driver code for them is already in the source.

<a id="pen-and-touch-in-one-device"></a>

### Pen displays with pen and touch in one device

- Cintiq Pro 27: `056a:03c0`
- Cintiq Pro 17: `056a:03c4`
- Wacom One 13: `056a:03cb`
- Cintiq Pro 22: `056a:03d0`
- DTH134: `056a:03ec`
- Movink 13: `056a:03f0`

These displays have pen and touch as two parts of one USB device, and the
Wacom driver handles each part through its generic path. The parts were tested
one at a time. Both parts together need more RAM than RP2040 has, even with a
much smaller usage limit, so a board with more RAM is needed to use both.

<a id="other-wacom-usb-devices"></a>

### Other Wacom USB devices

A Wacom USB device that is not listed above uses the Wacom driver's generic
path, which follows the device's own report descriptor. A device whose
descriptor contains a pen and touch mode switch is rejected, because that
switch links two devices and needs the same USB hub test as the pen displays
above.
