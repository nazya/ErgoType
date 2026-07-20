# Upstream Porting Audit

Updated: 2026-07-20

Rules: `usb_host/upstream-porting-rules.md`.

Linux baseline: `../linux-upstream-hid` at
`83f1454877cc292b88baf13c829c16ce6937d120`.

## Result

Partial conformance. Active vendor drivers and the new multitouch/haptic files
are traceable ports; core and input glue still contain explicit structural
exceptions. This is a porting audit, not a runtime safety certification.

Checkpoint `hid: stabilize stadia ff teardown` preserves the Stadia/`ff-memless` implementation and teardown
fixes. The working tree keeps that gaming path unlinked and instead links
upstream `hid-multitouch` plus `hid-haptic`. Both firmware images build; a
two-Pico run confirms haptic OUTPUT through emulator cursor feedback.

## Scope

Active TinyUSB/FreeRTOS glue, evdev/KeyD boundary, allowlisted Linux HID/input
files, and their compatibility headers. Disabled drivers were checked only for
link status; hiddev, CMedia, and Vivaldi are not certified for enablement.

## Conforming Areas

- CMake/`CONFIG_HID_*` agree on 13 vendor drivers, generic `hid-multitouch`,
  and `hid-haptic`. Active vendor/generic changes keep adjacent upstream lines
  and reasons; no unrelated vendor-flow rewrite was found.
- `hid-haptic.h` is byte-for-byte baseline. `hid-multitouch.c` differs only in
  three adjacent, explained `jiffies` member-token substitutions.
- `hid-haptic.c` keeps upstream flow; its explained teardown additions cancel
  work before buffer release and destroy dynamic mutex backing.
- Resolution-multiplier setup is back to the upstream synchronous block; the
  former local continuation functions are gone.
- `ff-core` remains linked; `ff-memless`/Stadia do not. Evdev clears `EV_REP`
  because KeyD owns held-key state and discards Linux repeat value `2`.
- AppleIR remains unlinked. The timer bridge now serves multitouch's 100-ms
  sticky-contact release; haptic waveform duration is device-managed and has
  no FF duration timer.
- `hid-drivers.c` is marked port-only linker/initcall glue; its Linux-derived
  directory placement remains an explicit exception.

## Open Porting-Rule Findings

1. `hid-core.c:2906-3006,3044-3078,3413-3513` rewrites mutable
   `struct hid_driver` around `hid_driver_runtime`; several signatures and
   register branches lack exact adjacent upstream anchors. Major exception.

2. `hid-core.c` can restore supported upstream `__free(kfree)`, logging, and
   `hid_close_report()` paths. Disabled module metadata and the `hid-input`
   KUnit block are also unnecessary diffs under current macros.

3. Some comments are not valid upstream anchors: the false port-only export
   rationale near `hid_match_one_id()`, the duplicate `#ifdef` near allocation,
   and historical WIP in `hid-drivers.c`.

4. `hid_is_usb()` moved into generic core and changed from ll-driver identity
   to bus check. Placement is mechanical; predicate restoration needs review.

5. `usbhid.c` correctly declares itself non-line-preserving glue, but several
   local anchors still summarize rather than preserve the exact upstream USB
   statement or switch body.

6. `input-mt.c` replaces three includes without anchors/reason. `ff-core.c`
   retains `guard()` anchors, but manual unlocks do not preserve every changed
   early-return/scoped-block line.

7. Reduced `input.c`, queue-backed `evdev.c`, and port-only `task.c` are
   deliberate non-line-preserving areas; `task.c` lacks a provenance note.

## Fixed Runtime Boundary

- One mutex serializes synchronous KeyD write/upload/erase with unregister.
  Unregister nulls `client->evdev`, sends the existing removal sentinel, and
  KeyD later deletes the queue/client. No second output queue/task or handshake.
- `driver_input_lock` serializes input parsing with remove. Async cancel drops
  queued reports and waits any dequeued parser/completion through its last HID
  access before destruction.

## Haptic Status

- `hid-multitouch`/`hid-haptic` are active; Stadia/`ff-memless` are unlinked.
  Haptic uses `input_ff_create()` and device-managed timing.
- The existing lifecycle task probes. Feature/raw/OUTPUT traffic passes through
  `hid_async_task`; caller tasks wait, TinyUSB callbacks do not. Raw GET/SET and
  `.output_report()` use the upstream usbhid helper bodies over the generic
  USB-message bridge. HID owner tags, mutexes, refcount, work cancellation, and
  async drain cover teardown.
- The temporary manual layout-change hook ignores the startup notification,
  builds and uploads/plays Press, then erases/stops it on the next change.
  Effect state is caller-owned; no test helper or field was added to the
  evdev/KeyD device structures.
- Fixture `ErgoType-hid-devices:device/haptic-touchpad` exposes the reports;
  it coalesces upstream STOP-to-PLAY into PLAY/up, while explicit STOP moves
  the cursor down. Both UF2s build; enumeration and cursor feedback pass on
  hardware. Full output-order, unplug, and stack-watermark checks remain.

## Open Semantic Boundaries

- The emulator confirms the active multitouch `FF_HAPTIC` output path. A real
  touchpad and the remaining teardown/order checks are not hardware-verified.
- `port_input_dev.name` is still a pointer, although the rest of the devmon
  record is queued by value. A fast add/remove can free `hidinput->name` before
  KeyD copies the pending devmon record.

## Headers and Deferred Runtime

- `uapi/linux/input.h` is byte-for-byte baseline; `linux/types.h` no longer
  imports `hid_compat.h`. Kernel `input.h`, `hid.h`, `usb.h`, `workqueue.h`,
  and `timer.h` remain reduced contracts.
- FreeRTOS queues use the 8-byte `port_input_event`; evdev converts from the
  16-byte kernel `input_event` at the devmon boundary. `__KERNEL__` selects the
  kernel UAPI layout without exposing unavailable newlib ioctl headers.
- `hidraw.h` is an extended proxy contract, but hidraw is not linked/claimed.
  hiddev also remains disabled and active code uses stubs.
- Audited callback-safe `raw_event`, synchronous HID report request/wait, and
  returned GET data are active. Bounded task-side USB control and interrupt-OUT
  adapters are active with per-interface cancellation. HID report and generic
  control requests share same-device EP0 order; all interrupt-OUT sources share
  endpoint-keyed order. Interrupt-IN STALL recovery uses that generic EP0 lane
  for remote clear-halt, then performs the required PIO DATA0 reset and rearm
  in the TinyUSB host owner. FAILED/TIMEOUT uses upstream's bounded delayed
  retry. Generic URBs, synchronous interrupt-IN, unaudited hooks,
  hidraw/hiddev runtime, and PIDFF remain deferred.
- `usbhid_start()` again computes the exact upstream per-device `bufsize` from
  INPUT, OUTPUT, and FEATURE reports. GET_REPORT uses that rounded EP0 limit and
  writes directly into completion-owned parser storage. Every asynchronous SET
  keeps the upstream `hid_alloc_report_buf()` enqueue snapshot; synchronous USB
  helpers borrow their blocked caller's buffer. Slot-owned snapshots are freed
  only after completion or the physical abort/drain/fence path, outside the
  FreeRTOS critical section. The generic bridge no longer imposes its former
  257-byte payload cap.
- Interrupt IN restores upstream's `usbhid->inbuf` field and start/stop buffer
  lifecycle. TinyUSB's logical transfer remains the largest INPUT report,
  capped at 16 KiB; the firmware allocation deliberately excludes unrelated
  OUTPUT/FEATURE maxima and adds only the PIO HCD's packet-safe tail. The old
  four-by-64-byte scratch pool and its descriptor rejection are gone. Physical
  detach is a two-owner fence: the callback publishes slot state, the report
  task queues a coalesced host event, and only that post-`hcd_device_close()`
  acknowledgement lets lifecycle release the slot and buffer.
- Report-descriptor ingress now preserves the pinned
  `hid_get_class_descriptor()` request tuple and retry shape beside the local
  replacement: standard interface `GET_DESCRIPTOR`, exact class-declared
  length, one zeroed buffer, four attempts, and acceptance of the final positive
  short read. The port-specific difference is scheduling those attempts through
  the lifecycle task and async EP0 owner before entering the synchronous Linux
  parser. Cancellation keeps the exact buffer owned through the physical-device
  generation fence. Descriptors up to Linux's 4 KiB limit no longer depend on
  TinyUSB's 512-byte enumeration scratch; configuration descriptors still do.
  A second SHA-pinned build-local source preserves TinyUSB `hid_host.c`'s full
  `hidh_open()` and prefetch blocks commented beside their port replacements.
  Its two-pass current-interface scanner mirrors Linux's accepted HID-descriptor
  positions, caps opened/stored endpoints at `bNumEndpoints`, and publishes the
  TinyUSB class slot only after endpoint success. SET_IDLE/SET_PROTOCOL remain
  unchanged, then the class mounts with `NULL` so lifecycle is the sole report-
  descriptor owner. The firmware-only full device-descriptor refetch carries a
  separate cache/client generation beside TinyUSB's address generation and uses
  four accepted attempts with 100-ms deadline wakeups; detach cannot restamp an
  old preprobe as the new address epoch. The installed callback documentation
  still describes stock behavior; this firmware intentionally passes `NULL`.
- Linux queues a device reset after clear-halt transfer failure or exhausted
  protocol retry. The adjacent upstream `usb_queue_reset_device()` lines remain
  commented; the port publishes the same terminal decision into a lifecycle
  coordinator because TinyUSB callbacks cannot wait for TinyUSB progress.
  The coordinator snapshots only the physical cache epoch/topology, closes and
  drains the old HID graph, gates global EP0 work, then runs full TinyUSB root
  or hub-port teardown/re-enumeration. This is deliberately stronger than
  Linux's successful in-place reset because pinned TinyUSB has no safe API to
  restore configured class state in place. Software-only async-pool exhaustion
  does not take this path; it parks and reports a local rearm failure. Reset
  publication also snapshots `report_revision` and atomically rejects a
  close-cancelled work item, preserving upstream `cancel_work_sync()` semantics
  across a concurrent reopen.
- Pinned TinyUSB omits its documented `tuh_mount_cb()` after hub enumeration,
  and `tuh_mounted()` becomes true before class-driver set-config completes.
  The build verifies the pinned `usbh.c` SHA and generates one build-local
  source with four audited deltas: a hub post-`enum_full_complete()` mount
  fence, a weak generation-authorization hook, direct host-owner root/hub
  enumeration helpers, and exact daddr/callback/user-data recovery for a lost
  EP0 completion. The helpers preserve `enum_new_device()` and avoid a blocking
  send back into TinyUSB's sole host queue from its own consumer. EP0 retirement
  permits three SETUP/DATA/ACK drain fences before the exact old owner receives
  TinyUSB's normal terminal TIMEOUT callback; a replacement serial is untouched.
  Exact unique CMake anchors preserve replaced upstream blocks beside the port
  and reject source drift. The SDK installation remains untouched.
- The KeyD queue adapter is firmware glue; no pinned upstream-KeyD comparison
  is claimed.

## Checks

- compared the active HID/input files with Linux `83f14548`
- matched the active vendor allowlist against `CONFIG_HID_*`
- checked disabled source/link status and current proxy declarations
- `cmake --build build -j4`
- confirmed `sizeof(hid_async_request) == 64` and
  `sizeof(hid_async_slot) == 92` on the RP2040 ABI
- confirmed `sizeof(usbhid_device) == 248`, the four RX metadata slots remain
  80 B total, and scratch X fell from 916 B to 660 B
- confirmed `sizeof(input_event) == 16` and `sizeof(port_input_event) == 8`
- built `device/haptic-touchpad` (`build/ErgoType.uf2`, 159232 bytes)
- `git diff --check`
- observed two-board haptic cursor feedback
