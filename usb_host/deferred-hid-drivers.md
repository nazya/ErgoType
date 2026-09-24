# Deferred HID Drivers

This note is about the current task-owned HID host architecture. It is not a
list of every "large" driver. The important boundary is whether the upstream
driver's transport and subsystem dependencies are implemented by the firmware
port.

## Product Decisions and Remaining Wired USB Gaps

This is the authoritative selection register for the current working tree,
audited against pinned Linux `83f1454877cc292b88baf13c829c16ce6937d120`.
Later sections explain individual implementations and measurements. Keep these
states distinct:

- **active** means linked in the production build; it does not claim that every
  retail alias was exercised on hardware;
- **retained, default-off** means a selected port is present but deliberately
  omitted from CMake to save firmware/RAM until that device is selected;
- **remaining input gap** means the complete applicable upstream behavior is
  not yet available as a coherent port merely by enabling one CMake entry;
- **outside the current product scope** means deliberately not selected now,
  not impossible to port later.

The selected product is a wired remapper for user-pluggable keyboards, mice,
presenters, touchpads, drawing tablets, pads, dials, and their USB receivers.
A gaming label does not exclude an otherwise useful keyboard or mouse.
Conversely, generic HID fallback is not equivalent to Linux driver support:
basic keys or pointer movement may work while repaired descriptors, extra
buttons, macro keys, high-resolution axes, battery data, or vendor functions
remain absent. Scope ends at Linux input/evdev; KeyD policy is separate.

### Gaming-related coverage already present

- Razer BlackWidow, Cougar 500K/700K, Corsair Glaive/Scimitar Pro/K70,
  Genius Gila/Manticore/GX Imperator, Saitek/Mad Catz R.A.T./M.M.O. mice,
  Holtek gaming keyboard/mice, and the selected EVision paths are active.
- Wired Logitech HID++ plus Unifying, gaming, Lightspeed, and Powerplay DJ
  receivers are active within the memory limits documented below.
- Rapoo `24ae:2015` is active through `hid-rapoo.c`: interface 1 publishes its
  two side-button bits through a separate managed Back/Forward input. The
  older pinned HID-BPF descriptor fix targets the same receiver and is not
  stacked on top of that newer driver. Two synthetic two-interface generations
  passed the managed-input lifecycle regression described below.
- The pinned G13 `046d:c21c`, G11 `c225`, G15
  `c222/c227`, and G510 `c22d/c22e` input/control paths are active.
  Independent macro, preset, record, LCD-menu and applicable mute keys are
  retained, as is the G13 thumbstick input and the exact startup
  interrupt-OUT/Feature-SET flow.
  LED/backlight code and Z-10 remain adjacent but compile-disabled.
- Corsair K90 `1b1c:1b02` now selects the upstream G1--G18, profile, and
  record-key mapping. The port deliberately leaves the keyboard's current
  macro mode unchanged, as Linux does until userspace requests a mode change;
  LED/backlight/sysfs controls remain outside the input-only boundary.
- Original Roccat Kone `1e7d:2ced` now retains the upstream firmware-1.38
  duplicate tilt/special-event suppression. Profile/DPI configuration and the
  `/dev/roccat*` publication path have no selected firmware consumer.
- The pinned non-FF `hid-lg.c` paths are active for
  Logitech S510 `046d:c50c`, UltraX `c101`, diNovo `c704`, Elite `c30a`, and
  LX500 `c512`. The complete S510 `0x104d` descriptor expansion and mappings
  are retained. `HID_MAX_USAGES` remains the sole capacity knob: the current
  RP2040 fixture narrows only its synthetic range to exercise the code, while
  a full retail-range verdict requires a sufficiently large setting and heap,
  expected on a PSRAM target. Wheel, joystick, force-feedback, Wii setup, and
  SpaceNavigator paths remain visibly gated.
- Sony VAIO RF mice `054c:024b/0374` are linked through the pinned
  `hid-sony.c` descriptor correction. Only those exact wired USB rows are
  active; Sony controller and Bluetooth paths remain visibly gated.
- Pinned `hid-kysona.c` is active for Kysona M600 and VXE
  R1 Pro wired/dongle rows `3554:f57c/f57d/f58a/f58c`. Interface 1 sends the
  exact online/battery OUTPUT queries at probe and every five seconds; accepted
  INPUT replies publish online, charging, capacity and voltage through the
  unchanged reduced `power_supply` snapshot boundary. Driver-local
  `power_supply_changed()` calls replace Linux's direct sysfs visibility; the
  UI still consumes snapshots without presenting them.
- Static common quirks already cover ADATA XPG, Cooler Master, Logitech G710+,
  and several Corsair K65/K70/K95/Strafe/M65 rows. They are table quirks, not
  missing vendor-driver ports.
- Glorious Model O/O-/D/I `258a:0036/0033`, `22d4:1503`, Redragon Asura
  `0c45:760b`, and Semitek `1ea7:0907` descriptor/input fixes are retained and
  default-off; they are unavailable in the production build until their CMake
  entries are enabled. Rakk Dasig X `248a:fb01/fa02` and the IOGEAR
  MMOmentum `258a:0027` native HID-BPF fix are likewise default-off, with their
  synthetic executable paths covered by the documented hardware matrix.

### Concrete remaining wired-device work

| Device or family | Current gap | Decision |
| --- | --- | --- |
| 3Dconnexion SpaceNavigator `046d:c626` / SpaceTraveller `c623` | Six axes are declared REL instead of ABS. The old `hid-lg.c` fix covers only some offsets; pinned SpaceNavigator HID-BPF additionally covers descriptor sizes 202/217/228 and offsets 32/36/49/53. | Explicitly deferred for now. If resumed, port and test the applicable `hid-lg` and HID-BPF fixes together. |

### Hardware-qualified current input batch

- Wacom ArtPen on Intuos Pro 2 M `056a:0357`, tool `0804`, and WALTOP
  Batteryless Tablet `172f:0505` are retained through their pinned HID-BPF
  descriptor/event programs. Their two source entries are commented out in
  the default CMake build and can be restored individually. ArtPen keeps
  per-HID interpolation state; WALTOP keeps per-HID button state and the
  complete replacement descriptor.
- Creative Prodikeys `041e:2801` links the upstream descriptor repair,
  report-6 initialization, Fn state, and office/media keys. ALSA/raw-MIDI,
  musical-note reports, and the MIDI launcher mode are deliberately omitted;
  ordinary keyboard reports remain on the Linux HID input path.
- K90 and original Kone use the active input boundaries described above.
  The combined CDC-free synthetic fixture passed on 2026-09-22: all 14
  generations matched the input/evdev oracle, including mapping/filter
  branches, negative predicates, held-state removal, and same-device
  reconnect. Initial `f1`, fourteen `f11` pairs, and terminal `f10` completed
  without `f12`; all 25 target input lifetimes balanced. The two Prodikeys
  fixups and nine ordered report-6 SETs matched, all 83 heap snapshots had
  `oom=0`, and marker-only heap plateaus were identical. Exact artifact
  hashes, event values, watermarks, and synthetic-test limits are recorded in
  [`hid-emulator-coverage.md`](hid-emulator-coverage.md#hardware-verified-k90-kone-prodikeys-waltop-and-artpen-matrix).

### Explicitly outside the current stage

- No further game-controller expansion: PlayStation/Sony controller rows,
  Nintendo, Steam, NVIDIA Shield, Lenovo Legion Go, ASUS Ally, Logitech
  wheels/joysticks, Thrustmaster, WinWing, universal PIDFF, and the older
  ACRUX/Betop/BigBen/DragonRise/EMS/GreenAsia/Mayflash/MegaWorld/PantherLord/
  SmartJoy/ZeroPlus families remain out. Some need real input repair as well as
  force feedback; they are excluded by product scope, not described as
  FF-only. Existing PXRC, Saitek PS1000, and retained Stadia FF code are
  historical exceptions and do not select a broader controller roadmap.
- Controller-only HID-BPF programs for FR-TEC Raptor Mach 2, Thrustmaster TCA
  Yoke Boeing, and Bluetooth Xbox Elite 2 remain out. Steam's controller
  driver and its lizard-mode management are also separate from accepting the
  keyboard/mouse reports that hardware may emit on its own.
- SteelSeries in the pinned baseline means the SRW-S1 wheel and Arctis 1/9
  headset battery paths, not a missing modern SteelSeries keyboard/mouse
  driver. Corsair Void, Logitech HID++ headset rows, Jabra, Plantronics, and
  CMedia audio-related controls are likewise outside the input product.
- No new Bluetooth, internal-laptop, detachable-cover/dock, or handheld-control
  family is selected merely because its electrical transport is USB. This
  excludes TUXEDO Sirius F13 filtering, Synaptics RMI laptop/cover devices,
  and laptop/handheld-only backlight or controller paths. Already active
  historical exceptions such as ASUS T101HA, Medion E1239T, and ALPS remain
  supported within their documented boundaries.
- Linux udev metadata alone is not a firmware input feature. The generic
  Win8-touchpad HID-BPF PadType property is intentionally omitted because it
  changes no input report. Sysfs/hidraw vendor configuration, RGB/DPI/profile
  channels, framebuffer/LCD appliances, ALSA/raw-MIDI, and hwrng stay out
  until a concrete firmware consumer is selected.
- HID proxy work remains paused until a new explicit user request. This
  decision does not block ordinary host-side raw GET/SET or HID++ transport.

The combined CDC-free synthetic matrix passed on hardware on 2026-09-22 with
test host UF2
`b17c6e59ae7964168dbb286d777a7974a99e27fbceef2f65d9cad09d41745997`,
emulator UF2
`daee91a028ec4089d2e249048fc45dbcb52946a71d986cd59d36eeb92e2dcb35`,
and main-host log
`dfb72561c3bb5201c3465e9a66a501a53f29490707fa9f916ff134f21daa8d69`.
All 28 target generations reached their marker, including reconnects, the
Rapoo managed secondary input, exact and negative Sony descriptor predicates,
legacy Logitech mappings, Kysona polling/retry/publication, and balanced
teardown. The log had no failure marker, `ERR`, `WARN`, OOM, or zero task
watermark. This qualifies the exercised host paths and synthetic descriptors;
it does not claim retail descriptor fidelity, the full S510 range at 675
usages, LED/LCD behavior, or external presentation of battery snapshots.

Capacity and evidence are separate from the product exclusions above.
`HID_MAX_FIELDS=64` and `HID_MAX_USAGES=675` deliberately bound the Linux data
model on RP2040; raising those same build-time limits on a larger-memory target
increases descriptor coverage without introducing another parser. Logitech
`c52f/c534`, the synthetic `c539` child, Lenovo `60ee`, Cherry Cymotion, and
Huawei CD30 have documented capacity constraints. Production rejects wildcard
Wacom descriptors requiring paired Pen/Touch mode-change; the full sibling
implementation remains only on its WIP branch and still needs the two-device
hub qualification. Synthetic fixtures prove the exercised host branches, not
unobserved retail-device details.

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
- multi-interface protocols that require generic USB-core ownership or sibling
  coordination beyond the explicit lifecycle-owned Wacom `056a:0084` registry
  path
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
- full Linux LED class/sysfs behavior beyond the reduced Wacom class/trigger
  contract
- backlight
- the full Linux power-supply class, sysfs, uevents, and notifier ownership
  (the linked drivers use a reduced detached-snapshot glue boundary)
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

## Deferred Synaptics RMI-over-HID Boundary

`hid-rmi.c` is deferred by product scope, not because wired USB transport is
unavailable. Its known upstream USB devices are internal laptop touchpads or
touchpads integrated into detachable keyboard covers and docks, such as the
Razer Blade 14, Lenovo X1 Cover, and Acer Switch 5. The `HID_GROUP_RMI`
wildcard identifies the protocol; it is not evidence of a mainstream
standalone RMI peripheral intended for an external USB Type-A or Type-C port.
These devices are outside the current input-side boundary of user-pluggable
wired USB peripherals and receivers.

Reconsider the upstream RMI stack if a concrete external wired USB product or
an explicit need for one of the internal, cover, or dock devices appears.

## Active Microsoft Wired USB Boundary

The complete pinned `hid-microsoft.c` is linked with a deliberately narrow
non-gaming USB table. Active IDs are `045e:0048`, `009d`, `00b4`, `00db`,
`00dc`, `00e3`, `00f9`, `0713`, `071d`, `0730`, `0732`, `0750`, `076c`, and
`07da`. They retain the upstream ergonomic/vendor-key mappings, presenter
controls, 571-byte descriptor correction, `NOGET`, duplicate-usage handling,
and forced-HIDINPUT branches.

SideWinder `045e:003b`, Bluetooth Presenter/Surface Dial, Xbox/8BitDo, and the
force-feedback worker remain visible behind
`CONFIG_HID_MICROSOFT_ALL_DEVICES`. Their matching entries are gated in both
the driver table and `hid_have_special_driver[]`, so an excluded USB device is
not denied generic HID fallback. Neither `ff-memless` nor a game-controller
consumer is added.

The active Microsoft call graph allocates only devres-owned `ms_data`; it adds
no work, timer, or asynchronous request lifetime. The first complete hardware
run demonstrated that pinned upstream's function-static F14--F18 `last_key`
crosses two simultaneous ergonomic HID owners and leaves F14 pressed until
disconnect. The port now retains that upstream static line commented beside a
narrow bugfix and stores the selector in per-device `ms_data`. This adds four
dynamic bytes per bound Microsoft HID and no static BSS; the exact fixed-host
pair passed on hardware on 2026-07-31. Its two-device interleave released F14
and F15 in owner order before either interface removal, all representative
profiles returned to the same 60752-byte free-heap plateau, and the run reached
terminal `f10` with `oom=0` and no host `ERR`.

## Active Apple External USB Boundary

The complete pinned `hid-apple.c` is linked for 18 external wired devices:
Mighty Mouse `05ac:0304`; Aluminum Mini `021d/021e/021f`; Aluminum
`0220/0221/0222`; Aluminum Rev B `024f/0250/0251`; Magic Keyboard 2015
`0267/026c`; Magic Keyboard 2021 `029a/029c/029f`; and Magic Keyboard 2024
`0320/0321/0322`. The Touch-ID-named products are supported only as
keyboards; no fingerprint interface is claimed.

Bluetooth, internal Fountain/Geyser/Wellspring/T2 devices, trackpad-only
interfaces, Touch Bar, and backlight-only endpoints remain visible behind
`CONFIG_HID_APPLE_ALL_DEVICES`. The same exact boundary is mirrored in
`hid_have_special_driver[]`, so excluded Apple USB products retain generic HID
fallback. The active scope needs no Apple LED/backlight or Linux sysfs proxy.

The active driver preserves upstream Fn/F-key translation, ISO/JIS correction,
Mighty Mouse button/HWheel handling, and the Magic Keyboard battery descriptor
and 60-second timer. The firmware difference in `apple_fetch_battery()` is
limited to the existing sparse report lookup beside the retained upstream
`report_id_hash[]` line. Removal still synchronously deletes the battery timer
before `hid_hw_stop()` drains queued or in-flight GET_REPORT requests and
before devres releases `apple_sc`, reports, or battery state.

The exact host and compact four-profile fixture passed on hardware on
2026-07-31. The run covered Aluminum Fn release ordering, Mighty Mouse button
mapping, both Magic Keyboard generations, cancellation before the
2015 timer deadline, one real 60-second 2024 battery deadline, disconnect with
the GET queued, and a fresh GET after reconnect. Terminal `f10` arrived with
`oom=0`, stable Magic removal heap, nonzero task watermarks, and no host `ERR`.
Production logs still do not expose detached battery snapshot values.
They also do not expose REL_HWHEEL; both relative Z signs were delivered by
the fixture, while their inversion remains source-audited.

## Active Lenovo External USB Boundary

The complete pinned `hid-lenovo.c` is linked with three external USB
TrackPoint keyboard rows retained:

- ThinkPad USB Keyboard with TrackPoint `17ef:6009`;
- ThinkPad Compact USB Keyboard with TrackPoint `17ef:6047`;
- ThinkPad TrackPoint Keyboard II USB `17ef:60ee`.

Only `6009/6047` are selected in `lenovo_devices[]`.
`hid_have_special_driver[]` retains the same pinned Linux pair. The adjacent
`60ee` row is behind `CONFIG_HID_LENOVO_ALL_DEVICES`; generic HID can attempt
fallback, but its full report graph cannot be allocated at retained 675 usages
on RP2040. Bluetooth, I2C, ScrollPoint, Pro Dock, tablet, and Linux
audio-LED-trigger paths remain visible but inactive; Legion is a separate
unlinked driver family. Other excluded USB products retain generic HID
fallback.
`CONFIG_HID_LENOVO_ALL_DEVICES` is a documentary source gate, not a supported
standalone build switch; the retained tablet portion still requires the
separately deferred Linux LED class.

The `6009` TrackPoint half retains pinned validation of feature report 4 and
output report 3, the Button-16-to-F20 mapping, Windows-compatible defaults, and
one nonfatal asynchronous feature SET_REPORT. The direct report lookup uses
the existing sparse report registry; validation proves the report before that
lookup, so no new defensive check is present.

The mouse interfaces of `6047` and `60ee` retain the pinned three-byte
synchronous raw-request command sequence, Fn/vendor mappings, wheel axes, and
middle-button click-versus-scroll state machine. Their keyboard interfaces
remain normal HID input and allocate no Lenovo state; the third `60ee` vendor
interface likewise carries no active Lenovo state. Disconnect completes the
exact-interface synchronous waiter before its command buffer is freed; the
asynchronous `6009` report snapshot is cancelled and drained by
`hid_hw_stop()` before report/devres release.

The active `lenovo_drvdata` is 28 bytes because tablet work and unavailable
Linux audio LED-class state remain gated. Sysfs group creation keeps its pinned
nonfatal control flow but publishes no firmware interface under the reduced
sysfs contract. `KEY_FN_ESC` reaches Linux input for `6047`, and for `60ee`
when its retained Lenovo row is enabled, but the current KeyD boundary logs and
drops evdev code `0x1d1`; that downstream mapping is not part of this
Linux-driver stage. The combined `6009/6047/60ee` hardware sequence completed
at temporary `64/256`; the selected `6009/6047` exact retained-policy
regression remains pending. The full `60ee` Consumer field needs another
13,408 bytes at 675 after a run that left 11,152 bytes free, so its upstream
table row is retained but disabled on RP2040.

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
foundation came from a narrow slice of pinned upstream `hid-logitech-hidpp.c`.
Committed host change `27bf73e` restores its common USB/DJ flow:

- direct USB `046d:c081/c082/c086/c087/c088/c08a/c08d/c090/c091/c094/c09a/
  c09b/c343` are selected; the source retains pinned table order;
- ordinary HID input remains active, with the upstream HID++ probe, raw,
  fixup, event, and input callbacks;
- a real single-waiter task bridge preserves register-before-test, the durable
  response predicate, timeout, response wake, and exact-interface disconnect
  cancellation without polling or report-time allocation;
- probe-time interrupt replies enter only the driver's validated `raw_event`
  matcher; ordinary field/input parsing stays behind final evdev activation;
- upstream HID++ 2.0 name and unit-ID/serial discovery runs before
  `hid_connect()`; the unit ID remains in Linux HID/input state;
- the evdev/devmon boundary retains the bounded final name and the KeyD task
  reports device attachment as `DEVICE: added <vid:pid> <name>`;
- direct HID++ battery discovery and notifications use a real reduced
  `power_supply` registration/get-property/change/unregister boundary. Each
  supply publishes detached full snapshots through its own length-one member
  of a separate devmon power QueueSet, so pending changes coalesce to the latest
  value. The UI task currently reads and ignores value events, then owns queue
  cleanup after terminal `REMOVED`; KeyD and the ordinary devmon path are not
  involved, and UI presentation remains deferred;
- exact DJ child rows `4011/4101/1017/402d/101b/101a/4024/4002/407f` retain
  their upstream quirks before the DJ-group wildcard. This is not a wildcard
  for arbitrary physical Logitech USB devices. A child without HID++ reports
  stays bound to HID++ with ordinary HID input and no protocol state;
- common battery, high-resolution wheel, touch, connect, and reset work are
  active. Both workers stop before waiter unbinding and mutex destruction.
  Linux sysfs publication remains a reduced no-op; force feedback, headsets,
  Bluetooth, legacy 27 MHz, and proxy/Dinovo exact classes stay gated.

The first single-M705 receiver checkpoint has since been superseded by the
pinned-upstream-shaped `hid-logitech-dj.c` port. Runtime matching enables
`046d:c52b/c532`, gaming/Lightspeed/Powerplay
`c531/c537/c539/c53a/c53f/c543`, and Lightspeed 1.3 `c547/c54d`.
The adjacent upstream `c52f` mouse-only and `c534` HID++ rows
remain intact behind `CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS`: at the retained
`HID_MAX_USAGES=675`, their physical 652-usage Consumer field requests 20,980
bytes and their 675-entry virtual-child field requests 21,716 bytes. Those two
persistent allocations alone total 42,696 bytes and do not fit the RP2040 heap
with the receiver graph. Legacy 27 MHz, Bluetooth-proxy, and Dinovo rows remain
behind the same broader gate. The active port retains the upstream multi-slot
virtual-child model, standard
mouse/keyboard/Consumer/power/media descriptors, HID++ descriptors, and
virtual-child raw-request routing through the physical receiver. Firmware glue
supplies the task-owned work/lifecycle boundary and final evdev activation.
The exact `c52f/c534` startup, child-input, and teardown logic passed on
hardware at temporary `64/256`; that capacity checkpoint is not support at the
retained policy. Existing `c52b` coverage remains valid; the selected `c532`
exact retained-policy regression remains pending. Because the IDs are absent only from the special-driver table,
Linux-style generic fallback may still publish their physical interfaces; it
sends no DJ/HID++ startup sequence and creates no virtual receiver child.

The earlier hardware-tested exact-class extension kept the upstream IDs and
quirks: M560 `046d:402d`, T650 `046d:4101`, K400 `046d:4024`, and K750
`046d:4002`. M560 and T650 retain upstream delayed input publication; T650
uses the WTP raw-XY path, K400 retains its normal composite reports, and K750
uses the reduced power-supply boundary for solar events. That stage did not
enable a wildcard ID, Bluetooth, Bolt, force feedback, or a new HIDRAW consumer. Its
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
only OOM count in that earlier run. Cleanup recovers, and subsequent M705,
ordinary keyboard, and direct HID++ phases continue to work. The later
13-generation production attempt also exhausted heap at the synthetic `c539`
child and did not reach `c547`; both branches passed at temporary 256 usages.
The still earlier two-OOM result used the upstream-sized 256-field table rather
than the retained 64-field table. Pinned upstream supplies protocol-version
detection but no generic version query for Logitech firmware entities.

The staged order from here is:

1. Retain the 2026-09-22 13-generation HID++ protocol/lifecycle pass at
   temporary `64/256`. It covers wheel setup, reset-work, gaming/Lightspeed,
   physical-button WTP, and wildcard-fallback paths, not exact hi-res values
   or queued-versus-running reset timing. Scope ends at Linux input/evdev;
   KeyD/downstream policy is unchanged. See `hid-emulator-coverage.md`.
2. Extend reduced HIDRAW only when a concrete driver or internal consumer
   requires descriptor or raw-report exchange.
3. Preserve `64/675` as the current compatibility policy. Do not optimize only
   to force every large descriptor graph onto RP2040; validate those graphs on
   a measured larger-RAM target if that becomes the selected hardware. The
   synthetic `c539` child exhausted heap at 675; `c547` was not reached there.
   Both passed at temporary 256; this is not a per-retail-descriptor verdict.

The earlier hardware-qualified capability/receiver matrix covers M705, M560,
K400, K750, T650, generic simultaneous Unifying keyboard/mouse children, and direct USB
HID++ regression. A direct-USB MX Vertical capability fixture remains future.
Logitech Bolt must be treated as a separate protocol/device check rather than
assumed from Unifying/DJ coverage. Bluetooth-only models remain outside this
USB transport until a Bluetooth HID backend exists.

The first UC-Logic stage is linked and hardware-passed. It activates Huion
dynamic IDs `256c:006d/006e` and XP-Pen Deco 01 V2 `28bd:0905`; the rest of
the complete pinned-upstream match table remains visible but inactive. Its
automatic matrix covered a generic pen, H640P and Kamvas 13 parameter
profiles, and Deco 01 V2 pen/pad/frame-mouse input. The 2026-07-26 run
completed with stable removal plateaus, `oom=0`, and a 92-word minimum
lifecycle watermark.

The modern UGEE-v2 extension is linked and hardware-passed. It selects the
common upstream paths for Deco L/LW `28bd:0935` and Deco Pro S/SW/MW
`28bd:0909/0933/0934`. Deco L and LW share a PID and use the optional product
string to select battery behavior; a missing string remains the wired profile.
Pro SW/MW retain the upstream mouse-frame and battery quirks. Unsolicited
battery reports feed the existing reduced power-supply snapshot, and the exact
reconnect event schedules the upstream re-probe work. No HIDRAW, VFS, KeyD
tablet policy, UI, or LED expansion was needed for this stage.

The expanded matrix completed on hardware on 2026-07-27. It covered every
wired/wireless profile, both dial directions, battery and reconnect traffic,
detach with reconnect work queued, and the Magic Trackpad sparse battery
lookup. The run reached its complete marker sequence with `oom=0`, stable heap
plateaus, nonzero task watermarks, and no host `ERR`.

The focused exact-ID extension for XP-Pen Deco 01 original `28bd:0042` and
Parblo A610 Pro `28bd:1903` is also linked and hardware-passed. It opens only
those two existing pinned table rows. Deco reaches the v1 raw string-100, Pen,
and eight-key Pad path without an interrupt-OUT probe. Parblo reaches the
existing UGEE-v2 endpoint-`0x03` interrupt-OUT probe before raw string 100,
then Mouse, Pen, nine-key Pad, and Dial reports. The two rows add 32 bytes of
text and no data or BSS; they add no compatibility primitive, mutable state,
battery object, or scheduled work/timer path.

The focused run completed a full generation and same-PID reconnect for each
profile, ten balanced target input lifetimes, terminal `f15, f10`, and 22
`oom=0` heap snapshots with no host `ERR` or `HID_REPORT_SKIP`. Deco and Parblo
removal returned to stable 60,752-byte and 60,736-byte plateaus. All task
watermarks remained nonzero, with lifecycle at a minimum of 85 words. Pad keys
reach KeyD; pen tool/absolute values and Parblo Dial remain at the existing
downstream unsupported-event boundary.

The common UC-Logic failed-probe cleanup now also frees the separately
allocated combined replacement descriptor before the existing parameter
cleanup. It uses the already reachable `params_initialized` branch and does
not change the failing call's return value or successful probe/remove order.
Hardware fault injection exercised three `hid_hw_start()` failures after
combined-descriptor generation; every failed generation returned to the same
60,752-byte removal plateau, and a following normal Parblo generation
completed input and teardown. All 25 snapshots reported `oom=0`, with a
105-word minimum lifecycle watermark. A post-generation `hid_parse()` failure
uses the same label and free order, but that branch remains source-audited
rather than runtime-injected.

The exact Artist extension selects wired XP-Pen Artist 22R Pro `28bd:091b`
and Artist 24 Pro `28bd:092d`; every other row retains its preceding active or
gated state.
Both retain the pinned three-interface topology: interfaces 0 and 1 are
rejected, while interface 2 performs one 10-byte endpoint-`0x03` OUT before
reading raw string 100 and publishes Pen and 20-button/two-Dial Pad inputs.
Artist 24 also retains the pinned `fragmented_hires2` report rewrite. The
nested parameter initializer retains the existing 512-word lifecycle stack.
No battery, scheduled work, timer, or new async request contract is activated.

The focused hardware run used a full Artist 22R matrix, a same-PID 22R
reconnect smoke, and one Artist 24 matrix. Six Pen/Pad input lifetimes balanced;
the six expected non-Pen bind failures appeared as `HID_IGNORED`; all 21 heap
snapshots had `oom=0`; the minimum lifecycle watermark was 36 words; and the
run reached terminal `f15, f10` without `f12`, `HID_REPORT_SKIP`, or host
`ERR`. The fixture's internal oracle proves exact OUT-before-string ordering
for each generation. The log shows the downstream tablet diagnostics but not
the numeric ABS_X value after the Artist 24 rewrite, so that value remains
source-audited rather than hardware-observed. The final 512-word sizing run
left 36 words free; this measured margin must be rechecked if the lifecycle
call graph or build configuration changes.

The exact Star G640 Rev A extension selects only wired XP-Pen `28bd:0094`.
It keeps the pinned three-interface topology: interfaces 0 and 2 are rejected,
while interface 1 reads raw string 100, generates the v1 Pen descriptor, and
applies inverted proximity. No compatibility primitive, wrapper, battery,
work item, timer, or mutable driver state was added. The focused fixture
disconnected during the first string-100 request, then completed one full Pen
generation and one same-PID reconnect. Both complete removals returned to
`free/largest/blocks=60744/54464/9`; all 13 heap snapshots had `oom=0`; the
minimum lifecycle watermark was 85 words; and terminal `f1, f2, f3, f10`
arrived without `f12`, `HID_REPORT_SKIP`, or host `ERR`. Its parameter string
and reports are protocol-equivalent rather than a retail capture; exact
numeric X/Y values remain outside the production-log verdict.

## Active USB Wacom Boundary

The current production candidate links the complete pinned `wacom_sys.c` and
`wacom_wac.c` sources plus the scoped newer-upstream FIFO and post-start
cleanup fixes. A total of 134 fixed USB rows are active in `wacom_ids[]`: 133
Wacom rows plus wired Lenovo `17ef:6004`. Nineteen retain their existing
exact-image verdicts. The distinct executable paths exposed by the other 115
rows ran in the 33-profile representative family matrix on 2026-09-11;
same-path PID aliases are source-compared rather than redundantly attached.
Those rows are:
`0026/0028/002a/00d4/00d5/00dd/00df/0300/0301`, the
14 Graphire-family rows `0010/0011/0012/0013/0014/0019/0060/0061/0062/0063/`
`0064/0069/006a/006b`, and the eight Bamboo 2FG rows
`00d1/00d2/00d3/00d6/00d7/00d8/00da/00db`, plus 30 standalone legacy
Intuos/Cintiq rows: 11 Intuos/Intuos2, seven Intuos3, five wired Intuos4, and
seven Cintiq/DTK profiles, plus 53 legacy, TabletPC, Bamboo Pad, DTUS, and
multi-touch Wacom rows and the Lenovo TABLETPC row newly included in the same
single-Pico fixture.

The other 17 pinned Wacom USB PIDs are the canonical unfinished-profile list:
16 rows form eight cross-PID sibling pairs which one emulator Pico cannot
present concurrently, and `0094` is the hidraw-only bootloader profile.
They stay in their original upstream positions in `wacom_ids[]`, behind
`CONFIG_HID_WACOM_ALL_DEVICES`, and the same explicit PID list appears once in
the corresponding firmware-boundary block in `hid_quirks[]`. Each entry uses
the existing upstream `HID_QUIRK_IGNORE_SPECIAL_DRIVER` flag. Consequently
normal, unchanged HID driver matching skips Wacom and lets unchanged
`hid-generic` handle those devices. The code list in `hid_quirks[]`, rather
than a duplicated list in this document, is the source of truth for unfinished
fixed profiles. This boundary adds neither a Wacom-specific `.match()` callback
nor a change to `hid-generic` control flow; only the explicit quirk data is
port-specific.

Enabling `CONFIG_HID_WACOM_ALL_DEVICES` restores the upstream fixed Wacom rows
and compiles out the unfinished-profile quirk block. It also enables the
compile-gated non-USB rows, so it is a development-wide upstream
switch, not a supported USB-only completeness knob.

Every future pinned-table update must recompare the two PID sets and check for
an earlier Wacom quirk entry, because `hid_quirks[]` uses first-match order.

One final wired USB `056a:*` wildcard handles only a PID absent from the
unfinished-profile list. Receiver children use exact lookup among the active
fixed rows and can select neither a compile-gated profile nor the wildcard.
Bluetooth, I2C, PCI, and the broad all-devices policy remain disabled
in the production configuration.

After the wildcard descriptor is parsed, any retained INPUT usage equivalent
to `WACOM_HID_WD_MODE_CHANGE` is rejected before the device can bind. Only
that paired Pen/Touch mode-change runtime is deferred for lack of the required
two-Pico setup on a common working USB 2.0 hub; the unfinished-profile list
does not claim that all 17 fixed profiles need mode change. The preserved
implementation is on `wip/wacom-wildcard-mode-change` and is not part of the
production boundary.

The single-Pico matching matrix passed the current quirk-list candidate on
hardware on 2026-09-07: captured `0350` rejection is phase 6 and phase 7 is
the terminal host-gated acknowledgement. The run also covered unchanged
generic fallback for unfinished `0333`, receiver-child classification and
publication, two GET 8 completions, ordered input, and balanced teardown. The
test-only coordinator and verdict markers were removed before rebuilding
production. The run remains hardware evidence for the unchanged production
logic it exercised; only the exact cleaned production UF2 was not separately
flashed. A separate 2026-09-10 temporary-64 matrix qualified the independent
descriptor-signature path for `056a:03ce`: one lifetime canceled before any
delayed mode request and one completed the delayed Feature SET/GET. It also
qualified 18 receiver rebind generations across six child profiles. It
does not qualify production 675 or full Linux value retention; exact artifacts
are recorded in `hid-emulator-coverage.md`.
The 2026-09-11 representative matrix later exercised the normal-stack
rejection and the newly reachable fixed and descriptor-signature paths at a
temporary 32-usage limit. Its incomplete `p`/`m` marker capture and exact
artifacts are recorded in `hid-emulator-coverage.md`; production 675 remains
outside that verdict.

The following list describes the preceding hardware-tested exact-ID stages:

- One by Wacom Small CTL-472 `056a:037a` and Medium CTL-672 `056a:037b` use
  the upstream `BAMBOO_PEN` parser, record FIFO, sibling shared data, and
  one-second delayed Feature SET/GET mode switch. Their second HID interface
  reaches the upstream pen-only ghost-interface rejection.
- Intuos5 S PTK-450 `056a:0029` enables Pen, Pad, ExpressKeys, Touch Ring, LED,
  and delayed initialization.
- Bamboo Capture CTH-470 `056a:00de` and Intuos5 touch M PTH-650 `056a:0027`
  enable paired Pen/Touch/Pad input and pen-versus-touch arbitration; PTH-650
  also reaches Touch Ring, LED, and ordinary USB battery paths.
- External wired Intuos `056a:0302/0303/030e/0323` and Intuos 2
  `056a:033b/033c/033d/033e` select the complete pinned `INTUOSHT` and
  `INTUOSHT2` Pen/Pad paths. Touch is applicable to `0302/0303/033c/033e`;
  `030e/0323/033b/033d` are touchless profiles.
- External wired Intuos Pro `056a:0314/0315/0317` selects the complete pinned
  `INTUOSPS/PM/PL` pen/touch/pad/ring/LED and battery paths.
- Wired Cintiq 13HD `056a:0304` selects the existing pinned `WACOM_13HD`
  Pen/Pad path, nine numbered buttons, and one-second Feature report 2 mode
  exchange. Its focused fixture does not claim Touch Ring or `ABS_WHEEL`.
- Yoga 260 AES `056a:5048` enables the generic AES Pen/Touch parser, control
  exchange, delayed battery work, and idle-proximity timer.
- Wacom USB wireless receiver `056a:0084` enables the upstream three-interface
  monitor/stylus/touch topology, pair/unpair, dynamic sibling rebind, LED, and
  receiver battery lifetime. The fixture deliberately selects the already
  active PTH-650 child profile `056a:0027`; the available receiver capture
  reports child `033b`, which is now selected but was not part of that older
  artifact. Therefore `0084 -> 0027` is protocol-path coverage, not a captured
  pairing.

`INTUOSHT2` can receive a general pen packet before it knows the tool ID. Its
upstream `raw_event` path then queues Feature GET_REPORT 8 and deliberately does
not call `hid_hw_wait()`: Linux `hid_ctrl()` still parses that completion. The
firmware therefore treats a GET queued by the report task as ordinary
control-report work; only the lifecycle task can become the direct
`parser_owner`. The 2026-07-31 eleven-ID fixture proved that its first GET 8
reached the emulator, but not that the completion was parsed or that the CTRL
head advanced: an enter packet established the tool ID before the next general
packet, and disconnect could clean up a stranded head. The focused `056a:033b`
fixture now requires two GET 8 callbacks before any enter or disconnect. Host
`72a83e26774db01425594d99945712dfbda7e4010bfcb088d93e453d9dfe2786`
and emulator
`d675aee258df5ecf0a5af30e2b77cafd2076e449e77f44906c1e8b30453f5020`
completed that hardware run with balanced Pen/Pad removal, terminal
`f15, f10`, `oom=0`, and no host `ERR` or failure marker.

The compatibility layer provides selective nested devres groups, power-of-two
byte/record kfifo storage, the common delayed-work deadline list, the timer
bridge, and reduced LED and power-supply glue. No new task was added. Firmware
power-supply unregister frees directly, so ordinary and AES battery callbacks
serialize with report parsing through nonblocking lock/requeue. Receiver work
snapshots PID under the monitor parser lock. It synchronously cancels both
sibling initialization callbacks before releasing either dynamic resource
graph, then performs the longer rebuild without holding the monitor lock.

The captured `0317` PTH-851 family descriptor repeats one explicitly declared
vendor usage across FEATURE reports as large as 265 values. The existing
exact-ID port quirk now also applies to `0314/0315/0317`: it preserves every
report value and wire byte but stores one explicit usage instead of hundreds
of duplicate mapping entries. On that capture, reused by the `0314/0315`
emulator profiles, it removes 626 duplicate usage/priority pairs, about 20 KiB.
The focused run measured 30,696 B free after Pro Pen probe instead of 10,688 B
before compaction; this allowed the Finger graph to complete without reducing
the retained `HID_MAX_USAGES=675` policy.

Receiver rebind repeatedly executes `hid_hw_stop()` followed by
`hid_hw_start()`. The field-ordering graph belongs to the parsed `hid_report`
and remains valid across that cycle because Wacom reuses the same report and
field topology. `hid_report_process_ordering()` returns when `field_entries`
already exists, and `hid_free_report()` releases it with the report. A real
allocation failure leaves the pointer `NULL`, preserving Linux's nonfatal
descriptor-order fallback and allowing a later connect to retry. The
2026-09-10 temporary-64 fixture exercised this guard through 18 logical
stop/start generations with balanced input lifetimes and no cumulative heap
retention.

If initial receiver-monitor probe queued `init_work` and a later
`hid_hw_open()` fails, the common failure path synchronously cancels that
callback before devres releases `struct wacom`. Reversible `usbhid_start()`
checks physical disconnect both before and after buffer allocation, reopens
the transport only for the same live interface generation, and preserves
`-ENODEV`/`-ENOMEM`. The newer upstream failure shape routes every error after
a successful `hid_hw_start()` through `hid_hw_stop()` before releasing devres.
The focused receiver-lifecycle pair, temporary-hook host
`068194dfbef409bcd96cfc8cd6a3543a43a3324ccb89256c4d663c17f27776ce`
and emulator
`e071014954af3897bd5d3f73fb8dece373a918b4e88df9aea20b27e8acdac289`,
forced the `033c` touch child through that common label after successful input
registration. Its `f13` marker and absence of an interface-1 completion proved
both the second child's local stop and the worker's additional stop of the
already-started first child. The LED, Bamboo-rejection, and monitor-open callers
share the exact upstream label but were not fault-injected independently;
Remote was compile-gated in that historical image.

Receiver rebind also clears `stylus_in_proximity` and `touch_down` while both
child report parsers are fenced. Two independent `0027` logical rebinds left
Pen proximity and then touch-down asserted across the old generation. Active
Finger input after the first rebind and active Pen input after the second
proved both resets before terminal `f14, f10`. Logical-unpair free heap
stabilized at 40,216 bytes, minimum free heap was 26,088 bytes, every snapshot
reported `oom=0`, and every task watermark remained nonzero. No cumulative
child-graph retention, host `ERR`, or input-drop marker appeared.
The temporary failure and polling hooks were then removed. The hook-free
production rebuild is `text/data/bss=605504/788/245412`, UF2 SHA-256
`c9464030c8b061450825ae9c26dc2c1f6f6b08a929ee5c4307f6410638b5e06c`;
it was not flashed unchanged, so the runtime verdict belongs to the exact
temporary-hook pair rather than being transferred to that production image.

The same source update imports upstream's empty-FIFO guard, checked
`kfifo_in()` result, `GFP_ATOMIC` flush allocation, and scoped temporary-buffer
cleanup. Receiver child `0027` does not set `WACOM_QUIRK_TOOLSERIAL`, and the
public USB transport cannot demonstrably deliver a callback report larger than
the allocated report buffer through this fixture. Those ToolSerial branches
therefore remain source-audited; ordinary receiver input is not presented as
their hardware coverage.

The wired five-profile artifact, later AES/receiver artifact, focused
eleven-ID Intuos artifact, and focused Cintiq 13HD artifact are separate
hardware results recorded in
`hid-emulator-coverage.md`. The wired base run completed 23 physical
attachments and 46 balanced input lifetimes with no host `ERR` and `oom=0` in
all 115 heap snapshots. The AES/receiver run completed four AES and four
receiver attachments, 20 balanced input lifetimes, pair/unpair/re-pair,
pending initial sibling work cancellation, held rebind/teardown controls,
physical disconnect, and recovery. All 47 heap snapshots reported `oom=0`,
terminal physical removal returned to the established 60,496/60,752-byte
plateaus, and every task watermark remained nonzero.

Production firmware does not log detached power-supply values, so none of
these runs proves exact `PRESENT`, status, capacity, or event ordering. PTH
and receiver pending-disconnect cases also cannot prove from a device-side USB
acknowledgement alone that the report task had already queued battery work.
The AES fixture does not wait for the real 30-minute expiry. The receiver
fixture deterministically proves cancellation while original sibling
`init_work` is pending and detects a stale callback through duplicate control
traffic, but cannot externally hold the short pre-PID callback after promotion
or during execution. Generic promotion-window cancellation, simultaneous
synchronous cancelers, callback self-requeue, queue destruction with delayed
entries, and tick wrap therefore remain static contract-audit results.

The retained input/devres layer restores pinned Linux's
`void devm_release_action()`, `devres_destroy()` `0/-ENOENT`, and separate
allocation/unregister resources for managed inputs. The generic evdev client
uses `input_dev->id` and leaves Wacom's opaque `struct wacom *` driver data
untouched. The separate Rapoo managed extra-input regression passed in the
2026-09-22 matrix.

Those historical images excluded Wacom product IDs outside their exact table.
The current candidate instead uses the explicit unfinished-profile quirk list
and final USB wildcard described above. Bluetooth, bootloader, I2C, PCI, Linux
LED/sysfs presentation, and Remote mode/unpair sysfs remain excluded. Current
receiver lookup accepts only an active exact fixed child PID. The historical
receiver verdict covered only `056a:0027`; the 2026-09-10 matrix later covered
`0027/0029/033b/033c/0302/030e`. Other child PIDs remain unqualified.

The focused Intuos matrix verifies mode and Pen/Pad smoke for all eleven
selected PIDs, Pro LED initialization for `0314/0315/0317`, and Finger smoke
for seven touch-capable profiles, with 29 balanced input lifetimes, `oom=0`,
and terminal success. It uses family captures rather than byte-exact retail
descriptors for every PID.
That historical verdict does not include report-task parsing or CTRL-head
retirement for the non-waiting `INTUOSHT2` Feature GET 8 described above.
Representative `0302/0314/033b` battery IN completions are device-side only;
host work enqueue and detached values remain unobserved. This matrix does not
add pending/running-work cancellation, same-PID reconnect, Touch Ring
semantics or explicit arbitration to the older matrix. Receiver-child `033b`
was covered separately by the 2026-09-10 matrix.

Exact USB ExpressKey Remote `056a:0331` is active through the pinned dynamic
five-child runtime. Firmware replaces the report/worker spinlock regions with
one PI mutex, runs Remote work on the lifecycle owner, and activates each late
evdev child after its input/devres graph is complete. The focused fixture
covered pair/unpair/replacement, all five slots, all 18 upstream button bits,
ring/mode input, the real 21-second battery-expiry interval, FIFO/work
pressure, physical disconnect, and reconnect. Its 26 Remote Pad additions had
26 removals and a peak of five; `oom=0`, heap plateaus, and all task watermarks
were stable. Exact power snapshots, FIFO-full, the ninth power-member failure,
and Linux mode/unpair presentation remain outside the verdict.

The focused `056a:0304` fixture completed three physical generations and six
balanced Pen/Pad lifetimes. Its device-side oracle accepted cancellation before
the initialization deadline, then exact Feature SET/GET with value 2 in two
fresh generations. Pen enter/move/exit, serial `0x12345678`, all nine Pad
buttons, and same-PID reconnect reached terminal `f1, f2, f3, f10` without
`f12`, host `ERR`, `HID_REPORT_SKIP`, or OOM. The 91-byte report descriptor is
protocol-equivalent because no retail `0304` capture is available; Touch,
Touch Ring, and `ABS_WHEEL` remain outside this verdict.

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
timer bridge. The delayed-work API is now active for Wacom, but a line-by-line
USB-only reachability audit still shows no linked Magic Mouse caller: USB Magic
Mouse 2 returns after `hid_hw_start()`, and the USB Trackpad 2 path can reach
the mode SET but the upstream delayed retry condition selects Magic Mouse 2
only. The three unreachable upstream delayed-work calls remain commented
beside that driver-specific boundary. Bluetooth and legacy IDs likewise remain
visible but disabled.
Generic HID battery strength is active through the reduced detached-snapshot
boundary. The retained Magic Mouse/Trackpad timer performs the pinned lookup
and uses the firmware-selected 90-second repeat interval; this does not add the
full Linux power-supply presentation subsystem.

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

## Retained ELAN USB Source

Full pinned `hid-elan.c` remains in the tree, but its CMake entry and
`CONFIG_HID_ELAN` gate are inactive. The retained source contains wired USB
`04f3:074d/0755`, synchronous five-byte Feature SET/GET initialization,
managed five-slot MT input, raw `0x81/0x82/0x83` parsing, final multitouch SET,
and the upstream I2C rows. It is source-audited only; no active-driver, build,
mute-LED, or hardware claim is made.

## Active LetSketch USB Boundary

Full `hid-letsketch.c` is linked for wired USB `6161:4d15`. The port retains
the upstream interface-0 gate, 255-read string initialization, separate Tablet
and Pad inputs, raw Pen/Pad parser, and 100-ms synthetic out-of-range timer.
It also includes upstream commit `46c8beeccd8a` so removal stops report input
before permanently shutting down the timer. A CDC-free HID fixture covers the
request failures, parse failure, input branches, pending-timer disconnect, and
same-PID reconnect. Its complete hardware run passed on 2026-09-17 with
balanced lifetimes, stable detached heap, nonzero task watermarks, and no OOM
or host error.

## Active ALPS USB Boundary

Full pinned `hid-alps.c` is linked for wired USB `044e:120b/120c/1215/121e`.
It retains the upstream U1/T4 Feature GET/SET initialization and raw touch,
button, and DualPoint-stick parsing. Two genuinely unaligned little-endian
loads use the compatibility helpers; the secondary stick input is managed so
the reduced device core releases it on disconnect. A CDC-free fixture passed
the malformed-T4-reply, U1, repeated DualPoint, repeated T4, input, reconnect,
and teardown matrix on 2026-09-18. The descriptors are protocol-equivalent;
the `121e` same-path alias remains source-audited rather than separately run.

## Active Corsair and Cougar USB Boundary

Pinned `hid-corsair.c` is linked for Glaive `1b1c:1b34` and Scimitar Pro
`1b1c:1b3e`, whose Consumer interface needs the exact upstream descriptor
repair, and for the K70/K70 RAPIDFIRE row `1b1c:1b09`, whose driver-table data
is zero. K70 reuses the already-linked Corsair callbacks and remains
source-audited rather than separately enumerated; its extra-key usages are not
a hardware-pass claim.
K90 input mapping is active for G1--G18, profile, and record keys. Firmware
does not change the keyboard's current macro mode. The upstream vendor-control,
LED, and sysfs implementation remains visible but compile-disabled.

Pinned `hid-cougar.c` is linked for 500K `060b:500a` and 700K `060b:700a`.
Its three-interface path retains the upstream mouse usage-count repair and
routes vendor G-keys through the sibling keyboard input. Firmware initializes
the compatibility mutex before probe and registers the shared cleanup action
after releasing that mutex. The 2026-09-18 CDC-free run passed both probe
orders, routing, reconnect, and target teardown with no OOM. Its initial `f1`
alert marker was sent without the later activation delay and was absent,
consistent with startup draining, while all target events and the remaining
ordered markers through `f10` were present.

## Active ASUS Wired USB Boundary

The complete pinned `hid-asus.c` is linked for nine wired USB rows. Claymore
II `0b05:196b` retains ordered Feature handshakes, ASUS/MS mappings, AURA
filters, and its exact sleep-packet filter. The zero-quirk XGM 2022/2023,
AK1D, MD-5110/5112, and T101HA rows use that already active common driver
graph. G752 `0b05:1822` additionally repairs its exact 75-byte descriptor;
Medion E1239T `048d:ce50` additionally uses interface-1 multi-input touchpad,
toggle-key, synthesized mute, and multitouch-start paths.

T100 rows remain gated because their shared USB IDs require unavailable DMI
identity to select the correct touchpad geometry. The nine backlight/NKEY/Ally
rows remain gated pending their WMI, DMI, listener, and locking services; I2C
and Bluetooth rows remain outside the wired scope. Matching special-driver
rows use the same active boundary, so excluded devices retain generic
fallback. The expanded CDC-free Claymore/G752/Medion/AK1D matrix passed on
hardware on 2026-09-18: all required markers, translated events, rejection
intervals, and 15 target lifetimes matched, with `oom=0` and nonzero task
watermarks. The remaining zero-quirk aliases are source-audited common-path
rows and were not enumerated individually.

## Native HID-BPF and Rakk Support

The tree retains the earlier 17 upstream HID-BPF programs as native callbacks
and the ordinary `hid-rakk.c` driver. Those 18 source entries remain commented
out in the default CMake build and can be enabled individually when their
device support is needed. Together they cover these 22 wired VID:PIDs:

| Family | Retained USB IDs |
| --- | --- |
| Rakk Dasig X direct / receiver | `248a:fb01/fa02` |
| Mistel MD770, Trust/Philips SPK6327, IOGEAR MMOmentum | `04d9:0339`, `145f:024b`, `258a:0027` |
| Huion Dial2, Inspiroy2 S/M, K20, Kamvas Pro19/27, Kamvas13/16 Gen3, Frego | `256c:0060/0066/0067/0069/006b/006c/2008/2009/2012` |
| XP-Pen ACK05, Artist24, ArtistPro14/16/19 Gen2, Deco01V3/02/Mini4 | `28bd:0202/093a/095a/095b/096a/0947/0803/0929` |

Upstream predicates, descriptor replacements, event logic, and tables remain;
native glue replaces the BPF loader/maps and gives each HID its own mutable
state. Huion mode selection uses standard USB string requests through the
existing task-owned transport; ACK05 uses its existing interrupt-OUT and
delayed-work contracts. There is no BPF VM, dynamic attachment, new bus, or
proxy. Frego/Rakk Bluetooth rows remain commented out; Artist24 Pro `092d`
stays with the existing UC-Logic driver rather than receiving a second fixup.
An instrumented build passed the 44-generation, no-CDC host-path matrix in two
separate hardware runs whose retained captures overlap at generation 14. The
temporary observer was removed afterward,
and the default production build excludes these sources. This qualifies the
selected synthetic executable paths, not every retail-device behavior; source
revisions and coverage limits are recorded in `upstream-porting-audit.md` and
`hid-emulator-coverage.md`.

The ArtPen and WALTOP programs are separate retained additions whose source
entries are commented out beside the earlier programs. The native adapter
source, forward declaration, device state, and active hook declarations are
also retained as comments; upstream `!CONFIG_HID_BPF` stubs preserve the
ordinary HID call graph. This is implemented and hardware-qualified support,
not unfinished work: it is disabled because no currently selected production
device needs these native HID-BPF corrections. If ArtPen, WALTOP, or another
retained device is needed, restore the commented adapter declarations/state
and CMake source, disable the adjacent no-op stubs, and uncomment only the
required program source. Their native per-attachment state requires the loader
to make private storage available before `probe()`, matching Linux BPF data-map
lifetime. The 2026-09-22 enabled-source matrix passed exact pressure/tilt/button
values, fresh-state handling, and size/PID/report controls through input/evdev;
it does not qualify every retail-device behavior or the default production image.

## Future Transport Work

Drivers beyond the current boundary need extensions to the existing
sync-over-async transport:

- add request-specific URB submit/kill/resubmit ownership
- add synchronous interrupt-IN without stealing continuous HID polling
- add any request-specific transfer contract only for an audited linked user
- retain generation-based cancellation on unmount and fast replug

The Linux-shaped caller may block only in a task; TinyUSB callbacks must remain
bounded publishers.

## Deferred Dependency Examples

The decision register above supersedes a broad family-level allowlist.
Synchronous raw GET/SET by itself is no longer a transport blocker:

- the non-FF keyboard/mouse part of `hid-lg.c` is active; its 3Dconnexion
  descriptor paths remain deferred, while `hid-lg*ff.c` stays in the excluded
  controller/FF scope;
- remaining `hid-logitech-hidpp.c` force-feedback, headset, Bluetooth, and
  legacy proxy/27 MHz classes stay outside the active USB/DJ boundary;
- `hid-ntrig.c` still needs a focused USB control-message firmware/query audit;
- controller portions of `hid-sony.c`, `hid-nintendo.c`, and
  `hid-playstation.c` remain excluded even though their request/work paths
  could be ported.

For any reopened row, record the exact upstream behavior, dependencies,
memory cost, and hardware/emulator evidence. Do not infer support merely from
generic HID fallback or from a source file being present in the tree.
