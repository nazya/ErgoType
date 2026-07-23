# Deferred HID Drivers

This note is about the current task-owned HID host architecture. It is not a
list of every "large" driver. The important boundary is whether the upstream
driver's transport and subsystem dependencies are implemented by the firmware
port.

## Current Safe Boundary

The current build may include drivers that only need one of these patterns:

- report descriptor fixup
- input mapping or input mapped hooks
- simple probe that does `hid_parse()` and `hid_hw_start()`
- `hid_hw_request()` followed by `hid_hw_wait()`, including returned feature
  data needed during probe
- synchronous-looking HID raw GET/SET and interrupt-output calls, because the
  wait happens in a firmware task while TinyUSB remains free to run
- upstream-shaped `.request()` OUTPUT routing: interrupt OUT when the
  interface exposes it, EP0 SET_REPORT otherwise

These drivers can run from the current pre-probe plus lifecycle-task probe path
because USB descriptor/string metadata is available before `hid_ignore()` and
driver probe. TinyUSB completions only publish bounded completion records; the
serialized async task resumes the waiting task-side continuation.

Razer is in this bucket: the tested macro-enable SET_REPORT uses upstream
`usbhid_set_raw_report()` over the generic async USB owner, and USB strings are
available before probe.

Multitouch and the standard HID Haptics Page helper are also in this bucket.
The lifecycle task completes the multitouch feature GET_REPORT before probe
continues, while haptic output uses the same asynchronous report owner. Both
transport/driver paths have emulator/hardware coverage, including unplug and
re-enumeration. That fixture's separate boot-mouse interface supplies its
visible cursor movement: it does not certify the downstream KeyD consumer for
an MT-only touchpad. Linux already publishes the correct Type-B stream and
legacy `ABS_X/Y` pointer emulation; the remaining stale-ABS fix, SYN framing,
and absolute-to-relative product policy belong after the Linux evdev boundary.

The retained narrow gaming path is Google Stadia `FF_RUMBLE` through
`hid-google-stadiaff.c` and `ff-memless.c`. Both sources are currently unlinked
because firmware has no product client for rumble. This is a product boundary,
not an unresolved driver bug: the driver's upstream spinlock and the two
ff-memless input `event_lock` scopes are mapped to task-context
priority-inheritance mutexes, and a targeted 2026-07-22 run passed upload,
timer stop, same-ID replay, running-work unplug, remove, and reconnect. Enable
the two CMake entries together when such a client is selected; the ordinary
layout-change `FF_HAPTIC` hook is a different path.

Regular FEATURE and raw requests stay on EP0; `.output_report()` remains the
interrupt-only entry point and returns `-ENOSYS` without an OUT endpoint.

## Unimplemented Transport Boundary

HID probe and remove no longer execute in TinyUSB callbacks, so waiting on an
implemented HID request does not create the original callback deadlock. Do not
link drivers whose probe/init path requires transport primitives or subsystem
ownership that the port still does not provide.

Current unsupported patterns include:

- `usb_submit_urb()` and request-specific kill/resubmit ownership
- synchronous interrupt-IN messages
- `usb_string()` or explicit string-descriptor reads when the string is required
  beyond the pre-probe product/manufacturer/serial snapshots
- multi-interface protocols that require a complete USB-core ownership model,
  sibling binding, or `usb_get_intfdata()` coordination
- protocols whose init burst exceeds the logical usbhid FIFO's shared 4096-byte
  node budget (CTRL/OUT ordering and more-than-nine-report bursts are supported;
  validate a real device before raising this firmware memory policy)

Task-context `usb_control_msg()` and interrupt-OUT `usb_interrupt_msg()` have
explicit submit, completion, timeout, per-interface cancellation, and
device-generation semantics in the TinyUSB transport owner. Their fixed pool
orders generic interrupt OUT per device endpoint, so one NAKing device does not
hold unrelated endpoints behind a global FIFO. The unsupported helpers remain
disabled instead of pretending success.

## Other Deferred Reasons

Some drivers should also stay out for reasons that are separate from callback
synchronous waits:

- sysfs
- hidraw fd/ioctl semantics
- LED class
- backlight
- power_supply
- hwrng
- force-feedback PID state
- framebuffer
- ALSA/rawmidi

`hid-cherry.c` is also deferred, but for the bounded parser rather than a
missing transport API. Real Cymotion descriptors expand an array to selector
`0x03ff`, and the driver's useful `0x301..0x303` mappings sit above this
firmware's `HID_MAX_USAGES=675` (`0x2a2`) cap. A synthetic descriptor with
three explicit usages would only test the hook while falsely claiming support
for the real hardware; increasing the cap would cost roughly 3 KiB per such
report and is not justified for this legacy keyboard.

`hid-huawei.c` is transport-safe but remains deferred until a dedicated heap
stress pass. Its CD30 replacement descriptor contains a Consumer array over
usages `0x0000..0x023c`: it fits the 675-usage policy, but one matched device
would still allocate roughly 18 KiB of persistent selector state plus about
5 KiB of transient parser storage.

`hid-jabra.c` and `hid-ortek.c` are technically compatible with the current
hooks, but are outside the selected allowlist: Jabra broadly matches every USB
headset/speakerphone from that vendor, while the supported Ortek family is a
legacy keyboard/trackpad/presenter set. Keep both source files available for a
specific hardware request instead of paying static registry RAM by default.

Those are missing Linux subsystem ownership layers, not the same problem as
blocking inside TinyUSB callbacks.

## Planned HIDRAW and Logitech HID++ Boundary

HIDRAW is a Linux client interface, not a device protocol. Ordinary keyboard
and mouse input does not need it, and Logitech HID++ kernel drivers can use
`.raw_event` plus `hid_hw_raw_request()` without a Linux `/dev/hidraw` file.
Some other useful drivers and a future WebHID/raw proxy do expect the HIDRAW
lifecycle, so the planned firmware boundary is deliberately smaller than the
Linux character-device ABI:

- retain upstream `hidraw_connect()` / `hidraw_disconnect()` and
  `HID_CLAIMED_HIDRAW` ownership;
- give each object generation-safe disconnect/reconnect lifetime;
- deliver raw input to an optional internal subscriber through a bounded
  queue, with an explicit drop counter and no producer wait when no subscriber
  exists;
- route GET, SET, and OUTPUT through the existing asynchronous HID transport;
- do not add `/dev`, file descriptors, `read()`, `ioctl()`, `poll()`, or VFS
  emulation until a real external proxy client needs them.

After that compatibility layer, port Logitech support in this order:

1. Direct USB HID++ 1.0/2.0 request/response, including timeout and
   generation-safe cancellation when a reply is pending during disconnect.
2. Battery state, identity/serial/version, high-resolution wheel, extra
   buttons, and touchpad raw XY.
3. `hid-logitech-dj` receiver ownership: first one paired mouse, then keyboard
   plus mouse, then per-child disconnect/reconnect, then several receiver
   slots with measured heap use.

The first device matrix should cover M560 or M705, K400 or K750, T650, a
combined Unifying keyboard/mouse receiver, and a direct-USB MX Vertical.
Logitech Bolt must be treated as a separate protocol/device check rather than
assumed from Unifying/DJ coverage. Bluetooth-only models remain outside this
USB transport until a Bluetooth HID backend exists. Wacom and simple
LetSketch-like tablets are the next useful family after Logitech because they
exercise the same raw-report and multitouch boundaries.

`CONFIG_HID_HOLTEK` is compound upstream. Firmware links its keyboard and mouse
descriptor-fixup drivers, but not the separate On Line Grip game-controller
driver or optional rumble support. The controller ID is therefore deliberately
left available to generic HID rather than marked as requiring an absent
special driver.

`hid-samsung.c` is deferred for this reason: the Samsung IrDA 184-byte path
forces HIDDEV and disables normal hidinput. The current firmware has no useful
hiddev consumer/proxy for ErgoType input routing, so the imported source stays
in the tree but is not linked by the CMake HID allowlist. Its emulator fixture
is not part of the current devices repo branch set.

Normal HID keyboard LEDs are not part of the Linux LED class boundary above.
CapsLock/NumLock/ScrollLock output remains in the generic HID input path:
`EV_LED` updates schedule `hidinput_led_worker()`, which sends the keyboard
LED SET_REPORT through the HID ll_driver request path. The deferred LED class
means `/sys/class/leds`-style brightness devices and vendor LED/RGB panels,
which need a firmware proxy/API before they are useful in this embedded host.
On boot-keyboard start, `usbhid` also clears NumLock and submits the complete
output report through the same route.

## Active USB Magic Mouse / Trackpad Boundary

Upstream `hid-magicmouse.c` is now linked for only the USB Magic Mouse 2 and
Magic Trackpad 2 IDs. The current port already has
the synchronous raw SET path over the async EP0 owner, input MT slots, and the
timer bridge. A line-by-line USB-only reachability audit shows that it does not
require enabling the currently compile-gated delayed-work API: USB Magic Mouse
2 returns after `hid_hw_start()`, and the USB Trackpad 2 path can reach the
mode SET but the upstream delayed retry condition selects Magic Mouse 2 only.
The three unreachable upstream delayed-work calls remain commented beside the
explained firmware disable rather than adding an unused timer/workqueue
lifetime model. Bluetooth and legacy IDs likewise remain visible but disabled.
With `CONFIG_HID_BATTERY_STRENGTH` disabled, the retained
battery timer performs one harmless failed lookup and does not rearm.

Upstream intentionally returns from the USB Magic Mouse 2 probe immediately
after `hid_hw_start()`: it does not reach the later native-report registration
or mode SET used by the other paths. Preserve that behavior; these USB IDs do
not claim the full Bluetooth Mouse 2 touch path. USB Trackpad 2 is the actual
wired multitouch target and exposes four HID interfaces: one mouse and three
non-mouse. The current `CFG_TUH_HID=4` fits that standalone device exactly.

The larger remaining risk is after Linux input, not in the TinyUSB transport.
A 15-contact native frame can publish more records than the fixed 64-record
evdev-to-KeyD queue; later legacy pointer-emulation ABS/SYN records can then be
dropped even though Linux's MT state is correct. Do not rewrite
`hid-magicmouse.c` to hide that boundary. Either add a later Linux-to-KeyD
filter/batched queue contract, or explicitly limit the first hardware claim to
low-contact traffic with visible drop diagnostics.

Expected extra live heap, including input value storage, is roughly 4.2 KiB
for Magic Mouse 2 and 5.3 KiB for a 16-contact Trackpad 2, before common HID
parser/report/evdev allocations. Keep Bluetooth and legacy IDs out of the
firmware allowlist. A dedicated fixture must cover all four Trackpad 2
interfaces, mode SET, native report ID `0x02`, and repeated reconnect. The
combined two-Pico pass on 2026-07-23 hardware-verified that normal low-contact
path, including removal and reconnect. Accepted `-EIO`, disconnect during SET,
contact ID 32, and multi-contact queue saturation remain separate fault tests.
This bounded USB result is not a reason to enable every Apple HID driver or an
HID++-style protocol stack.

## Future Transport Work

Drivers beyond the current boundary need extensions to the existing
sync-over-async transport:

- add request-specific URB submit/kill/resubmit ownership
- add synchronous interrupt-IN without stealing continuous HID polling
- add any request-specific transfer contract only for an audited linked user
- retain generation-based cancellation on unmount and fast replug

The Linux-shaped caller may block only in a task; TinyUSB callbacks must remain
bounded publishers.

## Examples To Keep Deferred Until Their Dependencies Exist

These are examples from upstream classes that still need a driver-by-driver
subsystem/lifecycle audit before they can be trusted. Synchronous raw GET/SET
by itself is no longer a transport blocker:

- `hid-alps.c`: raw GET/SET transport is present, but the surrounding init and
  device-specific state remain unaudited.
- `hid-letsketch.c`: uses `usb_string()` for tablet string data.
- `hid-lg.c` / `hid-lg4ff.c`: feature/raw transport is present, but FF and
  wait-style init dependencies remain unaudited.
- `hid-logitech-hidpp.c`: request/response protocol with wait queues.
- `hid-ntrig.c`: USB control-message firmware/query path.
- `hid-sony.c`, `hid-nintendo.c`, `hid-playstation.c`: controller init uses
  request/response and worker-style state.
- `hid-uclogic-params.c`: USB strings, interface counts, and returned feature
  data drive parameter setup.

This list is not exhaustive. Reimport one driver at a time and record the
specific upstream wait/request site and the hardware/emulator check that makes
it safe under the firmware transport layer.
