# Upstream Porting Audit

Updated: 2026-07-17

Rules: `usb_host/upstream-porting-rules.md`.

Linux baseline: `../linux-upstream-hid` at
`83f1454877cc292b88baf13c829c16ce6937d120`.

## Result

Partial conformance. Active vendor drivers are close, traceable ports, but the
active core still has unanchored changes, several stale port substitutions, and
deliberate non-line-preserving areas. This is a porting audit, not a runtime
safety certification.

Checkpoint `hid: stabilize stadia ff teardown` preserves the tested Stadia/`ff-memless` implementation and
its teardown fixes. The active build now excludes that gaming-only path while
retaining the generic FF interfaces needed by a future haptic touchpad port.

## Scope

- active TinyUSB/FreeRTOS glue: `task.c`, `usbhid.c`, `hid_async.c`,
  `hid_workqueue.c`, `hid_timer.c`
- active HID/input boundary: `evdev.c`, `evdev_client.c`, `devmon.c`, and
  `keyd/port/device.c`
- Linux-derived HID/input C files enabled by the root CMake allowlist
- compatibility headers used by that slice

Inactive drivers were checked for build status, not certified for enablement.
In particular, hiddev, CMedia, and Vivaldi remain disabled; `vivaldi-fmap.c`
itself is byte-for-byte upstream.

## Conforming Areas

- CMake links 13 vendor drivers: a4tech, chicony, creative-sb0540, cypress,
  holtek-kbd, ite, kye, primax, pxrc, razer, rapoo, saitek, and zydacron. The
  `CONFIG_HID_*` mirror in `hid_compat.h` matches.
- Every active vendor `static const struct hid_driver` conversion keeps the
  upstream declaration and reason. Extra diffs are seven documented
  `raw_event_callback_safe` markers, the documented Holtek include removal,
  and whitespace-only cleanup in chicony/rapoo. No unrelated vendor flow
  rewrite found.
- `hid-generic.c` and `hid-quirks.c` keep changed upstream lines beside their
  explained replacements.
- `ff-core.c` remains linked for the generic FF ABI. `ff-memless.c` and the
  Stadia driver remain in tree but are not linked.
- The evdev bridge clears `EV_REP` before opening each input device because
  KeyD owns held-key state and discards Linux `EV_KEY value=2` repeats.
- AppleIR remains in tree but is not linked. The timer bridge remains for a
  future `hid-multitouch` port; the current path does not schedule it.
- `hid-drivers.c` is clearly marked as port-only linker/initcall glue, although
  its location under `linux/drivers/hid` remains an explicit exception.

## Open Porting-Rule Findings

1. `hid-core.c:2906-3006,3044-3078,3413-3513` rewrites mutable
   `struct hid_driver` state around `hid_driver_runtime`. Several signatures,
   const conversions, lock lines, and register/unregister branches do not keep
   the exact upstream line beside the replacement. This is a major structural
   exception, not a completed line-preserving port.

2. Several `hid-core.c` deviations can now return to upstream because the
   compatibility layer already supports them: `__free(kfree)` at 1492-1507,
   no-op logging at 2133-2136, and `hid_close_report()` at 3347-3350. The
   disabled `DRIVER_DESC`/module metadata at 53-57 and KUnit block in
   `hid-input.c:2520-2523` are also unnecessary diffs under the current macros.

3. Some comments are not valid upstream anchors: the invented
   `sema_destroy()` at `hid-core.c:871`, the false port-only export rationale at
   2436-2438, and the duplicate `#ifdef` at 3361. Historical WIP in
   `hid-drivers.c:41-50` is not upstream context.

4. `hid_is_usb()` was moved from upstream usbhid transport code into generic
   `hid-core.c:2418-2426` and changed from an ll-driver identity check to a bus
   check. Placement can be restored mechanically; restoring the upstream
   predicate needs behavior review.

5. `usbhid.c` correctly declares itself non-line-preserving glue, but its local
   anchors still need to be accurate: line 967 lacks the adjacent upstream
   `usb_set_intfdata(intf, hid)` example required by the rules, line 1280 does
   not preserve upstream's GET/SET switch, and line 1291 does not preserve the
   upstream `wait_event_timeout()` body.

6. `hid-input.c:1932-2083` turns resolution-multiplier setup into continuation
   functions inside an upstream file. The synchronous block is mostly visible,
   but the original call sites/final setup and overall flow are not preserved.
   Treat this as an async state-machine exception, not ordinary thin glue.

7. `input-mt.c:13` replaces three upstream includes without leaving them or a
   reason. In `ff-core.c:122-229`, the `guard()` anchors remain, but manual
   unlocks change early-return and scoped-block shape without preserving every
   changed upstream line.

8. `input.c` is a reduced in-memory input core and `evdev.c` replaces Linux
   eventX clients with the firmware queue boundary. Both are explicit,
   non-line-preserving exceptions. `task.c` is port-only glue but lacks the
   corresponding top-level provenance note.

## Fixed Runtime Boundary

- KeyD keeps the existing synchronous `evdev_writer`. One mutex serializes its
  write/upload/erase calls with `evdev_unregister_device()` across CORE0/CORE1.
- Unregister clears `client->evdev` under that mutex, sends the existing
  `DEVICE_INPUT_REMOVED` sentinel, and leaves the detached client allocated.
  KeyD consumes the sentinel, deletes the input queue, and performs the final
  client free. There is no second output queue, worker task, ID lookup, or
  removal handshake.

## Haptic Direction

- Retained: `ff-core.c`, generic evdev upload/play/stop/erase calls, the HID
  workqueue bridge, and async OUTPUT/SET_REPORT transport.
- Removed from the active build: Stadia, `ff-memless`, its automatic effect
  timer, and the KeyD layout-rumble trigger.
- A future standard haptic touchpad port needs `hid-haptic.c` plus
  `hid-multitouch.c`; it uses `input_ff_create()` directly and device-managed
  waveform timing, not `ff-memless`.
- Haptic probe still needs an async continuation for feature GET_REPORT data;
  current `hid_hw_wait()` does not provide synchronous completion.

## Open Semantic Boundaries

- No active driver currently creates an FF device. The retained generic
  evdev/`ff-core` seam must be reviewed with the real cross-core locking and
  publication order when haptic support is enabled.
- The removal sentinel does not quiesce an already-running
  `evdev_events()`/`__pass_event()` input producer. The port has no upstream
  RCU wait, and active async input-report execution is not waited by cancel;
  full client lifetime therefore still needs a separate input-side boundary.
- `port_input_dev.name` is still a pointer, although the rest of the devmon
  record is queued by value. A fast add/remove can free `hidinput->name` before
  KeyD copies the pending devmon record.

## Headers and Deferred Runtime

- `hid.h`, `input.h`, `usb.h`, `workqueue.h`, and `timer.h` are reduced
  compatibility contracts, not line-preserving upstream headers. The actual
  timer/workqueue ABI lives in port-only `hid_compat.h`.
- `hidraw.h` is instead an extended proxy contract. Its proxy declarations are
  present in this branch, but no hidraw implementation is linked and hidraw is
  not claimed.
- hiddev declarations/implementation remain in tree, but `CONFIG_USB_HIDDEV`
  and `hiddev.c` are disabled; active code uses stubs.
- Audited callback-safe `raw_event` paths are active. The generic FF seam is
  retained without an active FF driver; synchronous request/wait semantics,
  returned-data probe continuations, haptic, unaudited hooks, hidraw/hiddev
  runtime, and PIDFF remain deferred.
- The KeyD queue adapter is treated as firmware boundary glue; this audit does
  not claim a pinned upstream-KeyD comparison.

## Checks

- compared the active HID/input files with Linux `83f14548`
- matched the active vendor allowlist against `CONFIG_HID_*`
- checked disabled source/link status and current proxy declarations
- `cmake --build build -j4`
- `git diff --check`
