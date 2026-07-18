# Upstream Porting Audit

Updated: 2026-07-18

Rules: `usb_host/upstream-porting-rules.md`. Linux baseline:
`~/linux-upstream-hid` at `83f1454877cc292b88baf13c829c16ce6937d120`.

## Result

Partial conformance: active vendor and multitouch/haptic drivers are traceable;
HID core and input glue retain listed structural exceptions. This is not a
runtime safety certification.

Commit `00c751e` preserves Stadia/`ff-memless`, but that gaming path is
unlinked; `hid-multitouch`/`hid-haptic` are active. The previous manual
upload/play/erase path passed two-Pico cursor feedback. The current
READY/preload/ID-reuse tree builds but needs hardware retest.

## Scope

Active TinyUSB/FreeRTOS glue, evdev/KeyD boundary, allowlisted HID/input files,
and compatibility headers. Disabled sources were checked only for link status;
hiddev, CMedia, and Vivaldi are not certified for enablement.

## Conforming Areas

- CMake/`CONFIG_HID_*` agree on 13 vendor drivers plus `hid-multitouch` and
  `hid-haptic`; active driver diffs preserve adjacent upstream code/reasons.
- `hid-haptic.h` matches baseline. `hid-multitouch.c` has only three explained
  `jiffies` substitutions. `hid-haptic.c` retains upstream flow; port teardown
  cancels work before freeing buffers and destroys mutex backing.
- Resolution-multiplier setup uses the upstream synchronous block. `ff-core`
  stays linked; Stadia/`ff-memless` do not. Evdev clears `EV_REP` because KeyD
  owns held state and discards repeat value `2`.
- AppleIR is unlinked. The timer bridge serves multitouch's 100-ms sticky
  contact release; haptic duration is device-managed.
- `hid-drivers.c` is marked port-only linker/initcall glue; its Linux-derived
  location remains an explicit exception.

## Open Porting-Rule Findings

1. `hid-core.c:2906-3006,3044-3078,3413-3513` wraps mutable
   `struct hid_driver` in `hid_driver_runtime`; several signatures/branches
   lack exact adjacent anchors. Major exception.
2. `hid-core.c` can restore supported upstream `__free(kfree)`, logging, and
   `hid_close_report()` paths; disabled metadata and the `hid-input` KUnit block
   are also unnecessary current-macro diffs.
3. Invalid anchors remain near `hid_match_one_id()`, the duplicate allocation
   `#ifdef`, and historical WIP in `hid-drivers.c`.
4. `hid_is_usb()` moved into generic core and changed from ll-driver identity
   to bus check; predicate restoration needs review.
5. `usbhid.c` correctly declares non-line-preserving glue, but some anchors
   summarize instead of preserving exact upstream USB statements.
6. `input-mt.c` replaces three includes without anchors/reason. `ff-core.c`
   keeps `guard()` anchors, but not every changed scoped unlock/return line.
7. Reduced `input.c`, queue-backed `evdev.c`, and port-only `task.c` are
   deliberate non-line-preserving areas; `task.c` lacks a provenance note.
8. Stale notes call HAPTIC deferred, unregister nonblocking/drop-oldest, and
   layout test erase; code differs.

## Runtime Boundary

- One mutex serializes KeyD write/upload/erase with unregister. Unregister
  detaches evdev and queues removal; KeyD later frees queue/client. No second
  output queue/task or handshake exists.
- `driver_input_lock` serializes parsing with remove. Async cancel drops queued
  reports and waits for dequeued parser/completion work before destruction.
- Feature/raw/OUTPUT traffic uses `hid_async_task`: callers may wait, TinyUSB
  callbacks do not. Refcount, work cancellation, and async drain cover teardown.
- Evdev overflow drops only the incoming ordinary event; lifecycle sends may
  wait in their task, so producers never dequeue a QueueSet member.
- Host keyboard LEDs use the virtual `devmon` path and synchronous evdev writer;
  the laptop/remapper/fixture loop passed on hardware.

## Haptic Status

- After successful `hid_add_device()`, glue publishes
  `DEVICE_INPUT_HAPTIC_READY` only for a fully initialized `FF_HAPTIC` input.
  READY uses that device's existing evdev queue and cannot be discarded by
  ordinary input overflow. The send may wait only in the lifecycle probe task;
  preload may wait in KeyD, never in TinyUSB callbacks. No queue/task/timer or
  handshake was added.
- On READY, `keyd/port/device.c` tries Click/Buzz/Rumble/Press/Release per
  device and stores successful device-local IDs in a dynamically allocated
  state owned by that KeyD device; removal frees the state before the device.
  Each post-startup layout callback queues one virtual Press PLAY; `daemon.c`
  fans it out like LED output to every grabbed device, so it never consumes
  more FF slots.
- The layout hook runs in the KeyD consumer task, so its virtual PLAY enqueue is
  nonblocking. A full `devmon` queue drops that optional pulse instead of making
  KeyD wait on itself.
- The `device/haptic-touchpad` fixture exposes Press/Release and cursor feedback.
  Its Press duration is 10 ms, so one PLAY requests one device-timed pulse.
  Retest must confirm preload before the first post-READY layout callback,
  over 96 cycles, unplug/replug, rapid unplug during preload/playback, ordering,
  and watermark.

## Open Semantic Boundaries

- A real touchpad remains untested. Successful Press/Release preload switches
  it from autonomous DEVICE mode to HOST mode; until a real press/release trigger
  path exists, only layout-change feedback drives its haptics.
- Virtual layout PLAY is broadcast to all grabbed devices; devices without a
  preloaded Press ID ignore it.
- Upstream `hid_haptic_erase()` sets the temporary effect to NONE before testing
  whether it was Press/Release, so its DEVICE-mode restoration branches are
  unreachable. The retained layout path does not call erase.
- `port_input_dev.name` is a pointer while the rest of its devmon record is by
  value; fast add/remove can free the name before KeyD copies the record.

## Headers and Deferred Runtime

- `uapi/linux/input.h` matches baseline; `linux/types.h` no longer imports
  `hid_compat.h`. Kernel `input.h`, `hid.h`, `usb.h`, `workqueue.h`, and
  `timer.h` remain reduced contracts.
- Queues use 8-byte `port_input_event`; evdev converts from 16-byte
  `input_event`. `__KERNEL__` selects kernel UAPI without newlib ioctl headers.
- `hidraw.h` is an extended proxy contract but hidraw is not linked/claimed;
  hiddev is disabled and active code uses stubs.
- Callback-safe `raw_event`, synchronous request/wait, and returned GET data are
  active. Generic URB/control adapters, unaudited hooks, hidraw/hiddev runtime,
  and PIDFF remain deferred.
- The KeyD queue adapter is firmware glue; no pinned upstream-KeyD comparison
  is claimed.

## Checks

- active-file comparison with Linux `83f14548`; allowlist/`CONFIG_HID_*` match
- disabled link status and proxy declarations checked
- `cmake --build build -j4`; `git diff --check`
- `sizeof(input_event) == 16`; `sizeof(port_input_event) == 8`
- prior haptic-fixture build and two-board cursor feedback
