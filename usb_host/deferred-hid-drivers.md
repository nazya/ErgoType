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

## HIDRAW and Logitech HID++ Boundary

HIDRAW is a Linux client interface, not a device protocol. Ordinary keyboard
and mouse input does not need it, and Logitech HID++ kernel drivers can use
`.raw_event` plus `hid_hw_raw_request()` without a Linux `/dev/hidraw` file.
Some device families need raw-report access beyond ordinary keyboard and mouse
input. Firmware now has the first independent HIDRAW lifecycle checkpoint,
deliberately smaller than the Linux character-device ABI:

- upstream-shaped `hidraw_connect()`, `hidraw_disconnect()`,
  `hidraw_report_event()`, and `HID_CLAIMED_HIDRAW` ownership are active;
- a small per-device object follows HID lifecycle and is revoked before free;
- no report is retained and no report-time allocation occurs because no raw
  consumer is linked;
- there is no subscriber, queue, descriptor API, GET/SET API, `/dev`, file
  descriptor, `read()`, `ioctl()`, `poll()`, or VFS emulation.

A later real raw-report consumer may add one bounded subscription, explicit
drop accounting, descriptor access, and GET/SET/OUTPUT over the existing async
transport. Those parts are not current functionality and must not be added
without a device or internal consumer that defines their exact contract.

Direct HID++ does not depend on HIDRAW. The hardware-tested request/reply
foundation links a deliberately narrow slice from pinned upstream
`hid-logitech-hidpp.c`; the current host extends it:

- direct USB `046d:c08d` and `046d:c08a` are selected;
- generic HID input remains active, while the HID++ driver adds `.probe`,
  `.remove`, `.raw_event`, and protocol-version detection;
- a real single-waiter task bridge preserves register-before-test, the durable
  response predicate, timeout, response wake, and exact-interface disconnect
  cancellation without polling or report-time allocation;
- probe-time interrupt replies enter only the driver's validated `raw_event`
  matcher; ordinary field/input parsing stays behind final evdev activation;
- the current extension enables upstream pre-connect HID++ 2.0 name
  and unit-ID/serial discovery before `hid_connect()`; the unit ID remains in
  Linux HID/input state;
- the evdev/devmon boundary retains the bounded final name and the KeyD task
  reports device attachment as `DEVICE: added <vid:pid> <name>`;
- direct HID++ battery discovery and notifications use a real reduced
  `power_supply` registration/get-property/change/unregister boundary. Each
  supply publishes detached full snapshots through its own length-one member
  of a separate devmon power QueueSet, so pending changes coalesce to the latest
  value. The UI task currently reads and ignores value events, then owns queue
  cleanup after terminal `REMOVED`; KeyD and the ordinary devmon path are not
  involved, and UI presentation remains deferred;
- generic HID battery strength, sysfs, force feedback, broad Logitech
  matching, Bluetooth, and legacy 27 MHz classes remain visibly gated in
  their upstream positions. The current exact-class stage additionally selects
  only the exact upstream M560, T650, K400, and K750 DJ child classes,
  including their class-specific input hooks and delayed initialization.

The first single-M705 receiver checkpoint has since been superseded by the
pinned-upstream-shaped `hid-logitech-dj.c` port. Runtime matching still enables
only receiver `046d:c52b`; the other upstream receiver IDs remain behind
`CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS`. The active port retains the upstream
multi-slot virtual-child model, standard mouse/keyboard/Consumer/power/media
descriptors, HID++ descriptors, and virtual-child raw-request routing through
the physical receiver. Firmware glue supplies the task-owned work/lifecycle
boundary and final evdev activation.

The exact-class extension keeps the upstream IDs and quirks:
M560 `046d:402d`, T650 `046d:4101`, K400 `046d:4024`, and K750
`046d:4002`. M560 and T650 retain upstream delayed input publication; T650
uses the WTP raw-XY path, K400 retains its normal composite reports, and K750
uses the reduced power-supply boundary for solar events. It does not enable a
wildcard ID, Bluetooth, Bolt, force feedback, or a new HIDRAW consumer. Its
matching automatic emulator completed the full hardware sequence on
2026-07-26, including both M560/T650 generations, K400, K750, and the final
direct regression.

The exact diagnostic host/emulator pair completed two automatic hardware runs
on 2026-07-24, including an ordinary HID++ 1.0 RAP request and HID++ 2.0 FAP
request with BUSY retry. Temporary task-side markers and those otherwise unused
requests are absent from the production checkpoint. The production-clean pair
was rebuilt reproducibly and accepted as post-test cleanup without another
hardware run; the exact hardware verdict remains associated with the
diagnostic hashes. The later full combined emulator sequence passed on RP2040
with temporary host limits `HID_MAX_FIELDS=8`, `HID_MAX_USAGES=256`.
Hardware testing at the retained `64/675` policy showed that standalone and
simultaneous M705 and ordinary DJ keyboard children work. The combined graph
leaves `free=5160`, `min=4008`, and no new OOM. The complete HID++ eQuad
keyboard connection profile does not fit as a standalone child and adds the
run's only OOM count. Cleanup recovers, and subsequent M705, ordinary keyboard,
and direct HID++ phases continue to work. The earlier two-OOM result used the
upstream-sized 256-field table rather than the retained 64-field table. Pinned
upstream supplies protocol-version detection but no generic version query for
Logitech firmware entities.

The staged order from here is:

1. Keep the hardware-verified exact-class change at the Linux input/evdev
   boundary. Downstream KeyD
   policy for high-resolution wheel or absolute touch input is a separate
   stage.
2. Extend reduced HIDRAW only when a concrete driver or internal consumer
   requires descriptor or raw-report exchange.
3. Preserve `64/675` as the current compatibility policy. Do not optimize only
   to force every large descriptor graph onto RP2040; validate those graphs on
   a measured larger-RAM target if that becomes the selected hardware.

The current capability/receiver matrix covers M705, M560, K400, K750,
T650, generic simultaneous Unifying keyboard/mouse children, and direct USB
HID++ regression. A direct-USB MX Vertical capability fixture remains future.
Logitech Bolt must be treated as a separate protocol/device check rather than
assumed from Unifying/DJ coverage. Bluetooth-only models remain outside this
USB transport until a Bluetooth HID backend exists.

The first UC-Logic stage is now linked as a hardware candidate. It activates
only Huion dynamic IDs `256c:006d/006e` and XP-Pen Deco 01 V2 `28bd:0905`;
the rest of the complete pinned-upstream match table remains visible but
inactive. The Huion path uses decoded string 201 and raw parameter string 200.
The three-interface Deco path sends its ten-byte probe through interrupt OUT
endpoint `0x03`, then reads raw parameter string 100. Both operations run in
the lifecycle task over the existing async transport owner. No HIDRAW, VFS,
KeyD tablet policy, UI, LED, or power expansion is part of this stage.

The matching automatic emulator covers a generic pen, H640P and Kamvas 13
parameter profiles, and Deco 01 V2 pen/pad/frame-mouse input. This establishes
specific profile coverage, not every retail model sharing Huion's dynamic
IDs. Hardware acceptance is still pending and must include full heap recovery,
`oom=0`, and a nonzero lifecycle stack watermark. Wacom is the next larger
tablet family after that result.

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
This bounded USB result is not a reason to enable every Apple HID driver or
the broader HID++ capability/receiver stack.

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
- broader `hid-logitech-hidpp.c` capability, power, touchpad, and receiver
  paths beyond the narrow direct request/reply candidate above.
- `hid-ntrig.c`: USB control-message firmware/query path.
- `hid-sony.c`, `hid-nintendo.c`, `hid-playstation.c`: controller init uses
  request/response and worker-style state.

This list is not exhaustive. Reimport one driver at a time and record the
specific upstream wait/request site and the hardware/emulator check that makes
it safe under the firmware transport layer.
