# Linux HID Drivers

This page lists every HID driver and HID-BPF program in the pinned Linux
source `83f1454877cc292b88baf13c829c16ce6937d120` and says whether the firmware
ports it. The device tables say how well each device works; this page answers
only which Linux code exists in the firmware.

**Ported** means the driver's source is in the firmware. It may still be
disabled in the default build; the device pages say how to enable it.

## Drivers

| Linux driver | In the firmware | Devices |
| --- | --- | --- |
| `hid-a4tech` | Ported. | [Various](vendors/various.md) |
| `hid-accutouch` | Ported. | [Various](vendors/various.md) |
| `hid-alps` | Ported. | [Various](vendors/various.md) |
| `hid-apple` | Ported. | [Apple](vendors/apple.md) |
| `hid-appleir` | Ported. | [Apple](vendors/apple.md) |
| `hid-appletb-bl` | Not ported: built into a Mac. | [Apple](vendors/apple.md) |
| `hid-appletb-kbd` | Not ported: built into a Mac. | [Apple](vendors/apple.md) |
| `hid-asus` | Ported. | [ASUS](vendors/asus.md) |
| `hid-aureal` | Ported. | [Various](vendors/various.md) |
| `hid-axff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-belkin` | Ported. | [Various](vendors/various.md) |
| `hid-betopff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-bigbenff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-cherry` | Ported. | [Various](vendors/various.md) |
| `hid-chicony` | Ported. | [Various](vendors/various.md) |
| `hid-cmedia` | Not ported: audio devices. The source is imported but not built. | [Various](vendors/various.md) |
| `hid-corsair` | Ported. | [Corsair](vendors/corsair.md) |
| `hid-corsair-void` | Not ported: headsets. | [Corsair](vendors/corsair.md) |
| `hid-cougar` | Ported. | [Various](vendors/various.md) |
| `hid-cp2112` | Not ported: not an input device. | — |
| `hid-creative-sb0540` | Ported. | [Various](vendors/various.md) |
| `hid-cypress` | Ported. | [Various](vendors/various.md) |
| `hid-dr` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-elan` | Ported. | [Various](vendors/various.md) |
| `hid-elecom` | Ported. | [ELECOM](vendors/elecom.md) |
| `hid-elo` | [Not ported yet.](vendors/various.md#elo-touchscreens) | [Various](vendors/various.md) |
| `hid-emsff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-evision` | Ported. | [Various](vendors/various.md) |
| `hid-ezkey` | Ported. | [Various](vendors/various.md) |
| `hid-ft260` | Not ported: not an input device. | — |
| `hid-gaff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-gembird` | Ported. | [Various](vendors/various.md) |
| `hid-generic` | Ported. | Every page |
| `hid-gfrm` | Not ported: Bluetooth remotes. | [Various](vendors/various.md) |
| `hid-glorious` | Ported. | [Various](vendors/various.md) |
| `hid-goodix-spi` | Not ported: SPI, built into laptops. | — |
| `hid-google-hammer` | Not ported: tablet keyboards. | [Various](vendors/various.md) |
| `hid-google-stadiaff` | Ported. | [Various](vendors/various.md) |
| `hid-gt683r` | Not ported: not an input device. | — |
| `hid-gyration` | Ported. | [Various](vendors/various.md) |
| `hid-haptic` | Ported. | Touchpads with haptic feedback, see [Touchscreens and Touchpads](vendors/touchscreens.md) |
| `hid-holtek-kbd` | Ported. | [Various](vendors/various.md) |
| `hid-holtek-mouse` | Ported. | [Various](vendors/various.md) |
| `hid-holtekff` | Not ported: game controller. | [Various](vendors/various.md) |
| `hid-huawei` | Ported. | [Various](vendors/various.md) |
| `hid-hyperv` | Not ported: virtual machines only. | — |
| `hid-icade` | Ported. | [Various](vendors/various.md) |
| `hid-ite` | Ported. | [Touchscreens and Touchpads](vendors/touchscreens.md), [Various](vendors/various.md) |
| `hid-jabra` | Not ported: headsets. The source is imported but not built. | [Various](vendors/various.md) |
| `hid-kensington` | Ported. | [Various](vendors/various.md) |
| `hid-keytouch` | Ported. | [Various](vendors/various.md) |
| `hid-kye` | Ported. | [Genius (KYE)](vendors/kye.md) |
| `hid-kysona` | Ported. | [Various](vendors/various.md) |
| `hid-lcpower` | Ported. | [Various](vendors/various.md) |
| `hid-led` | Not ported: not an input device. | — |
| `hid-lenovo` | Ported. | [Lenovo and IBM](vendors/lenovo.md), [Various](vendors/various.md) |
| `hid-lenovo-go` | Not ported: game controller. | [Lenovo and IBM](vendors/lenovo.md) |
| `hid-lenovo-go-s` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-letsketch` | Ported. | [Various](vendors/various.md) |
| `hid-lg` | Ported. | [Logitech](vendors/logitech.md) |
| `hid-lg-g15` | Ported. | [Logitech](vendors/logitech.md) |
| `hid-lg2ff` | Not ported: force feedback for Logitech wheels. | [Logitech](vendors/logitech.md) |
| `hid-lg3ff` | Not ported: force feedback for Logitech wheels. | [Logitech](vendors/logitech.md) |
| `hid-lg4ff` | Not ported: force feedback for Logitech wheels. | [Logitech](vendors/logitech.md) |
| `hid-lgff` | Not ported: force feedback for Logitech wheels. | [Logitech](vendors/logitech.md) |
| `hid-logitech-dj` | Ported. | [Logitech](vendors/logitech.md) |
| `hid-logitech-hidpp` | Ported. | [Logitech](vendors/logitech.md) |
| `hid-macally` | Ported. | [Various](vendors/various.md) |
| `hid-magicmouse` | Ported. | [Apple](vendors/apple.md) |
| `hid-maltron` | Ported. | [Various](vendors/various.md) |
| `hid-mcp2200` | Not ported: not an input device. | — |
| `hid-mcp2221` | Not ported: not an input device. | — |
| `hid-megaworld` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-mf` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-microsoft` | Ported. | [Microsoft](vendors/microsoft.md) |
| `hid-monterey` | Ported. | [Various](vendors/various.md) |
| `hid-multitouch` | Ported. | [Apple](vendors/apple.md), [ASUS](vendors/asus.md), [Huion, XP-Pen and UC-Logic](vendors/uclogic.md), [Lenovo and IBM](vendors/lenovo.md), [Logitech](vendors/logitech.md), [Touchscreens and Touchpads](vendors/touchscreens.md), [Various](vendors/various.md) |
| `hid-nintendo` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-nti` | Ported. | [Various](vendors/various.md) |
| `hid-ntrig` | Not ported: built into tablet PCs. | [Touchscreens and Touchpads](vendors/touchscreens.md) |
| `hid-nvidia-shield` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-ortek` | Ported. | [Various](vendors/various.md) |
| `hid-penmount` | Ported. | [Various](vendors/various.md) |
| `hid-petalynx` | Ported. | [Various](vendors/various.md) |
| `hid-picolcd` | Not ported: not an input device. | — |
| `hid-pl` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-plantronics` | Not ported: headsets. The source is imported but not built. | [Various](vendors/various.md) |
| `hid-playstation` | Not ported: game controller. | [Sony](vendors/sony.md) |
| `hid-primax` | Ported. | [Various](vendors/various.md) |
| `hid-prodikeys` | Ported. | [Various](vendors/various.md) |
| `hid-pxrc` | Ported. | [Various](vendors/various.md) |
| `hid-rapoo` | Ported. | [Various](vendors/various.md) |
| `hid-razer` | Ported. | [Various](vendors/various.md) |
| `hid-redragon` | Ported. | [Various](vendors/various.md) |
| `hid-retrode` | Ported. | [Various](vendors/various.md) |
| `hid-rmi` | Not ported: built into laptops. | [Lenovo and IBM](vendors/lenovo.md), [Touchscreens and Touchpads](vendors/touchscreens.md), [Various](vendors/various.md) |
| `hid-roccat-arvo` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-isku` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-kone` | Ported. | [Roccat](vendors/roccat.md) |
| `hid-roccat-koneplus` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-konepure` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-kovaplus` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-lua` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-pyra` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-ryos` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-roccat-savu` | Not ported: configuration only; devices work through `hid-generic.c`. | [Roccat](vendors/roccat.md) |
| `hid-saitek` | Ported. | [Saitek and Mad Catz](vendors/saitek.md) |
| `hid-samsung` | Ported. | [Various](vendors/various.md) |
| `hid-semitek` | Ported. | [Various](vendors/various.md) |
| `hid-sensor-hub` | Not ported: not an input device. | — |
| `hid-sigmamicro` | Ported. | [Various](vendors/various.md) |
| `hid-sjoy` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-sony` | Ported. | [Logitech](vendors/logitech.md), [Saitek and Mad Catz](vendors/saitek.md), [Sony](vendors/sony.md) |
| `hid-speedlink` | Ported. | [Various](vendors/various.md) |
| `hid-steam` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-steelseries` | Not ported: wheel and headsets. | [Game Controllers and Racing Wheels](vendors/game-controllers.md), [Various](vendors/various.md) |
| `hid-sunplus` | Ported. | [Various](vendors/various.md) |
| `hid-thrustmaster` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-tivo` | Ported. | [Various](vendors/various.md) |
| `hid-tmff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-topre` | Ported. | [Various](vendors/various.md) |
| `hid-topseed` | Ported. | [Various](vendors/various.md) |
| `hid-twinhan` | Ported. | [Various](vendors/various.md) |
| `hid-u2fzero` | Not ported: not an input device. | — |
| `hid-uclogic` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `hid-udraw-ps3` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-universal-pidff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-viewsonic` | Ported. | [Various](vendors/various.md) |
| `hid-vivaldi` | Ported. | [Various](vendors/various.md) |
| `hid-vrc2` | Ported. | [Various](vendors/various.md) |
| `hid-waltop` | Ported. | [Various](vendors/various.md) |
| `hid-wiimote` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-winwing` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-xiaomi` | Ported. | [Various](vendors/various.md) |
| `hid-xinmo` | Ported. | [Various](vendors/various.md) |
| `hid-zpff` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `hid-zydacron` | Ported. | [Various](vendors/various.md) |
| `wacom` | Ported. | [Lenovo and IBM](vendors/lenovo.md), [Wacom](vendors/wacom.md) |

## HID-BPF programs

See [HID-BPF programs](README.md#hid-bpf-programs) for how to enable a ported
program.

| HID-BPF program | In the firmware | Devices |
| --- | --- | --- |
| `FR-TEC__Raptor-Mach-2.bpf.c` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `Generic__touchpad.bpf.c` | Not ported: udev property only, no input change. | — |
| `HP__Elite-Presenter.bpf.c` | Not ported: Bluetooth. | [Various](vendors/various.md) |
| `Huion__Dial-2.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__Inspiroy-2-M.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__Inspiroy-2-S.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__Kamvas-Pro-19.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__Kamvas13Gen3.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__Kamvas16Gen3.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__KeydialK20-Bluetooth.bpf.c` | Not ported: Bluetooth. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `Huion__KeydialK20.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `IOGEAR__Kaliber-MMOmentum.bpf.c` | Ported. | [Various](vendors/various.md) |
| `Logitech__SpaceNavigator.bpf.c` | Not ported: 3D mouse. | [Logitech](vendors/logitech.md) |
| `Microsoft__Xbox-Elite-2.bpf.c` | Not ported: Bluetooth. | [Microsoft](vendors/microsoft.md) |
| `Mistel__MD770.bpf.c` | Ported. | [Various](vendors/various.md) |
| `Rapoo__M50-Plus-Silent.bpf.c` | Not ported: `hid-rapoo.c` handles the same receiver. | [Various](vendors/various.md) |
| `TUXEDO__Sirius-16-Gen1-and-Gen2.bpf.c` | Not ported: built into a laptop. | [Various](vendors/various.md) |
| `Thrustmaster__TCA-Yoke-Boeing.bpf.c` | Not ported: game controller. | [Game Controllers and Racing Wheels](vendors/game-controllers.md) |
| `Trust__Philips-SPK6327.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `WALTOP__Batteryless-Tablet.bpf.c` | Ported. | [Various](vendors/various.md) |
| `Wacom__ArtPen.bpf.c` | Ported. | [Wacom](vendors/wacom.md) |
| `XPPen__ACK05.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `XPPen__Artist24.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `XPPen__ArtistPro16Gen2.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `XPPen__Deco01V3.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `XPPen__Deco02.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
| `XPPen__DecoMini4.bpf.c` | Ported. | [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) |
