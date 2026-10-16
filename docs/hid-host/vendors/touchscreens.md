# Touchscreens and Touchpads

## Enabling the driver

The multitouch driver is built when both of these lines are uncommented:

- in [`CMakeLists.txt`](../../../CMakeLists.txt):
  `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-multitouch.c`
- in [`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h):
  `#define CONFIG_HID_MULTITOUCH 1`

Rows marked `hid-generic.c` work without it. Rows marked `hid-ite.c` need the
ITE driver, see [Various vendors](various.md).

## Devices

A touchscreen or touchpad whose report descriptor follows the Windows
multi-touch format works with the multitouch driver without its own row. The
rows below are devices that Linux lists by ID, usually because they need a
correction. Touchscreens and touchpads of vendors that have their own page are
listed there. The Google Whiskers keyboard `18d1:5030` is on the
[Various vendors](various.md) page with the other Google detachable keyboards.

Driver: `hid-multitouch.c` unless a row names another one.

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Product | VID:PID | Comments |
| --- | --- | --- |
| Other Windows-compatible touchscreens and touchpads | any | 🟢 |
| 3M touchscreens (M1968, M2256, M3266) | `0596:0500`, `0596:0502`, `0596:0506` | 🟢 |
| Anton touchpad | `1130:3101` | 🟢 |
| Atmel maXTouch digitizer | `03eb:2118` | 🟢 |
| Baanto MT-190W2 touchscreen | `2453:0100` | 🟢 |
| Cando touchscreens | `2087:0703`, `2087:0a01`, `2087:0f01` | 🟢 |
| Chunghwa touchscreen | `2247:0001` | 🟢 |
| CJTouch touchscreens | `24b8:0020`, `24b8:0040` | 🟢 |
| CVTouch touchscreen | `1ff7:0013` | 🟢 |
| eGalax touch controller | `0eef:0001` | 🟢 |
| eGalax touchscreens | `0eef:480d`, `0eef:480e`, `0eef:7207`, `0eef:720c`, `0eef:7224`, `0eef:722a`, `0eef:725e`, `0eef:7262`, `0eef:726b`, `0eef:72a1`, `0eef:72aa`, `0eef:72c4`, `0eef:72d0`, `0eef:72fa`, `0eef:7302`, `0eef:7349`, `0eef:73f7`, `0eef:a001`, `0eef:c000`, `0eef:c002` | 🟢 |
| Elitegroup touchscreen | `03fc:05d8` | 🟢 |
| ETT TC4UM and TC5UH touchscreens | `0664:0306`, `0664:0309` | 🔴 Separate non-HID driver in Linux. |
| eTurboTouch touchscreens | `22b9:0006`, `22b9:2968` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| FlatFrog Multitouch 3200 | `25b5:0002` | 🟢 |
| FocalTech FTxxxx touchscreen | `10c4:81b9` | 🟢 |
| Fructel Gametel | `25b6:0002` | 🔴 Hardware limits: Bluetooth. |
| General Touch touchscreens | `0dfc:0003`, `0dfc:0100`, `0dfc:0101`, `0dfc:0102`, `0dfc:0106`, `0dfc:010a`, `0dfc:e100` | 🟢 |
| General Touch touchscreens (older models) | `0dfc:0001`, `0dfc:0002`, `0dfc:0004` | 🔴 Separate non-HID driver in Linux. |
| Goodix touchscreens | `27c6:01e8`, `27c6:01e9` | 🔴 Outside the scope: I2C laptop device. |
| GoodTouch touchscreen | `1aad:000f` | 🟢 |
| Google Rose touchpad | `18d1:5028` | 🟢 |
| Hantick touchpad | `0911:5288` | 🔴 Outside the scope: I2C laptop device. |
| Hanvon touchscreen | `22ed:1010` | 🟢 |
| Ideacom IDC6680 touchscreen | `1cb6:6680` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ILITEK touchscreen | `222a:0001` | 🟢 |
| LG Melfas touchscreen | `1fd2:6007` | 🟢 |
| LG touchscreen | `1fd2:7010` | 🔴 Outside the scope: I2C laptop device. |
| N-Trig digitizers (built into tablet PCs) | `1b96:0001`, `1b96:0003`, `1b96:0004`, `1b96:0005`, `1b96:0006`, `1b96:0007`, `1b96:0008`, `1b96:0009`, `1b96:000a`, `1b96:000b`, `1b96:000c`, `1b96:000d`, `1b96:000e`, `1b96:000f`, `1b96:0010`, `1b96:0011`, `1b96:0012`, `1b96:0013`, `1b96:0014` | 🔴 `hid-ntrig.c`<br>Outside the scope: built into a laptop. |
| N-Trig DuoSense | `1b96:1500` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| N-Trig touchscreen | `1b96:1b05` | 🔴 Outside the scope: I2C laptop device. |
| Nexio PTI0750 touchscreen | `1870:0110` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| NextWindow touchscreen | `1926:0003` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Novatek touchscreen | `0603:0600` | 🟢 |
| Panasonic Panaboard UB-T780, UB-T880 | `04da:1044`, `04da:104d` | 🟢 |
| Panjit touchscreens | `134c:0001`, `134c:0002`, `134c:0003`, `134c:0004` | 🔴 Separate non-HID driver in Linux. |
| PixArt optical touchscreens | `093a:8001`, `093a:8002`, `093a:8003` | 🟢 |
| PixArt touchpads | `093a:0255`, `093a:0274` | 🔴 Outside the scope: I2C laptop device. |
| Quanta optical touchscreen | `0408:3001` | 🟢 |
| Quanta optical touchscreens | `0408:3003`, `0408:3008` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| SiS touchscreens | `0457`, any PID | 🟢 |
| SiS touchscreens with quirks | `0457:0817`, `0457:1013`, `0457:1030`, `0457:9200` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Stantum touchscreen | `0483:3261` | 🟢 |
| Topseed Peripad 701 | `1784:0016` | 🟢 |
| Touch International touchscreen | `1e5e:0313` | 🟢 |
| Touchpack RTS touchscreen | `1bfd:1688` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| TPV optical touchscreens | `25aa:8882`, `25aa:8883` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Turbox Mosart touchscreen | `062a:7100` | 🟢 |
| Unitec touchscreens | `227d:0709`, `227d:0a19` | 🟢 |
| VTL touchscreen | `0306:ff3f` | 🟢 |
| Winbond touchscreen | `0416:c168` | 🟢 |
| Wistron optical touchscreen | `0fb8:1109` | 🟢 |
| XAT touchscreen | `2505:0220` | 🟢 |
| Xiroku touchscreens | `1477:1006`, `1477:1007`, `1477:100e`, `1477:1021`, `1477:1022`, `1477:1023`, `1477:1024`, `1477:1025`, `1477:1026` | 🟢 |

### Synaptics · VID `06cb`

| Product | PID | Comments |
| --- | --- | --- |
| USB TouchPad, cPad, TouchStyk and DisplayPad | `0001`, `0002`, `0003`, `0007`, `0008`, `0009`, `0010`, `0013` | 🟢 `hid-generic.c` |
| Touchscreens | `0ac3`, `0af8`, `1ac3`, `1d10` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Dell K12A keyboard | `2819` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Acer Switch 5 and Acer One keyboards | `2968`, `73f4`, `73f5`, `73f6` | 🟢 `hid-ite.c` |
| TouchPad V103 | `5710` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Dell K15A keyboard | `6e21` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Acer Switch 5 touchpad | `81a7` | 🔴 `hid-rmi.c`<br>Outside the scope: built into a laptop. |
| Touchpads | `8323`, `cd7e`, `cddc`, `ce08`, `ce09` | 🔴 `hid-multitouch.c`<br>Outside the scope: I2C laptop device. |
| Other RMI touchpads | any | 🔴 `hid-rmi.c`<br>Outside the scope: built into a laptop. |

On Linux, the Synaptics USB TouchPad family can also use the separate
`synaptics_usb` driver, which is not HID. The firmware uses the HID path, as
Linux does when that driver is not built.
