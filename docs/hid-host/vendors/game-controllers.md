# Game Controllers and Racing Wheels

Game controllers are outside the purpose of this firmware. This page lists the
game controllers that Linux handles with drivers or fixes which the firmware
does not port, so that none of them is missing from the tables. Game
controllers of vendors that have their own page, such as Logitech, Microsoft,
Saitek or Sony, are listed there.

Many simple gamepads still work through `hid-generic.c`, because their Linux
driver only adds force feedback. Those rows are 🟢 with a note.

## Devices

**Support:** 🟢 Full · 🟡 Partial · 🔴 Not supported

| Vendor | Product | VID:PID | Comments |
| --- | --- | --- | --- |
| 8BitDo | Pro 3 controller | `2dc8:6009` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Aashima | Gamepad and Predator | `06d6:0025`, `06d6:0026` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| ACRUX | Gamepads | `1a34:0802`, `1a34:f705` | 🟢 `hid-generic.c`<br>Force feedback (`hid-axff.c`): outside the scope. |
| Asetek | Invicta, Forte, La Prima and Tony Kanaan wheel bases | `2433:f300`, `2433:f301`, `2433:f303`, `2433:f306` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| Betop | 2185 gamepads | `11c2:2208`, `11c0:5506`, `20bc:5500`, `8380:1850` | 🟢 `hid-generic.c`<br>Force feedback (`hid-betopff.c`): outside the scope. |
| BigBen | PS3 mini gamepad | `146b:0902` | 🔴 `hid-bigbenff.c`<br>Outside the scope: game controller. |
| Cammus | C5 and C12 wheel bases | `3416:0301`, `3416:0302` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| CH Products | Joysticks, yokes, throttle and pedals | `068e:001c`, `068e:0051`, `068e:00d3`, `068e:00f1`, `068e:00f2`, `068e:00f3`, `068e:00f4`, `068e:00ff` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| CHIC | Gamepad | `05fe:0014` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Dracal | raphnet 2nes2snes and 4nes4snes adapters | `289b:0002`, `289b:0003` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| DragonRise | Gamepads (including Redragon Seymur 2) | `0079:0006`, `0079:0011` | 🔴 `hid-dr.c`<br>Outside the scope: game controller. |
| DragonRise | Wii U GameCube adapter | `0079:1800` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| DragonRise | PS3 adapter, DolphinBar and GameCube adapters | `0079:1801`, `0079:1803`, `0079:1843`, `0079:1844`, `0079:1846` | 🟢 `hid-generic.c`, `hid-quirks.c`<br>Force feedback (`hid-mf.c`): outside the scope. |
| EMS | Trio Linker Plus II | `2006:0118` | 🟢 `hid-generic.c`, `hid-quirks.c`<br>Force feedback (`hid-emsff.c`): outside the scope. |
| Essential Reality | P5 glove | `0d7f:0100` | 🔴 Ignored by Linux HID. |
| FFBeast | Joystick, rudder and wheel | `045b:58f9`, `045b:5968`, `045b:59d7` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| FR-TEC | Raptor Mach 2 | `11c0:5606` | 🔴 `FR-TEC__Raptor-Mach-2.bpf.c`<br>Outside the scope: game controller. |
| GameRon | Dual PSX and PCS adapters | `0810:0001`, `0810:0002` | 🟢 `hid-generic.c`<br>Force feedback (`hid-pl.c`): outside the scope. |
| Gamevice | GV186 and Kishi controllers | `27f8:0bbe`, `27f8:0bbf` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| GreenAsia | Gamepad | `0e8f:0003` | 🟢 `hid-generic.c`<br>Force feedback (`hid-pl.c`): outside the scope. |
| GreenAsia | Gamepad | `0e8f:0012` | 🟢 `hid-generic.c`<br>Force feedback (`hid-gaff.c`): outside the scope. |
| GreenAsia | Dual Saturn adapter and dual USB joypad | `0e8f:3010`, `0e8f:3013` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Happ | UGCI driving, flying and fighting controllers | `078b:0010`, `078b:0020`, `078b:0030` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Innomedia | Innex Genesis and Atari adapter | `1292:4745` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| JESS2 | Color Rumble Pad | `0f30:0111` | 🟢 `hid-generic.c`<br>Force feedback (`hid-pl.c`): outside the scope. |
| Labtec | ODDOR handbrake | `1020:8888` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Lite Star | PXN V10, V12, V12 Lite and GT987 wheels | `11ff:1112`, `11ff:1211`, `11ff:1212`, `11ff:2141`, `11ff:3245` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| MCS | Gamepad block | `16d0:0bcc` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| MegaWorld | Gamepad | `07b5:0312` | 🟢 `hid-generic.c`<br>Force feedback (`hid-megaworld.c`): outside the scope. |
| Mojo | Retro adapter | `8282:3201` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Moza | R3, R5, R9, R12, R16 and R21 wheel bases | `346e:0000`, `346e:0002`, `346e:0004`, `346e:0005`, `346e:0006`, `346e:0010`, `346e:0012`, `346e:0014`, `346e:0015`, `346e:0016` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| Natsu | Gamepad | `08b7:0001` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| NEC | USB game pad | `073e:0301` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Nintendo | Wii Remote, Wii Remote Plus | `057e:0306`, `057e:0330` | 🔴 `hid-wiimote-core.c`<br>Hardware limits: Bluetooth. |
| Nintendo | Joy-Con (L), Joy-Con (R) | `057e:2006`, `057e:2007` | 🔴 `hid-nintendo.c`<br>Hardware limits: Bluetooth. |
| Nintendo | Switch Pro Controller | `057e:2009` | 🔴 `hid-nintendo.c`<br>Outside the scope: game controller. |
| Nintendo | Joy-Con charging grip | `057e:200e` | 🔴 `hid-nintendo.c`<br>Outside the scope: game controller. |
| Nintendo | SNES, N64 and Genesis controllers for Switch | `057e:2017`, `057e:2019`, `057e:201e` | 🔴 `hid-nintendo.c`<br>Outside the scope: game controller. |
| NVIDIA | Shield Thunderstrike controller | `0955:7214` | 🔴 `hid-nvidia-shield.c`<br>Outside the scope: game controller. |
| PlayDotCom | EMS USB II | `0b43:0003` | 🟢 `hid-generic.c`<br>Force feedback (`hid-sjoy.c`): outside the scope. |
| QinHeng | Lenovo Legion Go S controller | `1a86:e310`, `1a86:e311` | 🔴 `hid-lenovo-go-s.c`<br>Outside the scope: game controller. |
| RetroUSB | SNES RetroPad and RetroPort | `f000:0003`, `f000:00f1` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| SteelSeries | SRW-S1 wheel | `1038:1410` | 🔴 `hid-steelseries.c`<br>Outside the scope: game controller. |
| THQ | uDraw tablet (PS3) | `20d6:cb17` | 🔴 `hid-udraw-ps3.c`<br>Outside the scope: game controller. |
| Thrustmaster | TCA Yoke Boeing | `044f:0409` | 🔴 `Thrustmaster__TCA-Yoke-Boeing.bpf.c`<br>Outside the scope: game controller. |
| Thrustmaster | Force-feedback gamepads and wheels | `044f:b300`, `044f:b304`, `044f:b320`, `044f:b323`, `044f:b324`, `044f:b605`, `044f:b651`, `044f:b653`, `044f:b654`, `044f:b65a` | 🟢 `hid-generic.c`<br>Force feedback (`hid-tmff.c`): outside the scope. |
| Thrustmaster | T-series wheels (start-up mode) | `044f:b65d` | 🔴 `hid-thrustmaster.c`<br>Outside the scope: game controller. |
| Topmax | Cobra Pad | `0663:0103` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Valve | Steam Controller | `28de:1102`, `28de:1142` (wireless) | 🔴 `hid-steam.c`<br>Outside the scope: game controller. |
| Valve | Steam Deck | `28de:1205` | 🔴 `hid-steam.c`<br>Outside the scope: game controller. |
| VRS | DirectForce Pro wheel base | `0483:a355` | 🔴 `hid-universal-pidff.c`<br>Outside the scope: game controller. |
| VRS | R295 wheel | `0483:a44c` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| WinWing | TGRIP-15E, 15EX, 16EX and 18 grips | `4098:bd64`, `4098:bd65`, `4098:be62`, `4098:be68` | 🔴 `hid-winwing.c`<br>Outside the scope: game controller. |
| WiseGroup | SmartJoy Plus, Dual USB joypad and Super Joy Box 3 | `0925:0005`, `0925:8866`, `0925:8888` | 🟢 `hid-generic.c`<br>Force feedback (`hid-sjoy.c`): outside the scope. |
| WiseGroup | Quad USB joypad | `0925:8800` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| WiseGroup | Super Joy Box 3 Pro, Super Dual Box Pro and Super Joy Box 5 Pro | `6666:8801`, `6666:8802`, `6666:8804` | 🟢 `hid-generic.c`<br>Force feedback (`hid-sjoy.c`): outside the scope. |
| WiseGroup | SmartJoy Dual Plus | `6677:8802` | 🟢 `hid-generic.c`, `hid-quirks.c` |
| Zeroplus | Gamepads | `0c12:0005`, `0c12:0030` | 🟢 `hid-generic.c`<br>Force feedback (`hid-zpff.c`): outside the scope. |
