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
pinned-upstream-shaped `hid-logitech-dj.c` port. Runtime matching enables
`046d:c52b/c532`. The adjacent upstream `c52f` mouse-only and `c534` HID++ rows
remain intact behind `CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS`: at the retained
`HID_MAX_USAGES=675`, their physical 652-usage Consumer field requests 20,980
bytes and their 675-entry virtual-child field requests 21,716 bytes. Those two
persistent allocations alone total 42,696 bytes and do not fit the RP2040 heap
with the receiver graph. Gaming, Lightspeed/Powerplay, legacy 27 MHz,
Bluetooth-proxy, and Dinovo rows remain behind the same broader gate. The
active port retains the upstream multi-slot virtual-child model, standard
mouse/keyboard/Consumer/power/media descriptors, HID++ descriptors, and
virtual-child raw-request routing through the physical receiver. Firmware glue
supplies the task-owned work/lifecycle boundary and final evdev activation.
The exact `c52f/c534` startup, child-input, and teardown logic passed on
hardware at temporary `64/256`; that capacity checkpoint is not support at the
retained policy. Existing `c52b` coverage remains valid; the selected `c532`
exact retained-policy regression remains pending. Because the IDs are absent only from the special-driver table,
Linux-style generic fallback may still publish their physical interfaces; it
sends no DJ/HID++ startup sequence and creates no virtual receiver child.

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

The current build links the complete pinned `wacom_sys.c` and `wacom_wac.c`
implementation while keeping a narrow USB-only match table:

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
`hid_hw_start()`. The port releases the connect-lifetime HID field-ordering
graph only after the low-level transport has synchronously stopped all report
producers; the next connect rebuilds it for the new Wacom profile. Allocation
failure keeps Linux's nonfatal descriptor-order fallback and does not change
the return value of `hid_connect()`.

If initial receiver-monitor probe queued `init_work` and a later
`hid_hw_open()` fails, the common failure path synchronously cancels that
callback before devres releases `struct wacom`. Reversible `usbhid_start()`
checks physical disconnect both before and after buffer allocation, reopens
the transport only for the same live interface generation, and preserves
`-ENODEV`/`-ENOMEM`. The pinned late-error shape remains: input or LED
registration failure after a successful restart releases the input resources
but leaves transport started until the next rebind or disconnect, and an
unchanged PID is not retried automatically. These error paths are
source-audited, not hardware-injected.

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
untouched. The separate Rapoo managed extra-input regression remains pending.

All other Wacom product IDs remain behind
`CONFIG_HID_WACOM_ALL_DEVICES`. Bluetooth, ExpressKey Remote, bootloader, I2C,
PCI, Linux LED/sysfs presentation, and product IDs outside the 19-entry USB
table remain excluded. Receiver lookup can select any child PID already in
that table; only child `056a:0027` is covered by the receiver hardware verdict.
The focused Intuos matrix verifies mode and Pen/Pad smoke for all eleven
selected PIDs, Pro LED initialization for `0314/0315/0317`, and Finger smoke
for seven touch-capable profiles, with 29 balanced input lifetimes, `oom=0`,
and terminal success. It uses family captures rather than byte-exact retail
descriptors for every PID.
Representative `0302/0314/033b` battery IN completions are device-side only;
host work enqueue and detached values remain unobserved. This matrix does not
add pending/running-work cancellation, same-PID reconnect, Touch Ring
semantics, explicit arbitration, or receiver-child `033b` coverage to the
older `0084 -> 0027` receiver verdict.

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
