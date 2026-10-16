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
  Their source stays available for a larger-memory profile.
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

The current list lives under "Remaining transport and parser qualification"
in `hid-emulator-coverage.md`:

- one SET_IDLE before report-descriptor GET, nonfatal SET_IDLE STALL, and no
  SET_PROTOCOL from generic probe;
- REMOVE and a foreign ATTACH while the enumeration retry deadline is armed;
- a short full-configuration read above 512 bytes and a deterministic
  transient allocation failure, which need a bounded host hook or internal
  harness;
- report-descriptor and parser faults, narrowed first: map the remaining
  `usbhid_parse()`/`hid_open_report()` error labels against existing logs and
  add a fixture only for an unreached cleanup transition;
- accepted `-EIO` for the USB Magic Trackpad 2 mode SET; disconnect during that
  SET is already covered, and contact ID 32 belongs to the generic
  `hid-multitouch` boundary;
- delayed-work promotion-window, simultaneous synchronous cancel, callback
  self-requeue, queue destruction, and tick rollover stay source-audited
  unless a dedicated internal harness is selected.

The first-interrupt-pair endpoint selection is already hardware-proved; do not
add another fixture for it. A device whose code path is already exercised by
another qualified device needs no separate run. Direct USB `046d:c08d` HID++
passed two full hardware runs, and MX Vertical `c08a` uses the same
input/HID++/battery graph, so neither needs another stage.

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
2. Switch the default build to `hid-generic` only, then build the
   `CONFIG_HID_LENOVO_ALL_DEVICES` plus `CONFIG_LEDS_CLASS` combination once to
   confirm the documented Lenovo enable path.

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
