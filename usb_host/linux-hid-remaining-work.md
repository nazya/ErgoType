# Linux HID USB Remaining Plan

This file contains only unfinished implementation, qualification, integration,
and cleanup work for the wired USB HID host. Completed implementation belongs
in Git history. Hardware evidence belongs in
`hid-emulator-coverage.md` and remains tied to the exact host/emulator
artifacts which produced it.

The product is an advanced wired USB remapper for useful keyboards, mice,
presenters, touchpads, drawing tablets, pads, dials, sensors, and applicable
USB receivers. Bluetooth, HIDDEV/VFS, game-controller and force-feedback work
without a selected product consumer, and rare obsolete devices are outside
this roadmap. HID proxy work remains paused until a new explicit request.

Pinned Linux `83f1454877cc292b88baf13c829c16ce6937d120` is the source of
truth. Preserve source order, branches, names, return semantics, and inline
logic. Keep unsupported upstream calls visible beside the firmware boundary
and put port-specific execution in glue.

## Current factual boundary

- The production RP2040 parser policy is `HID_MAX_FIELDS=64` and
  `HID_MAX_USAGES=675`, versus Linux's larger tables. These are explicit
  retained-capacity limits, not runtime-learning or sparse-parser policy.
- Native HID-BPF/Rakk sources which passed their synthetic matrix remain
  retained but commented out in the default CMake build. ALPS and LetSketch
  are retained default-off the same way. The planned default build keeps only
  `hid-generic`.
- Active Logitech HID++/DJ covers ordinary Unifying plus selected
  gaming/Lightspeed/Powerplay paths. The complete expansion matrix passed at
  temporary `64/256`; its synthetic `c539` child exhausted heap at
  production `64/675`.
- Logitech `c52f/c534`, Lenovo `60ee`, and Huawei CD30 remain inactive on
  RP2040 because their complete retained graphs exceed the measured heap.
  Their source stays available for a larger-memory profile. The complete
  `60ee` driver graph already passed on RP2040 at temporary `64/256`; the
  larger-memory requirement is specifically for retaining the production
  `64/675` parser policy.
- The Wacom production boundary keeps the qualified fixed profiles, explicit
  unfinished-profile generic fallback, and the final wired USB wildcard.
  Wildcard descriptors requiring `WACOM_HID_WD_MODE_CHANGE` are rejected
  before bind because the paired-sibling runtime is not qualified.
- The reduced 32-bit millisecond time comparison has a 49.7-day rollover
  boundary. Reboot within that period is the accepted constraint; this is not
  an active implementation stage.
- HIDRAW remains lifecycle-only. It has no VFS, report subscriber, descriptor
  API, ioctl, poll, or device node. Extend it only for a selected bounded
  firmware consumer.
- The final marker-free source selection after the latest input batch has no
  documented production build artifact in the current checkout.

## Parser capacity policy

Keep the pinned Linux report representation and use build-time field/usage
limits. A bounded profile supports only retained values inside those limits:
preserve the full wire layout, make input drops visible, reject a field when a
retained prefix is unsafe, and never claim complete coverage of an omitted
tail.

A larger-RAM or future PSRAM-backed profile may raise the same limits toward
Linux's `256/12288`. That exact image still requires its own descriptor,
lifecycle, reconnect, heap, and stack qualification. Do not reduce production
limits silently to obtain a passing fixture.

Exact VID/PID compaction is allowed only when a descriptor and the pinned call
graph prove the omitted mappings redundant while report IDs, wire lengths, and
raw bytes remain unchanged. Do not generalize an exact quirk into a new parser.

## Stage 0: establish the current production artifact

This stage changes no runtime behavior.

1. Inspect the complete working-tree diff.
2. Build only with `cmake --build build -j4`.
3. Record UF2 SHA-256, text/data/bss, `__bss_end__`, and main-bank headroom
   (bytes from `__bss_end__` to `0x20040000`) in `hid-emulator-coverage.md`
   next to the run that uses that image.
4. Run `git diff --check`.

Compilation establishes the artifact for the next test; it is not a runtime
verdict.

## Stage 1: retained-policy `c532` smoke

Run one focused CDC-free sequence for Logitech receiver `046d:c532` at
`64/675`: DJ startup, one ordinary virtual mouse child, input,
unpair/reconnect, balanced teardown, stable heap, and `oom=0`. Its execution
graph passed at temporary `64/256`. Lenovo `6009/6047` need no limit-only pass
because their descriptors stay below 256 usages.

Do not include `c52f/c534/60ee` in the automatic success path. Those rows are
intentionally memory-gated.

Use public TinyUSB descriptors and callbacks. Emulator CDC is unavailable as
an oracle. Make progress and terminal verdicts visible through the main host
log. Do not put calls with side effects in `assert()` or `configASSERT()`.
Record a verdict only for the exact host/emulator hashes which ran.

## Stage 2: common transport and parser evidence

Completed evidence remains in
[`hid-emulator-coverage.md`](hid-emulator-coverage.md); this section is the
single checklist for unfinished common qualification.

- Verify generic probe control ordering: exactly one SET_IDLE before report-
  descriptor GET, a SET_IDLE STALL remains nonfatal, and generic probe emits no
  SET_PROTOCOL. This is separate from the driver-level `.idle` call reached
  only through the unfinished PM lifecycle below.
- Extend the long-configuration fixture with REMOVE and a foreign ATTACH while
  its 100-ms retry deadline is armed. Exact 512/513/4096-byte configurations,
  4097-byte and malformed-header rejection, and the ordinary retry already
  passed.
- Exercise a short full-configuration read above 512 bytes and a deterministic
  transient allocation failure. These need a bounded host hook or internal
  harness; an ordinary USB emulator cannot force allocator failure reliably.
- Map the remaining `usbhid_parse()`/`hid_open_report()` error labels against
  existing evidence before adding fixtures. Add a fixture only for an
  unreached cleanup transition: a descriptor GET shorter than its declared
  size, malformed descriptor, temporary-buffer or parser allocation failure,
  or `HID_MAX_FIELDS` exhaustion. The 4096-byte descriptor, Kamvas Pro 19
  oversized INPUT ARRAY, Logitech persistent-field limits, and several
  malformed vendor descriptors already ran.
- Inject an accepted `-EIO` result for the USB Magic Trackpad 2 mode SET.
  Disconnect while that SET is pending is already covered; contact ID 32 is a
  generic multitouch boundary and does not need another device-specific run.
- Inject deterministic control failures for the standard haptic-touchpad path.
  A real physical haptic touchpad is also still uncovered.
- Exercise interrupt-IN FAILED/TIMEOUT retry. A normal TinyUSB NAK does not
  prove it; this needs a lower-level no-response or bad-packet injector.
- Unplug and natively replug on the same root port during every terminal-reset
  phase. Root clear-halt recovery already passed; hub-child, parent-hub, and
  USB-address-reuse variants belong to the common-hub gate below.
- If exact power values become part of the product claim, add a bounded
  task-context oracle for `ADDED`/`CHANGED` snapshots and a deterministic
  startup-failure case. Queue lifetime itself is mandatory and belongs to the
  separate stage below.
- Keep delayed-work promotion-window cancellation, simultaneous synchronous
  cancellation, callback self-requeue, delayed-queue destruction, and tick
  rollover as source-audited cases unless a dedicated internal harness is
  selected. Existing fixtures already cover the reachable pending, running,
  rearmed, and teardown graphs of their real callers.

The first-interrupt-pair endpoint selection is already hardware-proved; do not
add another fixture for it. A device whose code path is already exercised by
another qualified device needs no separate run. Direct USB `046d:c08d` HID++
passed two full hardware runs, and MX Vertical `c08a` uses the same
input/HID++/battery graph, so neither needs another stage.

## Stage 3: USB suspend and resume

The production boundary is `usbhid_pm_set_target(state, remote_wakeup)`: one
last-wins desired state, stored before notifying the existing lifecycle task.
There is no new queue, task, or periodic policy poll. A target written before
host initialization is retained. An ACTIVE target arriving during suspend is
handled after that transition completes; it is not discarded as busy.
`usbhid_pm_state_changed_cb()` runs in lifecycle context. Its state is the
transition's requested target and its status is the result; an overwritten
mailbox write does not receive a separate completion.
`usbhid_pm_wakeup_cb()` reports an actual device wake request in the same
lifecycle context, independently of whether interface resume succeeds.

The explicit single-transition API `usbhid_pm_request()` remains available for
ordinary suspend and resume of a direct root-port device whose bound interfaces
all use `hid-generic`. The caller supplies the policy event and retains the
callback context until the lifecycle task reports completion. There is one
outstanding command: a duplicate completed state returns `-EALREADY`, while a
command overlapping an active transition returns `-EBUSY`.

The request call returns `-EINVAL` for an invalid target and `-ENODEV` before
the lifecycle owner exists. Once accepted, its completion reports the actual
transition result: `-EOPNOTSUPP` for a hub child or vendor driver, `-EBUSY` for
enumeration/reset/recovery ownership which prevents quiescing, `-ENODEV` if the
device generation disappears, or the existing transport error such as
`-ETIMEDOUT` while draining accepted I/O.

Suspend closes new interrupt, control, and output admission, aborts the armed
interrupt-IN request, and drains every control/output request accepted before
that gate, including its parser completion. The PIO HCD then stops SOF
generation. Completion is delayed for four 1 ms firmware ticks, which
guarantees the USB three-frame idle interval at any tick phase.

Resume is a PIO-host state machine rather than a delay in the TinyUSB task:

- the TX state machine holds the correct K polarity for 40 ms, matching the
  conservative Linux host timeout;
- the existing TX PIO program terminates K with a standard low-speed EOP: two
  low-speed bit times of SE0 followed by one low-speed bit time of J;
- SOF generation restarts immediately after that EOP;
- endpoint transactions remain fenced for the USB 10 ms resume-recovery
  interval, after which parked HID traffic is rearmed and the completion is
  delivered.

The PIO deadlines use unsigned microsecond subtraction, so the 32-bit timer
wrapping during either interval does not change their ordering. Disconnect can
replace any suspended or resuming physical state; lifecycle validation then
returns an error instead of reopening the detached HID generation.

EOP completion waits for the PIO output enables to release D+/D-, not for a
short-lived program-counter value. DMA completion, the EOP flag, and pin
release share one 100 us budget. A timeout disables TX, releases the pins, and
posts a DMA abort without waiting inside the IRQ. SOF and endpoint traffic
remain closed, and the PM completion returns `-ETIMEDOUT` rather than ACTIVE.
The failed port can be reused after DMA BUSY and ABORT retire and a physical
disconnect is observed; if DMA cannot retire, the IRQ still returns and the
port stays fenced. The source/native checks and the main-image hardware test
are described in [PIO resume checks](../tests/pio-usb-pm/README.md).

This requires a build-local PIO HCD extension because pinned TinyUSB and
Pico-PIO-USB expose no host-port suspend/resume operation. The signalling and
recovery intervals follow USB 2.0 section 7.1.7.7. The 40 ms K interval follows
Linux's `USB_RESUME_TIMEOUT` interoperability choice instead of using the
specification's 20 ms minimum.

The ordinary direct-root cycle passed the two-board hardware test with input
before and after sleep in the same device generation. The host-only remote
wakeup test also passed with a real full-speed ErgoType NKRO keyboard. The
current policy and remote-wakeup path implement:

- `tud_suspend_cb()` supplies SUSPENDED and the computer's wakeup permission;
  `tud_resume_cb()`, mount, and unmount supply ACTIVE;
- before policy-driven sleep, read the selected configuration's nine-byte
  header and SET/CLEAR `DEVICE_REMOTE_WAKEUP` when the device supports it;
  pinned TinyUSB selects configuration index zero;
- enable the root port's physical K rising-edge GPIO IRQ only for an armed
  keyboard: D- for full speed, D+ for low speed. The handler verifies the full
  line state and immediately takes over K, then disables the wake IRQ;
- the existing bounded resume/EOP/recovery state machine completes the bus
  transition. Its notifications replace the task-side one-tick status poll;
- retain the remote-wake override until the upstream target changes, so the
  keyboard is not immediately suspended again while its computer is waking;
- deliver the wake-request callback before waiting for downstream resume;
  production firmware defers `tud_remote_wakeup()` to the upstream USB-device
  task. TinyUSB enforces the upstream wakeup permission. Transition-result
  callbacks do not generate wake requests.

The host boundary passed with the real-keyboard fixture while upstream USB
stayed active. Its final marker required the wake-request callback,
successful resume, and restored same-generation keyboard input. The laptop's
upstream runtime-suspend test has not passed and is not a host-only verdict.
The complete keyboard → remapper → Android test passed: Android woke on a key
and both devices left their suspend indication. See the
[Android test](../tests/pio-usb-pm/README.md#android-complete-wakeup-chain-without-cdc).

GPIO input inversion and IRQ inversion are independent in RP2; the K-edge
selection uses the physical polarity, while the existing line-state helper
undoes INOVER. The IRQ is shared through a raw handler, not by replacing the
pointing sensor's GPIO callback. The timing requirements come from USB 2.0
section 7.1.7.7; see [USB-IF](https://www.usb.org/document-library/usb-20-specification)
and the [RP2 GPIO API](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_gpio).

Hubs, reset-resume, device-family PM callbacks, and MCU low-power mode remain
outside this generic path. FS/LS polarity and IRQ control flow are native-tested;
the full-speed host-initiated and real-keyboard host-only wakeup cycles are
hardware-verified, as is the complete Android wakeup chain. Low-speed hardware
and the precise signalling waveform remain unqualified.

Probe-time SET_IDLE and the ll-driver `.idle` EP0 request remain implemented.
`pm_ptr()` still removes driver callbacks from their descriptors, so the next
code stage is the common Linux driver PM dispatcher. Hotplug and terminal
recovery rebuild a device from a fresh generation and do not substitute for
that same-generation callback path.

This affects the complete imported driver tables, not only sources selected in
the default image:

- `hid-generic` restores input resolution multipliers after reset;
- `hid-multitouch` selects its suspend reporting mode, releases contacts after
  reset, restores normal reporting, and sends the driver-level SET_IDLE. Its
  scope is the complete imported USB multitouch table and the generic
  multitouch/Windows 8 wildcards. Upstream specifically needs or has tested the
  resume SET_IDLE on legacy Elan, 3M, Stantum, Cypress, Zytronic, and eGalax
  panels;
- `hid-alps` restores U1/T4 modes;
- `hid-asus` restores multitouch mode and, for the broader gated table,
  keyboard backlight state;
- `hid-lenovo` restores TrackPoint settings for the Compact USB Keyboard and
  TrackPoint Keyboard II paths;
- `hid-logitech-dj` switches a receiver back to DJ mode;
- `hid-uclogic` reinitializes tablet parameters;
- `wacom` restores Wacom mode and LED state.

The imported Sony controller PM block remains separately gated with the
controller scope. Do not enable it as part of wired keyboard/mouse PM.

Continue PM as one host-layer contract rather than defining `CONFIG_PM` in
isolation:

1. Qualify the generic direct-root path with suspend/resume of the same device
   generation, pending control/output requests, and disconnect during idle,
   K-signalling, and recovery.
2. Dispatch ordinary driver suspend/resume callbacks in Linux order, propagate
   their real return values, and release pressed input state where upstream
   requires it.
3. Add reset-resume and restore required device modes and LED/output state.
4. Cover denied upstream wake permission and disconnect during suspend.
   Direct-root wake-on-key with an Android host has passed.
5. Add hub-port suspend/resume only after the common hub gate is available.
6. Test one representative for each distinct callback graph; do not test every
   VID:PID when another device executes the same graph.

## Stage 4: power-supply queue lifetime

`power_supply_unregister()` publishes a terminal `REMOVED` snapshot, but only
the UI task currently removes and deletes that queue. If the UI task is absent
or not consuming power events, detached queues remain members of the QueueSet;
when active and detached members together reach eight, later battery devices
fail admission with `POWER_SUPPLY_LIMIT`.

Make terminal queue ownership independent of UI availability while preserving
delivery to a running consumer. The design must prevent deletion while the UI
has selected the queue, serialize terminal `REMOVED` against pending
`changed_work`, and release each QueueSet member exactly once. Qualify repeated
register/change/unregister both with no UI consumer and with concurrent UI
consumption, including allocation/admission failure and disconnect during
pending battery work.

## Deferred common USB hub gate

A real common hub is required to qualify hub-child terminal recovery, child
removal, parent-hub removal, USB-address reuse, and overlapping retiring cache
generations. One Pico presenting one composite USB device cannot prove
physical-parent matching, sibling registry lifetime, both arrival orders, or
overlapping generations.

Do not enable paired `056a:0333/0335`, paired `032b/032c`, or the captured
`0354/0350` mode-change runtime before this topology exists.

First qualify fixed `0333/0335`:

- Pen-first and Touch-first arrival;
- pen/touch arbitration;
- sibling disconnect in either order and parent removal;
- address reuse, same-PID reconnect, and overlapping retiring generations;
- balanced physical/input lifetimes, stable terminal heap, `oom=0`, and
  nonzero watermarks.

Then qualify `032b/032c` after the sensor path with simultaneous
Pen/Pad/Sensor/Touch input and the same lifetime matrix.

Only after that, implement and qualify descriptor-driven mode change:

- lifecycle-owned work which releases Wacom resources, stops transport, and
  rebuilds Pen before Touch in pinned order;
- report parsing fenced across both sibling generations;
- synchronous cancellation before either object can be released;
- preserved upstream early-return and partial-restart behavior;
- coalescing/requeue so the final hardware mode is represented;
- disconnect at each rebuild boundary and deterministic partial failures.

Do not invent transactional rollback or a single-device approximation. Remove
all temporary synchronization hooks before a production build.

## Not ported yet

The port plan lives in the public note that the table links to:

- ELO touchscreens `04e7:0009/0030`: `docs/hid-host/vendors/various.md#elo-touchscreens`.
  The `elo_broken_firmware()` sibling scan needs the common USB hub gate.

## Larger-memory qualification

The following are not RP2040 enablement tasks:

- Logitech `c52f/c534`;
- Lenovo `60ee`;
- the complete synthetic `c539` child;
- Huawei CD30;
- full-retail S510 ranges which exceed the retained fixture capacity;
- Kamvas Pro19 binding which needs at least 1387 retained usages;
- the six captured Wacom Pen+Touch composites `056a:03c0/03c4/03cb/03d0/
  03ec/03f0`, whose two simultaneous HID graphs plus device state exceed the
  RP2040 heap even at a near-minimal usage cap (their Pen and Touch interfaces
  ran only as standalone profiles; the composite fixtures on
  `device/wacom-one-13-03cb`, `device/wacom-movink-03f0`,
  `device/wacom-cintiq-pro-03c0-03c4-03d0` and `device/wacom-dth134-dtc121`
  have no hardware verdict);
- the complete HID++ eQuad keyboard child and broader real-device DJ
  combinations.

Select a concrete larger-RAM/PSRAM target first. Raise the existing parser
limits explicitly, then qualify each exact image. Do not transfer temporary
`64/256`, `64/64`, or `64/32` RP2040 results to that target.

## Public support tables

1. Keep `docs/hid-host/vendors/`, `docs/hid-host/linux-drivers.md` and
   the scope section of `docs/hid-host/README.md` in step with any later driver, gate or pin change.
2. Switch the default build to `hid-generic` only, then build the complete
   documented Lenovo enable path once: `hid-lenovo.c`, `CONFIG_HID_LENOVO`,
   `CONFIG_HID_LENOVO_ALL_DEVICES`, and `CONFIG_LEDS_CLASS`.

## Later architecture

- The workqueue, timer, and HID lifecycle split costs three FreeRTOS tasks.
  Consolidating them is a separate architecture change, not part of driver
  qualification.

## Final audit and documentation

After the outstanding stages actually run:

1. Compare every changed imported block with pinned Linux and verify table
   order, branches, names, and return values.
2. Keep every port replacement adjacent to the original upstream block with a
   short accurate reason.
3. Audit active compatibility primitives against all real callers. Remove
   only checks proved unreachable; retain allocation failures and uncertain
   branches.
4. Keep compatibility contracts in headers and scheduling/ownership in glue.
5. Review complete host and emulator diffs manually and rerun only regressions
   touched by common-layer changes.
6. Record exact artifacts, log markers, heap plateaus, OOM count, and stack
   watermarks. Never transfer evidence to a rebuilt image.
7. Remove temporary diagnostics and fixture-only host code before production
   commits.
8. Update tracked documentation only with implemented and observed facts.
9. Rewrite or combine checkpoint history only with explicit user permission.

## Rules for every stage

- Before runtime changes, state the missing contract, new behavior, and
  reachable race/object-lifetime risks.
- Preserve Linux return semantics for real callers.
- Do not add speculative defensive checks or remove a branch whose
  unreachability is not proved by the current call graph.
- Keep Linux parsers in imported files. TinyUSB, FreeRTOS, lifecycle, clocks,
  and output policy belong in firmware glue.
- Build in the normal directories only.
- Use one focused emulator for newly reachable behavior, with programmatic
  disconnect/reconnect and host-visible verdicts.
- A build is not hardware verification.
- Do not commit, amend, rebase, or push without explicit authorization.

## Explicitly outside this roadmap

- Bluetooth and Bluetooth proxy;
- game controllers and force feedback without a selected remapper consumer;
- HIDDEV/VFS and bootloader HIDRAW workflows without a concrete consumer;
- rare obsolete devices;
- 3D mice (`docs/hid-host/README.md#3d-mice`) until KeyD has a six-axis
  consumer. If resumed: SpaceNavigator `046d:c626` and SpaceTraveller `c623`
  declare absolute cap positions as relative. Split the `#if 0` blocks in
  `hid-lg.c` (rel/abs fix at bytes 32/33 and 49/50, both rows) from the gated
  wheel/joystick code without reordering upstream, and import
  `Logitech__SpaceNavigator.bpf.c` (`c626` only; probe accepts descriptor
  sizes 202/217/228 and skips an already absolute descriptor; fixup checks
  bytes 32/36/49/53). The static BPF probe/rdesc graph is already exercised.
  Test a 32/49 descriptor and a 228-byte 36/53 descriptor. With the fix, KeyD
  maps ABS_X/Y to an absolute cursor position and drops Z/RX/RY/RZ;
- Linux sysfs/uevent presentation without a firmware-facing consumer;
- resumed HID proxy work without a new explicit request.
