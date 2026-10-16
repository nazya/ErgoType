# Various USB HID Devices

Vendors with only a few devices in Linux share this page.

## Enabling the drivers

A driver is built when its line in [`CMakeLists.txt`](../../../CMakeLists.txt)
and, where listed, its line in
[`hid_compat.h`](../../../usb_host/linux/include/linux/hid_compat.h) are
uncommented:

| Driver | `CMakeLists.txt` | `hid_compat.h` |
| --- | --- | --- |
| `hid-a4tech.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-a4tech.c` | `#define CONFIG_HID_A4TECH 1` |
| `hid-accutouch.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-accutouch.c` | — |
| `hid-alps.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-alps.c` | `#define CONFIG_HID_ALPS 1` |
| `hid-aureal.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-aureal.c` | — |
| `hid-belkin.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-belkin.c` | — |
| `hid-cherry.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cherry.c` | — |
| `hid-chicony.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-chicony.c` | `#define CONFIG_HID_CHICONY 1` |
| `hid-cougar.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cougar.c` | `#define CONFIG_HID_COUGAR 1` |
| `hid-creative-sb0540.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-creative-sb0540.c` | `#define CONFIG_HID_CREATIVE_SB0540 1` |
| `hid-cypress.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-cypress.c` | `#define CONFIG_HID_CYPRESS 1` |
| `hid-elan.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-elan.c` | `#define CONFIG_HID_ELAN 1` |
| `hid-evision.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-evision.c` | `#define CONFIG_HID_EVISION 1` |
| `hid-ezkey.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ezkey.c` | — |
| `hid-gembird.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gembird.c` | — |
| `hid-glorious.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-glorious.c` | — |
| `hid-google-stadiaff.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-google-stadiaff.c` and `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/input/ff-memless.c` | — |
| `hid-gyration.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-gyration.c` | — |
| `hid-holtek-kbd.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-kbd.c` | `#define CONFIG_HID_HOLTEK 1` |
| `hid-holtek-mouse.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-holtek-mouse.c` | `#define CONFIG_HID_HOLTEK 1` |
| `hid-huawei.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-huawei.c` | — |
| `hid-ite.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ite.c` | `#define CONFIG_HID_ITE 1` |
| `hid-kensington.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kensington.c` | `#define CONFIG_HID_KENSINGTON 1` |
| `hid-keytouch.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-keytouch.c` | — |
| `hid-kysona.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-kysona.c` | — |
| `hid-lcpower.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-lcpower.c` | — |
| `hid-letsketch.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-letsketch.c` | — |
| `hid-macally.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-macally.c` | — |
| `hid-maltron.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-maltron.c` | — |
| `hid-monterey.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-monterey.c` | — |
| `hid-nti.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-nti.c` | — |
| `hid-ortek.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-ortek.c` | — |
| `hid-penmount.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-penmount.c` | — |
| `hid-petalynx.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-petalynx.c` | — |
| `hid-primax.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-primax.c` | `#define CONFIG_HID_PRIMAX 1` |
| `hid-prodikeys.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-prodikeys.c` | `#define CONFIG_HID_PRODIKEYS 1` |
| `hid-pxrc.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-pxrc.c` | `#define CONFIG_HID_PXRC 1` |
| `hid-rakk.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-rakk.c` | `#define CONFIG_HID_RAKK 1` |
| `hid-rapoo.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-rapoo.c` | `#define CONFIG_HID_RAPOO 1` |
| `hid-razer.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-razer.c` | `#define CONFIG_HID_RAZER 1` |
| `hid-redragon.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-redragon.c` | — |
| `hid-retrode.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-retrode.c` | — |
| `hid-samsung.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-samsung.c` | — |
| `hid-semitek.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-semitek.c` | — |
| `hid-sigmamicro.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sigmamicro.c` | — |
| `hid-speedlink.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-speedlink.c` | — |
| `hid-sunplus.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-sunplus.c` | — |
| `hid-tivo.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-tivo.c` | — |
| `hid-topre.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topre.c` | `#define CONFIG_HID_TOPRE 1` |
| `hid-topseed.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-topseed.c` | — |
| `hid-twinhan.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-twinhan.c` | — |
| `hid-viewsonic.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-viewsonic.c` | — |
| `hid-vrc2.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-vrc2.c` | — |
| `hid-waltop.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-waltop.c` | — |
| `hid-xinmo.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-xinmo.c` | — |
| `hid-zydacron.c` | `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/hid-zydacron.c` | `#define CONFIG_HID_ZYDACRON 1` |

Rows marked `hid-generic.c` work without these drivers. Rows marked with a
`.bpf.c` file use a HID-BPF program, see
[HID-BPF programs](../README.md#hid-bpf-programs).

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Vendor | Product | VID:PID | Comments |
| --- | --- | --- | --- |
| Any vendor | ChromeOS keyboards with a Vivaldi top row | any, detected from the descriptor | 🟢 `hid-generic.c`<br>Top-row map for userspace (`hid-vivaldi.c`): outside the scope. |
| A4Tech | WCP32PU mouse | `09da:0006` | 🟢 `hid-a4tech.c` |
| A4Tech | X5-005D mouse | `09da:000a` | 🟢 `hid-a4tech.c` |
| A4Tech | RP-649 mouse | `09da:001a` | 🟢 `hid-a4tech.c` |
| A4Tech | NB-95 mouse | `09da:022b` | 🟢 `hid-a4tech.c` |
| Acecad | Flair and 302 tablets | `0460:0004`, `0460:0008` | 🔴 Separate non-HID driver in Linux. |
| ADATA XPG | Wireless gaming mouse and its receiver | `125f:7505`, `125f:7506` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Afatech | AF9016 TV tuner remote receiver | `15a4:9016` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Aiptek | Tablets | `08ca:0001`, `08ca:0010`, `08ca:0020`, `08ca:0021`, `08ca:0022`, `08ca:0023`, `08ca:0024` | 🔴 Separate non-HID driver in Linux. |
| Alcor | Maltron keyboard | `058f:9410` | 🟢 `hid-maltron.c` |
| ALPS | IBM Gamepad | `0433:1101` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ALPS | U1 DualPoint | `044e:120b` | 🟢 `hid-alps.c` |
| ALPS | T4 Buttonless | `044e:120c` | 🟢 `hid-alps.c` |
| ALPS | U1 | `044e:1215` | 🟢 `hid-alps.c` |
| ALPS | U1 Unicorn Legacy | `044e:121e` | 🟢 `hid-alps.c` |
| AMI | Virtual keyboard and mouse (server BMC) | `046b:ff10` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ATEN | KVM switches (UC100KM, CS124U, CS682, CS692, CS1758, 2- and 4-port KVM) | `0557:2004`, `0557:2202`, `0557:2204`, `0557:2205`, `0557:2208`, `0557:2213`, `0557:2220`, `0557:8021` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Aureal | W01RN remote | `0755:2626` | 🟢 `hid-aureal.c` |
| Belkin | Flip KVM | `050d:3201` | 🟢 `hid-belkin.c` |
| BTC | Emprex remotes | `046e:5577`, `046e:5578` | 🟢 `hid-topseed.c` |
| Cherry | [Cymotion keyboards](#cherry-cymotion) | `046a:0023`, `046a:0027` | 🟢 `hid-cherry.c`<br>Hardware limits: RAM. |
| Chicony | Tactical Pad | `04f2:0418` | 🟢 `hid-chicony.c` |
| Chicony | Wireless keyboard and remote | `04f2:0618` | 🟢 `hid-topseed.c` |
| Chicony | PixArt USB optical mouse | `04f2:0939`, `04f2:1053` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Chicony | Wireless keyboard | `04f2:1123`, `04f2:1236` | 🟢 `hid-chicony.c` |
| Chicony | Toshiba WT10A keyboard | `04f2:1408` | 🔴 `hid-topseed.c`<br>Hardware limits: Bluetooth. |
| Chicony | Acer Switch 12 keyboard | `04f2:1421` | 🟢 `hid-chicony.c` |
| Chicony | Multi-touch device | `04f2:b19d` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| CMedia | CM109 USB phone | `0d8c:000e` | 🔴 Outside the scope: phone. |
| CMedia | HS-100B headset chip | `0d8c:0014` | 🟡 `hid-generic.c`<br>Buttons only: the firmware has no audio. Microphone mute button (`hid-cmedia.c`): outside the scope. |
| CMedia | CM6533 audio chip | `0d8c:0022` | 🔴 `hid-cmedia.c`<br>Outside the scope: audio device. |
| CodeMercs | VRC-2 car controller | `07c0:1125` | 🟢 `hid-vrc2.c` |
| Cooler Master | Mouse receiver | `2516:01b7` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Creative Labs | Prodikeys PC-MIDI keyboard | `041e:2801` | 🟢 `hid-prodikeys.c`<br>MIDI: outside the scope. |
| Creative Labs | SB0540 remote | `041e:3100` | 🟢 `hid-creative-sb0540.c` |
| Creative Labs | Sound Blaster Omni Surround 5.1 | `041e:322c` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Cypress | Cypress Mouse | `04b4:0001` | 🟢 `hid-cypress.c` |
| Cypress | Varmilo VA104M | `04b4:07b1` | 🟢 `hid-cypress.c` |
| Cypress | Barcode 3 | `04b4:bca1` | 🟢 `hid-cypress.c` |
| Cypress | Barcode 1 | `04b4:de61` | 🟢 `hid-cypress.c` |
| Cypress | Barcode 2 | `04b4:de64` | 🟢 `hid-cypress.c` |
| Cypress | Barcode 4 | `04b4:ed81` | 🟢 `hid-cypress.c` |
| Dell | PixArt USB optical mouse | `413c:301a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Dell | Pro Wireless keyboard and mouse KM5221W | `413c:4503` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ELAN | SB974D touchpad | `04f3:0400` | 🔴 `hid-elan.c`<br>Outside the scope: I2C laptop device. |
| ELAN | Toshiba Click Mini L9W touchpad | `04f3:0401` | 🔴 `hid-elan.c`<br>Outside the scope: I2C laptop device. |
| ELAN | HP Pavilion X2 10 | `04f3:074d` | 🟢 `hid-elan.c` |
| ELAN | HP X2 10 Cover | `04f3:0755` | 🟢 `hid-elan.c` |
| ELAN | Chromebook touchscreen | `04f3:2f81` | 🔴 `hid-input.c`<br>Outside the scope: I2C laptop device. |
| ELAN | Multitouch device | `04f3:313a` | 🔴 `hid-multitouch.c`<br>Outside the scope: I2C laptop device. |
| ELAN | Multitouch device | `04f3:3148` | 🔴 `hid-multitouch.c`<br>Outside the scope: I2C laptop device. |
| ELAN | Multitouch device | `04f3:32ae` | 🔴 `hid-multitouch.c`<br>Outside the scope: I2C laptop device. |
| ELAN | Other USB devices | `04f3`, any PID | 🟢 `hid-generic.c`, `hid-quirks.c`, `hid-input.c` |
| ELO | Touchscreens | `04e7:0009`, `04e7:0030` | 🔴 `hid-elo.c`<br>[Not ported yet.](#elo-touchscreens) |
| ELO | TS2700 touchscreen | `04e7:0020` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ELO | AccuTouch 2216 touchscreen | `04e7:0050` | 🟢 `hid-accutouch.c` |
| EVision | Telink receiver | `320f:226f` | 🟢 `hid-evision.c` |
| EVision | ICL01 keyboard | `320f:5041` | 🟢 `hid-evision.c` |
| EZKEY | BTC 8193 keyboard | `0518:0002` | 🟢 `hid-ezkey.c` |
| Formosa | IR receiver | `147a:e03e` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| FTDI | Retrode 2 controller adapter | `0403:97c1` | 🟢 `hid-retrode.c` |
| Gembird | JPD DualForce 2 gamepad | `11ff:3331` | 🟢 `hid-gembird.c` |
| Google | Detachable keyboards (Hammer, Staff, Wand, Whiskers, Masterball, Magnemite, Moonball, Don, Eel, Jewel) | `18d1:5022`, `18d1:502b`, `18d1:502d`, `18d1:5030`, `18d1:503c`, `18d1:503d`, `18d1:5044`, `18d1:5050`, `18d1:5057`, `18d1:5061` | 🔴 `hid-google-hammer.c`<br>Outside the scope: tablet keyboard. |
| Google | Stadia controller | `18d1:9400` | 🟢 `hid-google-stadiaff.c`<br>Hardware limits: Bluetooth. |
| Google Fiber | TV remotes | `0058:2000`, `0471:2210` | 🔴 `hid-gfrm.c`<br>Hardware limits: Bluetooth. |
| GoTop | Super Q2, GoGoPen and PenPower tablets | `08f2:007f`, `08f2:00ce`, `08f2:00f4` | 🔴 Separate non-HID driver in Linux. |
| Griffin | PowerMate and SoundKnob | `077d:0410`, `077d:04aa` | 🔴 Separate non-HID driver in Linux. |
| GTCO | CalComp digitizers | `078c:0090`, `078c:0100`, `078c:0101`, `078c:0103`, `078c:0104`, `078c:0105`, `078c:0106`, `078c:0107`, `078c:0108`, `078c:0200`, `078c:0201`, `078c:0202`, `078c:0203`, `078c:0204`, `078c:0205`, `078c:0206`, `078c:0207`, `078c:0300`, `078c:0301`, `078c:0302`, `078c:0303`, `078c:0304`, `078c:0305`, `078c:0306`, `078c:0307`, `078c:0308`, `078c:0309`, `078c:0400`, `078c:0401`, `078c:0402`, `078c:0403`, `078c:0404`, `078c:0405`, `078c:0500`, `078c:0501`, `078c:0502`, `078c:0503`, `078c:0504`, `078c:1000`, `078c:1001`, `078c:1002`, `078c:1003`, `078c:1004`, `078c:1005`, `078c:1006`, `078c:1007` | 🔴 Separate non-HID driver in Linux. |
| Gyration | Remotes | `0c16:0002`, `0c16:0003`, `0c16:0008` | 🟢 `hid-gyration.c` |
| Holtek | Mistel MD770 keyboard | `04d9:0339` | 🟢 `Mistel__MD770.bpf.c` |
| Holtek | On Line Grip gamepad | `1241:5015` | 🟢 `hid-generic.c`<br>Force feedback (`hid-holtekff.c`): outside the scope. |
| Holtek | Tracer Sniper TRM-503, NOVA Gaming Slider X200, Zalman ZM-GM1 | `04d9:a04a` | 🟢 `hid-holtek-mouse.c` |
| Holtek | Gaming keyboard | `04d9:a055` | 🟢 `hid-holtek-kbd.c` |
| Holtek | Sharkoon Drakonia, Perixx MX-2000 | `04d9:a067` | 🟢 `hid-holtek-mouse.c` |
| Holtek | Gaming mouse | `04d9:a070` | 🟢 `hid-holtek-mouse.c` |
| Holtek | LEETGION Hellion | `04d9:a072` | 🟢 `hid-holtek-mouse.c` |
| Holtek | SHARKOON DarkGlider | `04d9:a081` | 🟢 `hid-holtek-mouse.c` |
| Holtek | Keyboard | `04d9:a096` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Holtek | ETEKCITY Scroll T-140 | `04d9:a0c2` | 🟢 `hid-holtek-mouse.c` |
| Holtek | Keyboard | `04d9:a293` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Holtek | LC-Power LC1000 keyboard | `1241:f767` | 🟢 `hid-lcpower.c` |
| HP | OEM optical mice | `03f0:0641`, `03f0:0941`, `03f0:094a`, `03f0:0a4a`, `03f0:0b4a`, `03f0:134a`, `03f0:1f4a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| HP | Elite Presenter | `03f0:464a` | 🔴 `HP__Elite-Presenter.bpf.c`<br>Hardware limits: Bluetooth. |
| Huawei | [CD30 keyboard](#huawei-cd30) | `12d1:109b` | 🟢 `hid-huawei.c`<br>Hardware limits: RAM. |
| ION | iCade | `15e4:0132` | 🔴 `hid-icade.c`<br>Hardware limits: Bluetooth. |
| ITE | ITE8595 keyboard controller | `048d:8595` | 🟢 `hid-ite.c` |
| ITE | TUXEDO Sirius 16 keyboard | `048d:8910` | 🔴 `TUXEDO__Sirius-16-Gen1-and-Gen2.bpf.c`<br>Outside the scope: built into a laptop. |
| ITE | Lenovo Yoga Slim 7x keyboard | `048d:8987` | 🔴 `hid-lenovo.c`<br>Outside the scope: I2C laptop device. |
| Jabra | Headsets and speakerphones | `0b0e`, any PID | 🟡 `hid-generic.c`<br>Buttons only: the firmware has no audio. Jabra's own controls are not hidden as in Linux (`hid-jabra.c`) and may appear as extra buttons. |
| JESS | Redragon Asura keyboard | `0c45:760b` | 🟢 `hid-redragon.c` |
| KBGear | JamStudio tablet | `084e:1001` | 🔴 Separate non-HID driver in Linux. |
| Kensington | Slimblade trackball | `047d:2041` | 🟢 `hid-kensington.c` |
| Keytouch | IEC keyboard | `0926:3333` | 🟢 `hid-keytouch.c` |
| Kysona and VXE | Kysona M600 (receiver) | `3554:f57c` | 🟢 `hid-kysona.c` |
| Kysona and VXE | Kysona M600 (cable) | `3554:f57d` | 🟢 `hid-kysona.c` |
| Kysona and VXE | VXE Dragonfly R1 Pro (receiver) | `3554:f58a` | 🟢 `hid-kysona.c` |
| Kysona and VXE | VXE Dragonfly R1 Pro (cable) | `3554:f58c` | 🟢 `hid-kysona.c` |
| Labtec | Wireless keyboard | `1020:0006` | 🟢 `hid-belkin.c` |
| Laview | Glorious Model I | `22d4:1503` | 🟢 `hid-glorious.c` |
| LetSketch / VSON / Case U | WP9620N | `6161:4d15` | 🟢 `hid-letsketch.c` |
| Monterey | Genius KB-29E keyboard | `0566:3004` | 🟢 `hid-monterey.c` |
| Multiple vendors | PhoenixRC flight simulator adapter | `1781:0898` | 🟢 `hid-pxrc.c` |
| Multiple vendors | raphnet 4nes4snes adapter (old) | `1781:0a9d` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Novatek | Mouse | `0603:1602` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| NTI | USB-Sun keyboard adapter | `0757:0a00` | 🟢 `hid-nti.c` |
| Ortek | PKB-1700 keyboard | `05a4:1700` | 🟢 `hid-ortek.c` |
| Ortek | WKB-2000 keyboard | `05a4:2000` | 🟢 `hid-ortek.c` |
| Ortek | iHome iMac A210S keyboard | `05a4:8003` | 🟢 `hid-ortek.c` |
| PenMount | 1610 and 1640 touchscreens | `14e1:1610`, `14e1:1640` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PenMount | 6000 touchscreen | `14e1:6000` | 🟢 `hid-penmount.c` |
| Petalynx | Maxter remote | `18b1:0037` | 🟢 `hid-petalynx.c` |
| PI Engineering | VEC USB foot pedal | `05f3:00ff` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| PixArt | USB optical mouse | `093a:2510` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Plantronics | Headsets | `047f`, any PID | 🟡 `hid-generic.c`<br>Buttons only: the firmware has no audio. Plantronics' own volume and mute buttons and the filter for doubled presses (`hid-plantronics.c`): outside the scope. |
| Primax | PixArt mouse | `0461:4d0f`, `0461:4d65`, `0461:4e22` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Primax | Mouse | `0461:4d22`, `0461:4e2a` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Primax | Keyboard | `0461:4e05` | 🟢 `hid-primax.c` |
| Primax | Rezel touchpad | `0461:4e72` | 🔴 `hid-rmi.c`<br>Outside the scope: built into a laptop. |
| Prodige | Cordless keyboard | `05af:3062` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Rapoo | 2.4G receiver | `24ae:2015` | 🟢 `hid-rapoo.c` |
| Razer | BlackWidow Ultimate | `1532:010d` | 🟢 `hid-razer.c` |
| Razer | BlackWidow | `1532:010e` | 🟢 `hid-razer.c` |
| Razer | BlackWidow Classic | `1532:011b` | 🟢 `hid-razer.c` |
| Razer | Blade 14 touchpad | `1532:011d` | 🔴 `hid-rmi.c`<br>Outside the scope: built into a laptop. |
| Samsung | IR remote | `0419:0001` | 🔴 `hid-samsung.c`<br>Outside the scope: Linux handles it through HIDDEV, not input. |
| Samsung | Wireless keyboard and mouse | `0419:0600` | 🟢 `hid-samsung.c` |
| Samsung Electronics | Bluetooth keyboards, gamepad and Action Mouse | `04e8:7021`, `04e8:a000`, `04e8:a004`, `04e8:a006`, `04e8:a064` | 🔴 `hid-samsung.c`<br>Hardware limits: Bluetooth. |
| Semico | Airen SlimPlus keyboard | `1a2c:0002` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Semico | USB keyboards | `1a2c:0023`, `1a2c:0027` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Semitek | Keyboard | `1ea7:0907` | 🟢 `hid-semitek.c` |
| Sigma Micro | Keyboard | `1c4f:0002` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Sigma Micro | USB mouse | `1c4f:0034` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Sigma Micro | Keyboard | `1c4f:0059` | 🟢 `hid-sigmamicro.c` |
| Signotec | ViewSonic PD1011 signature pad | `2133:0018` | 🟢 `hid-viewsonic.c` |
| SinoWealth | IOGEAR Kaliber MMOmentum mouse | `258a:0027` | 🟢 `IOGEAR__Kaliber-MMOmentum.bpf.c` |
| SinoWealth | Glorious Model D | `258a:0033` | 🟢 `hid-glorious.c` |
| SinoWealth | Glorious Model O, Model O- | `258a:0036` | 🟢 `hid-glorious.c` |
| SinoWealth | Keyboard | `258a:6a88` | 🟢 `hid-ite.c` |
| Skycable | Wireless presenter | `1223:3f07` | 🟢 `hid-ortek.c` |
| Solid Year (Cougar) | Macally iKey keyboard | `060b:0001` | 🟢 `hid-macally.c` |
| Solid Year (Cougar) | Cougar 500K | `060b:500a` | 🟢 `hid-cougar.c` |
| Solid Year (Cougar) | Cougar 700K | `060b:700a` | 🟢 `hid-cougar.c` |
| SteelSeries | Arctis 1 and Arctis 9 headsets | `1038:12b6`, `1038:12c2` | 🟡 `hid-generic.c`<br>Buttons only: the firmware has no audio. Battery level (`hid-steelseries.c`): outside the scope. |
| Sun | Raritan KVM dongle | `0430:cdab` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Sunplus | Wireless desktop | `04fc:05d8` | 🟢 `hid-sunplus.c` |
| Symbol | Barcode scanners | `05e0:0800`, `05e0:1200`, `05e0:1300` | 🟢 `hid-generic.c`, `hid-quirks.c`, `hid-input.c` |
| Telink | Rakk Dasig X mouse (Bluetooth) | `248a:8266` | 🔴 `hid-rakk.c`<br>Hardware limits: Bluetooth. |
| Telink | Rakk Dasig X mouse | `248a:fb01` (cable), `248a:fa02` (receiver) | 🟢 `hid-rakk.c` |
| TiVo | Slide remote (Bluetooth) | `150a:1200` | 🔴 `hid-tivo.c`<br>Hardware limits: Bluetooth. |
| TiVo | Slide and Slide Pro remotes | `150a:1201`, `150a:1203` | 🟢 `hid-tivo.c` |
| Topre | Realforce R2 (87 keys) | `0853:0146` | 🟢 `hid-topre.c` |
| Topre | Realforce R2 (108 keys) | `0853:0148` | 🟢 `hid-topre.c` |
| Topre | Realforce R3S (87 keys) | `0853:0313` | 🟢 `hid-topre.c` |
| Topseed | RF combo | `1784:0004` | 🟢 `hid-topseed.c` |
| Topseed | Cyberlink remote | `0766:0204` | 🟢 `hid-topseed.c` |
| Turbox | Keyboard | `062a:0201` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Twinhan | IR remote | `6253:0100` | 🟢 `hid-twinhan.c` |
| ViewSonic | PD1011 signature pad | `0543:e621` | 🟢 `hid-viewsonic.c` |
| WALTOP | Slim tablets 5.8" and 12.1" | `172f:0032`, `172f:0034` | 🟢 `hid-waltop.c` |
| WALTOP | Q Pad | `172f:0037` | 🟢 `hid-waltop.c` |
| WALTOP | Tablet | `172f:0038` | 🟢 `hid-waltop.c` |
| WALTOP | Media tablets 14.1" and 10.6" | `172f:0500`, `172f:0501` | 🟢 `hid-waltop.c` |
| WALTOP | Sirius battery-free tablet | `172f:0502` | 🟢 `hid-waltop.c` |
| WALTOP | Batteryless tablet | `172f:0505` | 🟢 `WALTOP__Batteryless-Tablet.bpf.c` |
| X-Tensions | Speedlink VAD Cezanne mouse | `1ae7:9001` | 🟢 `hid-speedlink.c` |
| Xiaomi | Mi Silent Mouse | `2717:5014` | 🔴 `hid-xiaomi.c`<br>Hardware limits: Bluetooth. |
| Xin-Mo | Dual Arcade and THT 2P arcade controllers | `16c0:05e1`, `16c0:75e1` | 🟢 `hid-xinmo.c` |
| Yealink | P1K, P4K and B2K USB phones | `6993:b001` | 🔴 Outside the scope: phone. |
| Zydacron | Remote control | `13ec:0006` | 🟢 `hid-zydacron.c` |

Some devices of these vendors are on other pages:

- [ASUS](asus.md): ASUS AK1D `04f2:1125`, Medion E1239T `048d:ce50`, ASUS
  MD-5110 `062a:5110` and MD-5112 `0c45:5112`.
- [Touchscreens and touchpads](touchscreens.md): Google Rose touchpad
  `18d1:5028`, Topseed Peripad 701 `1784:0016` and the Turbox touchscreen
  `062a:7100`.
- [Game controllers](game-controllers.md): SteelSeries SRW-S1 wheel `1038:1410`.

## Notes

<a id="cherry-cymotion"></a>

### Cherry Cymotion `046a:0023`, `046a:0027`

These keyboards declare their media keys over a range up to `0x3ff`. The keys
the driver maps, `0x301` to `0x303`, are beyond the `0x2a2` the firmware keeps
at `HID_MAX_USAGES=675`, so on RP2040 those keys produce no event. A board with
more RAM can raise the limit.

<a id="elo-touchscreens"></a>

### ELO touchscreens `04e7:0009`, `04e7:0030`

These touchscreens send position, pressure and touch in their own packet
format, which starts with the letter `T`. The Linux driver `hid-elo.c` decodes
these packets and makes the device appear as a touchscreen instead of a mouse.

Controllers with firmware version M (USB `bcdDevice` `0x010d`) also need a
diagnostics command from the computer once a second, as requested by IBM. The
driver sends it, except on newer IBM 4820 monitors, which it recognizes by
their video device on the same USB hub.

The driver is not in the firmware. What remains to port:

1. Import `hid-elo.c` unchanged and add its lines to `CMakeLists.txt` and
   `hid_compat.h`.
2. Connect the driver to what the firmware already has: vendor control
   requests, also used by the Corsair, Roccat Kone and UC-Logic drivers; work
   that repeats every second, as in the Wacom and Kysona drivers; and the
   usual raw-event and input-setup hooks. The driver's own work queue and its
   `use_fw_quirk` module option become fixed settings: one queue created at
   start, and the workaround on, as in Linux by default.
3. Test one emulated touchscreen that sends `T` packets for touch, movement,
   pressure and release, and one with firmware version M. On the second, the
   command must repeat once a second and stop cleanly when the touchscreen is
   unplugged during a command.
4. The IBM 4820 exception looks at other devices on the same USB hub. Like the
   [paired Wacom devices](wacom.md#paired-pen-and-touch), it can be tested
   only with a real USB hub.

<a id="huawei-cd30"></a>

### Huawei CD30 keyboard `12d1:109b`

The driver's corrected descriptor fits `HID_MAX_USAGES=675`, but the keyboard
needs about 18 KiB of RAM while connected and about 5 KiB more while it is
being parsed. On RP2040 that may not fit next to other devices.
