# USB HID Host Support

Use this matrix to check whether the firmware can act as a host for a wired
USB HID device and which limitations apply. Find the device's
`vvvv:pppp` identifier with `lsusb`, then look up its Vendor ID and Product ID
in the linked tables.

This documentation describes the wired USB HID driver ports available to the
RP2 firmware. It is compared with pinned Linux HID source
`83f1454877cc292b88baf13c829c16ce6937d120`. Devices that Linux supports only
over Bluetooth or I2C are listed too, marked 🔴.
The RP2 parser retains at most 64 fields and 675 usages per field; a driver
being present in source does not override those memory limits.
The allocation model and boundary behavior are documented in
[RP2 USB HID host memory and capacity](rp2.md).

## Host transport

The RP2040 and RP2350 builds use TinyUSB over Pico-PIO-USB on the board's
single PIO D+/D- root port. Current hardware evidence and exact static SRAM
measurements come from RP2040; the shared parser policy, reduced endpoint
capacity, and remaining memory-tuning options are documented in
[RP2 USB HID host memory and capacity](rp2.md).

The active path is Pico-PIO-USB controller and PHY, TinyUSB host/class glue,
the Linux-derived HID core and drivers, then the firmware input boundary. The
RP2 configuration reduces the shared endpoint pool from 32 to 8 records and
the physical PIO root-port count from two to one. The current RP2040 map
measures savings of 4,512 and 48 bytes respectively. The root-port reduction
does not remove USB-hub support.

An ESP32-P4 platform build exists, but its HID-host transport is not yet
integrated or hardware-qualified. The support tables therefore describe the
current RP2 host path.

## Device lifecycle

```text
USB attach -> TinyUSB -> lifecycle task -> descriptors and SET_IDLE
           -> Linux HID parser and driver -> input devices -> KeyD
```

Each HID interface of a composite USB device is probed independently. An
unsupported interface can be ignored without preventing supported keyboard or
mouse interfaces on the same device from working. CDC interfaces are not part
of the HID probe path.

TinyUSB callbacks only publish bounded state changes and wake the owning task.
Descriptor reads, Linux driver probe, driver-specific control requests,
delayed initialization, and final destruction run in task context. Incoming
interrupt reports pass through the Linux HID parser and input mapping before
they reach the firmware input boundary.

On removal, new reports stop first. The lifecycle task then cancels and drains
the interface's requests, reports, work, and timers before unregistering its
input devices and releasing the HID graph. A fast reconnect receives a new
device generation, so a late completion from the old attachment cannot be
delivered to the new one.

OUTPUT reports use interrupt OUT when the interface provides it and otherwise
use EP0. FEATURE reports and raw control requests use EP0. The exact request,
cancellation, enumeration, and ownership contracts are documented in
[`tinyusb-host-port.md`](../../usb_host/tinyusb-host-port.md) and
[`linux-hid-port-contract.md`](../../usb_host/linux-hid-port-contract.md).

## What the HID host supports

ErgoType is a wired remapper for devices that you plug into it: keyboards,
mice, trackballs, presenters, touchpads, drawing tablets and pads, dials, and
their USB receivers. A gaming label does not exclude a keyboard or mouse.

The device tables mark other devices `Outside the scope`. This section explains
those decisions.

### Game controllers

Gamepads, racing wheels, joysticks, flight controls and handheld consoles are
not what this firmware remaps. Their Linux drivers are not ported, including
HID-BPF programs written only for controllers, such as those for the FR-TEC
Raptor Mach 2, the Thrustmaster TCA Yoke Boeing and the Xbox Elite Series 2
over Bluetooth.

Many simple gamepads still work through `hid-generic.c`, because their Linux
driver only adds force feedback. A controller whose input itself depends on
its Linux driver, for example Nintendo, Steam or Moza devices, does not work.
Force feedback is not excluded in general: haptic feedback on a touchpad is
supported.

### 3D mice

3D mice, such as the 3Dconnexion SpaceNavigator and SpaceTraveller, have six
axes: pushing the cap in three directions and tilting or twisting it. KeyD
uses only the two axes of an ordinary mouse or the position of a touchscreen,
so the firmware has no use for the other four. Linux also corrects these
devices so that the axes report the position of the cap, and KeyD would then
move the cursor to a point on the screen. The Linux fixes in `hid-lg.c` and
`Logitech__SpaceNavigator.bpf.c` are therefore not ported. If KeyD gains a
mapping for six axes, this decision can change.

### Headsets and other audio devices

USB headsets and speakerphones, wired or with a USB wireless receiver, also
have a HID part, mostly call and volume buttons. These buttons work through
`hid-generic.c`, but the firmware has no audio, so the sound does not. The
tables mark such devices 🟡 `Buttons only: the firmware has no audio.`

The Linux drivers for Corsair Void, Jabra, Plantronics, SteelSeries Arctis,
Logitech G935 and CMedia audio chips are not used. They mostly add battery
level, sidetone and similar settings; the Jabra and Plantronics drivers also
hide or remap the vendor's own controls, and the tables say what differs. The
Jabra driver would also claim every Jabra headset and speakerphone. The CMedia
CM6533 chip reports only whether headphones are plugged in, so it stays
outside the scope.

Bluetooth headsets do not use these drivers at all.

### Bluetooth

The RP2 board has no Bluetooth radio, so devices or modes that Linux supports
only over Bluetooth are marked `Hardware limits: Bluetooth`. They stay in the
tables in case a Bluetooth host is added later.

### Devices built into laptops and tablets

Internal keyboards and touchpads, Touch Bars, tablet keyboard covers and docks
often use USB inside the device, but they cannot be plugged into the firmware.
Examples are MacBook keyboards and trackpads, ThinkPad tablet keyboards, ASUS
ROG laptop keyboards and ROG Flow covers. I2C devices are always internal.

All known USB devices that use the Synaptics RMI protocol are internal
touchpads or touchpads in detachable covers and docks, such as those of the
Razer Blade 14, Lenovo X1 Tablet and Acer Switch 5. The RMI driver is
therefore not ported. If an external USB device with RMI appears, this
decision can change.

### Configuration interfaces

Some Linux drivers only let a vendor tool change settings stored in the device,
such as profiles, DPI levels, lights or macros, or publish extra data for
programs on the computer through sysfs, hidraw, HIDDEV or udev properties.
Input does not depend on them, so such devices work through `hid-generic.c`
and the tables say what is missing. A HID-BPF program that only sets a udev
property and changes no input is not ported.

### Rare and obsolete devices

Logitech 27 MHz receivers and old Bluetooth receivers in HID proxy mode are
outside the scope. Some other old device families have ported drivers that
are simply not enabled by default to save memory. For example, the Ortek
keyboards and presenter can be enabled when needed.

### Devices that are not input devices

Linux HID also handles laboratory instruments, radios, UPS units, cameras,
card readers, USB bridges, LED devices, and MIDI and audio-only devices. The
tables do not list them.

## How to read the tables

Each vendor page lists every input device that the pinned Linux HID drivers
know for that vendor. The support symbol shows how much of the Linux driver
support for the device is ported, not whether the driver is enabled in the
default firmware image. Each page starts with the lines to uncomment for its
drivers.

| Symbol | Meaning |
| --- | --- |
| 🟢 | Full: the Linux driver support for this device is ported. Unless Comments say otherwise, it works as in Linux. |
| 🟡 | Partial: only part of the device works, such as the buttons of a headset, or part of its support is unfinished or relies on a path that was not tested, such as a USB hub. Comments say which part. |
| 🔴 | Not supported: the device does not work with this firmware. Comments say why. |

Comments use a few fixed phrases:

- `Hardware limits: Bluetooth.` Linux supports the device, or its wireless
  mode, over Bluetooth, and the RP2 board has no Bluetooth. On a 🟢 row the
  device works over its USB cable.
- `Hardware limits: RAM.` The device's reports do not fit into RP2040 memory,
  see [memory limits](#memory-limits). A linked note gives details.
- `Outside the scope: <reason>.` The device does not fit the purpose of the
  firmware, for example a game controller or a tablet keyboard cover. See
  [What the HID host supports](#what-the-hid-host-supports) for the reasons.
- `Buttons only: the firmware has no audio.` The buttons of a headset or
  speakerphone work through `hid-generic.c`, but its sound does not. See
  [Headsets and other audio devices](#headsets-and-other-audio-devices).
- `Not ported yet.` The device is in scope, but its Linux fix has not been
  ported. The phrase links to a note that explains what is missing and what
  remains to port.

The tables cover delivery of input events to KeyD. What KeyD does with those
events is separate and does not change the status.

Support is checked on an RP2040 host with USB emulators. A device that uses
the same driver code path as a tested device is not tested separately.

### Memory limits

To fit into RP2040 memory, the firmware keeps at most 64 fields per report and
675 usages per field; Linux keeps 256 and 12,288. Ordinary keyboards and mice
fit. Devices whose reports need more are marked `Hardware limits: RAM.` and
need a board with more RAM. [RP2 USB HID host memory and capacity](rp2.md) has
the technical details.

## Generic USB HID

A standards-compliant USB keyboard, mouse, Consumer Control device, System
Control device, or ordinary descriptor-driven composite device normally uses
the active `hid-generic` path and therefore does not need an explicit VID:PID
row. An absent row is not automatically an unsupported device.

An explicit row is useful when Linux applies a descriptor correction, vendor
protocol, additional input mapping, receiver lifecycle, delayed work, or
another device-specific behavior.

## Enabling drivers

The planned default firmware builds only `hid-generic.c`; every other driver
is switched on for a particular build. Until that default is in place, the
current build still includes more drivers. [`CMakeLists.txt`](../../CMakeLists.txt)
and [`hid_compat.h`](../../usb_host/linux/include/linux/hid_compat.h) show
exactly what a build contains.

To enable a driver, uncomment its source line in `CMakeLists.txt` and, where
the vendor page lists one, its `CONFIG_HID_*` line in `hid_compat.h`. Both are
needed: the source line alone can leave the driver unused, and the
`CONFIG_HID_*` line alone adds no code. Some devices need an extra option, and
their vendor page names it. After rebuilding, check that the firmware still
fits into memory, see [memory limits](#memory-limits).

## HID-BPF programs

Some devices need a small upstream HID-BPF program rather than a driver. Their
rows name the program file, for example `Huion__Dial-2.bpf.c`. The firmware
runs these programs natively, without a BPF virtual machine, but this support
is switched off in the default build. To include a program:

1. In [`CMakeLists.txt`](../../CMakeLists.txt), uncomment
   `${CMAKE_CURRENT_LIST_DIR}/usb_host/hid_bpf_static.c` and the line of the
   program, for example
   `${CMAKE_CURRENT_LIST_DIR}/usb_host/linux/drivers/hid/bpf/progs/Huion__Dial-2.bpf.c`.
2. In [`hid.h`](../../usb_host/linux/include/linux/hid.h), follow the comment
   above `hid_bpf_connect_device()`: uncomment the `hid_bpf_static_device`
   declaration, the `bpf_static` member of `struct hid_device` and the hook
   declarations, then comment out the matching no-op stubs.

## Device tables

- [Apple](vendors/apple.md) — Magic Keyboard, Magic Mouse, Magic Trackpad,
  Aluminum keyboards, and built-in Mac devices.
- [ASUS](vendors/asus.md) — ROG Claymore II, XG Mobile, Transformer keyboards,
  and built-in laptop devices.
- [Corsair](vendors/corsair.md) — keyboards, mice, and Void headsets.
- [ELECOM](vendors/elecom.md) — mice.
- [Game controllers and racing wheels](vendors/game-controllers.md) —
  gamepads, wheels and joysticks of vendors without their own page.
- [Genius (KYE)](vendors/kye.md) — mice, keyboards, and EasyPen and MousePen
  tablets.
- [Huion, XP-Pen and UC-Logic](vendors/uclogic.md) — drawing tablets, pen
  displays, and the Trust and UGTIZER tablets that use the same driver.
- [Lenovo and IBM](vendors/lenovo.md) — ThinkPad keyboards, ScrollPoint mice,
  and other Lenovo devices.
- [Logitech](vendors/logitech.md) — receivers, mice, keyboards, G-series
  keyboards, wheels, and Bluetooth devices.
- [Microsoft](vendors/microsoft.md) — keyboards, mice, presenters, Surface
  covers, and Bluetooth controllers.
- [Roccat](vendors/roccat.md) — mice and keyboards.
- [Saitek and Mad Catz](vendors/saitek.md) — R.A.T. and M.M.O. mice, joysticks,
  and gamepads.
- [Sony](vendors/sony.md) — VAIO mice, PlayStation controllers, and rhythm game
  instruments.
- [Touchscreens and touchpads](vendors/touchscreens.md) — multi-touch
  screens and touchpads of many vendors, and Synaptics.
- [Wacom](vendors/wacom.md) — pen tablets, pen displays, built-in tablet PC
  digitizers, ExpressKey Remote, and the wireless receiver.
- [Various vendors](vendors/various.md) — about 70 vendors with only a few
  devices each.

[Linux HID drivers](linux-drivers.md) lists every Linux HID driver and HID-BPF
program and whether the firmware ports it.

## Diagnostics

[HID host diagnostics](diagnostics.md) explains the host's runtime messages and
the first checks for enumeration, probe, report, and teardown failures.
