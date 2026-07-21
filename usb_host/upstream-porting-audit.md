# Upstream Porting Audit

Updated: 2026-07-21

Rules: `usb_host/upstream-porting-rules.md`.

Linux baseline: `../linux-upstream-hid` at
`83f1454877cc292b88baf13c829c16ce6937d120`.
Post-baseline upstream fix: multitouch active-slot bitmap commit
`8813b0612275cc61fe9e6603d0ee019247ade6be`.

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
- `hid-haptic.h` is byte-for-byte baseline. `hid-multitouch.c` retains three
  adjacent, explained `jiffies` member-token substitutions and imports upstream
  `8813b061`: active contacts use a `maxcontacts`-sized bitmap and the RUNNING
  flag is again bit zero, avoiding out-of-bounds bit access on 32-bit RP2040.
- `hid-haptic.c` keeps upstream flow and adjacent upstream anchors. Its
  explained generic fixes skip unassigned report values, restore DEVICE mode
  after the last Press/Release effect, and cancel work before HID/report-buffer
  release. The explicit five-slot replacement for Linux's 96 userspace effect
  slots is the firmware RAM-bound policy. Heap-backed FreeRTOS mutex creation
  is checked and unwound, failed optional FF publication restores input
  capabilities, and dynamic mutex backing is destroyed.
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

- After successful driver probe, lifecycle drains the interface's finite
  `io_pending` leases before evdev activation. This preserves the ordering of
  probe-originated mode SETs when firmware's direct interrupt-IN scheduler is
  independent from EP0/interrupt-OUT; the wait excludes continuously armed IN.
- `evdev_connect()` now registers only an inactive input handle. After the
  complete synchronous `hid_add_device()` probe, `evdev_activate_hid()`
  prepares every client/queue privately, opens every matching handle, and only
  then publishes all final input snapshots and writers to devmon. This matches
  Linux's private evdev object versus successful later eventX-open separation:
  another core cannot call a writer before its input handle is open. Partial
  activation is unwound by ordinary input disconnect; unpublished clients are
  freed directly, while already published siblings receive normal removal.
  Synchronous input generated by an open hook is suppressed until its empty
  queue has joined the QueueSet. Completions during activation retain normal
  transport recovery/rearm but skip parser delivery until `driver_ready`,
  which is published under the transport lock only if detach/stop has not
  already been published.
- One mutex serializes synchronous KeyD write/upload/erase with unregister.
  Unregister nulls `client->evdev`, sends the existing removal sentinel, and
  KeyD later deletes the queue/client. No second output queue/task or handshake.
- Host evdev uses a bounded 64-entry member queue for hid-input's standard
  60-value MT hint, its two input-core slots, and release/removal reserve; other
  firmware input queues remain 16 entries. A computed batch above 62 raises a
  bounded-policy warning and remains best-effort. QueueSet storage covers the
  largest member in all eight supported device slots. Neither overflow nor
  unregister dequeues a member outside the KeyD `xQueueSelectFromSet()`
  consumer: ordinary overflow drops the incoming value, while lifecycle removal
  waits in task context for its reserved slot.
- The queued devmon snapshot points at a bounded name copy owned by that same
  client tombstone, not at `input_dev`/`hidinput` storage. A fast activation
  failure or unplug therefore cannot leave ADD with a dangling name pointer.
- `driver_input_lock` serializes input parsing with remove. Async cancel drops
  queued reports and waits any dequeued parser/completion through its last HID
  access before destruction.
- Upstream `hid_ctrl()` parses a completed control URB in its callback. This
  port sends ordinary GET completion to the report task, but a probe GET whose
  lifecycle caller still owns `driver_input_lock` publishes its request buffer
  directly to that interface's `.wait()` owner. This avoids a firmware-only
  queue handoff without opening interrupt input on a half-built device. The
  existing per-interface wait head now carries the corresponding upstream-style
  wake edge; the broader firmware `io_pending` lease remains the durable
  predicate, so `hid_hw_wait()` no longer needs one-tick polling. After producer
  stop, teardown cancels by exact HID owner and uses the same wait head for a
  composite `usb_kill_urb()` predicate: no aggregate I/O, async slot,
  interrupt-IN owner, deferred host pass, or physical-detach fence retains the
  interface. Every last-owner transition publishes the wake after unlocking;
  the three former teardown polling barriers are gone without another RTOS
  object.
- TinyUSB callbacks are task-context publishers in this port. One explicit
  transport mutex replaces the former common FreeRTOS critical domain across
  async slots, lifecycle/cache state, and report ownership. It replaces only
  upstream's short IRQ-side state lock: generic HID's `ll_open_lock` again owns
  `ll_open_count` and its first/last-client transition, while the restored
  per-interface `usbhid->mutex` serializes complete start/stop/open/close
  operations as in Linux. The report task never reads `ll_open_count` across a
  foreign lock; TinyUSB completion snapshots the byte-sized transport
  `CLOSED/RESUMING/OPEN` state under the transport mutex. This mirrors both
  upstream's `HID_OPENED` test and its 50-ms `HID_RESUME_RUNNING` drain before
  deferred parser delivery. Callbacks take neither lifecycle mutex. FreeRTOS
  allocates both restored mutex objects dynamically, so the
  port checks construction as ordinary control flow and explicitly destroys
  them only after their Linux-owned objects are no longer reachable.
- The exact interrupt callback resolves its stable context through the live
  registry slot before dereferencing it, validates the full arm tuple, and only
  then publishes `QUEUED`. Invalid owner/epoch/buffer state clears the completed
  physical serial and parks that interface; an orphan slot is quarantined from
  reuse. Diagnostics are a bit in the existing lifecycle fault word, so the
  TinyUSB callback neither logs nor asserts after mutating report ownership.
  Recoverable host-owner, EP0-abort, and pinned hub-reset completion invariants
  use their existing task-side `-EIO` retirement path with the same
  lifecycle-owned diagnostic boundary. The hub callback always releases a
  slot it still owns from `HOST_COMPLETING`, so a diagnostic cannot pin the
  global EP0 lane.
  The common EP0/interrupt-OUT broker likewise selects completion by its unique
  request serial, validates the callback-provided address/endpoint, setup,
  buffer, and actual length before publication, and retires an invalid physical
  giveback as task-side `-EIO`. TinyUSB intentionally omits buffer identity from
  non-control callbacks, so interrupt OUT validates only the fields it exposes.
- Non-`ALWAYS_POLL` close now retains upstream `usb_kill_urb()` semantics: it
  clears polling/revision state, cancels retry recovery, then waits only for
  the direct interrupt-IN owner, deferred host pass, and any active parser to
  release the interface. A stack-owned per-interface waiter keeps this narrow
  close fence independent of the broader `hid_hw_wait()`/teardown wait head;
  its durable predicate is tested only under the transport mutex, and the
  final broad-idle edge is relayed after unlocking. `ALWAYS_POLL` still leaves
  receive and recovery running and only clears the completion-time open gate,
  exactly as upstream.
- Linux workqueue emulation now has its own priority-inheritance mutex rather
  than sharing either that transport domain or FreeRTOS's scheduler-wide
  critical section. Its FIFO and pending/running flags are durable conditions;
  direct task notifications wake the sole worker, while stack-owned waiters
  make `flush_work()`, `cancel_work_sync()`, and `destroy_workqueue()` block on
  exact state transitions instead of polling every tick. No mutex scope crosses
  a work callback, timer API, heap operation, or wait. All live entries are from
  KeyD, HID timer/lifecycle, or workqueue tasks; TinyUSB callbacks do not invoke
  this API. Firmware evaluates FreeRTOS calls and lock predicates outside
  `configASSERT()`, then diagnoses task-side failures and asserts only the
  captured boolean. This keeps the operations present in the `NDEBUG` build.
  That assertion rule also covers `hid_async.c`, `usbhid.c`,
  `usbhid_report.c`, and the transport mutex; imported Linux/FreeRTOS sources
  remain untouched.
- Linux's timer wheel must accept IRQ/softirq callers, but every active timer
  parent in this firmware is now task-owned. TinyUSB unmount no longer calls
  even non-waiting `timer_delete()`; it publishes stopping, and the report task
  immediately retires pre-wire clear-halt state or claims I/O-retry cancellation
  under an `io_pending` interface lease. The timer bridge therefore uses its
  own priority-inheritance mutex, a direct worker notification, and stack-owned
  condition waiters for `timer_delete_sync()`. Timer callbacks run outside the
  mutex, and no transport/workqueue lock is held across the synchronous wait.
  This removes the four remaining glue critical regions and their one-tick
  running-callback poll without changing Linux-derived timer call sites.
- Linux wait queues pair a durable condition with a wake edge. The lifecycle
  glue now follows that split directly: flags/cache slots own the condition and
  a dedicated indexed task notification wakes their sole task owner. This
  removes the firmware-only one-entry event queue without changing any
  Linux-derived source. Fixed-slot admission now follows the same model inside
  the generic synchronous USB bridge: lifecycle and workqueue callers register
  stack-owned FIFO waiters, enqueue retry is the condition, and normal-slot
  release or teardown supplies the wake up to a separate one-second local
  bound. The reserved recovery slot is excluded
  and nonblocking producers cannot consume waiter-reserved capacity. Descriptor
  callers contain no FreeRTOS retry logic; `hid_get_class_descriptor()` is once
  again line-identical to upstream.
- TinyUSB mount owns an immutable fixed-slot publication and its nonzero host
  serial; the sole lifecycle consumer owns only the handled serial. Unmount
  clears the host serial, so a task-side probe in flight cannot validate or
  acknowledge a later fast-replug publication. This replaces the mixed
  callback/lifecycle `FREE/PENDING/ACTIVE` mini-state-machine without changing
  the 16-byte slot or adding a queue.

## Haptic Status

- `hid-multitouch`/`hid-haptic` are active; Stadia/`ff-memless` are unlinked.
  Haptic uses `input_ff_create()` with five firmware-owned effect slots and
  device-managed timing. Linux's 96-slot call and allocation remain commented
  beside that memory-bounded replacement.
- The existing lifecycle task probes. Feature/raw/OUTPUT traffic passes through
  `hid_async_task`; caller tasks wait, TinyUSB callbacks do not. Raw GET/SET and
  `.output_report()` use the upstream usbhid helper bodies over the generic
  USB-message bridge. HID owner tags, mutexes, refcount, work cancellation, and
  async drain cover teardown.
- Evdev client publication and the first `hid_hw_open()` occur only after
  `hid_add_device()` returns from the complete multitouch/haptic probe. The
  final `EV_FF`/`FF_HAPTIC` state is therefore atomic at the firmware client
  boundary without a READY/update side channel.
- The temporary manual layout-change hook ignores the startup notification,
  builds and uploads/plays Press, then erases/stops it on the next change.
  Effect state is caller-owned; no test helper or field was added to the
  evdev/KeyD device structures.
- ff-core retains an old definition throughout replacement upload and retains
  an erased definition while the driver callback runs. `hid-haptic` uses those
  upstream contracts to decide HOST/DEVICE ownership; it cancels a slot's
  pending PLAY before rewriting it and drains all effect/STOP work before
  teardown frees its dependencies.
- Fixture `ErgoType-hid-devices:device/haptic-touchpad` exposes the reports;
  it coalesces upstream STOP-to-PLAY into PLAY/up, while explicit STOP moves
  the cursor down. Both UF2s build; enumeration and cursor feedback pass on
  hardware. Full output-order, unplug, and stack-watermark checks remain.

## Open Semantic Boundaries

- The emulator confirms the active multitouch `FF_HAPTIC` output path. A real
  touchpad and the remaining teardown/order checks are not hardware-verified.

## Headers and Deferred Runtime

- `uapi/linux/input.h` is byte-for-byte baseline; `linux/types.h` no longer
  imports `hid_compat.h`. Kernel `input.h`, `hid.h`, `usb.h`, `workqueue.h`,
  and `timer.h` remain reduced contracts. The reduced `bitmap.h` provides only
  the upstream `bitmap_empty()` API needed by `hid-multitouch`.
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
  in the TinyUSB host owner. A physical arm failure publishes only its exact
  generation/open revision; the report task applies `hid_io_error()` and the
  same bounded FAILED/TIMEOUT retry. Old-open failures cannot schedule recovery
  after close/reopen. Local CLEAR_HALT slot admission is now wait-queue shaped rather than
  timer-polled: normal-slot release or a dropped unused reservation wakes the
  durable report-task state, and a capacity latch closes the unlocked enqueue
  race. Its report-task notification wait retains the absolute eight-second
  local bound without another timer. Once its wire request is active, close no
  longer drops completion:
  success performs the host DATA0 reset and wire failure keeps Linux's device-
  reset decision, while only interrupt-IN rearm depends on the open state.
  Generic URBs, synchronous interrupt-IN, unaudited hooks,
  hidraw/hiddev runtime, and PIDFF remain deferred.
- `usbhid_start()` again computes the exact upstream per-device `bufsize` from
  INPUT, OUTPUT, and FEATURE reports. GET_REPORT uses that rounded EP0 limit and
  writes directly into completion-owned parser storage. Every asynchronous SET
  keeps the upstream `hid_alloc_report_buf()` enqueue snapshot; synchronous USB
  helpers borrow their blocked caller's buffer. Slot-owned snapshots are freed
  only after completion or the physical abort/drain/fence path, outside the
  transport mutex. The generic bridge no longer imposes its former
  257-byte payload cap.
- Interrupt IN restores upstream's `usbhid->inbuf` field and start/stop buffer
  lifecycle. TinyUSB's logical transfer remains the largest INPUT report,
  capped at 16 KiB; the firmware allocation deliberately excludes unrelated
  OUTPUT/FEATURE maxima and adds only the PIO HCD's packet-safe tail. The old
  four-by-64-byte scratch pool and its descriptor rejection are gone. Physical
  detach is a two-owner fence: the callback publishes slot state, the report
  task queues a coalesced host event, and only that post-`hcd_device_close()`
  acknowledgement lets lifecycle release the slot and buffer.
- USB detach now preserves the upstream `usbhid_disconnect(struct
  usb_interface *)` entry shape after `usbhid_probe()`. TinyUSB's earlier
  physical and HID-class callbacks only publish an idempotent disconnect bit,
  close the report producer, and wake lifecycle. The task-side function keeps
  the upstream `usb_get_intfdata()` / disconnected / destroy / free sequence
  visible, with the firmware's async/report/I/O synchronization inserted before
  destruction. Physical publication remains authoritative before address-epoch
  cancel and `hcd_device_close()`; raw app-driver close supplies that fence for
  hubs, where TinyUSB omits its common unmount callback.
- Interrupt completion owns no HID protocol policy and performs no address-based
  HID lookup. Under the transport mutex it resolves the fixed slot's one live
  registry owner, then validates that owner against the retained `hid`, `inbuf`,
  device generation, endpoint, size, and open revision. Only that accepted tuple
  publishes raw length/result and `ARMED -> QUEUED`. The report task claims
  `QUEUED -> ACTIVE` and applies
  open/stopping/recovery policy under that ownership fence. An old arm is
  aborted across close/reopen, while an already queued old-revision completion
  is discarded and followed by a fresh arm, matching `usb_kill_urb()`'s epoch
  boundary. The previous callback-side BOOT branch remains commented adjacent
  to the task replacement for port provenance. Linux's reset-default Report
  assumption removes the active retained mode and generic SET_PROTOCOL path.
  Invalid or failed payload still cannot enter `hid_safe_input_report()`.
- Report-descriptor ingress now preserves the pinned
  `hid_get_class_descriptor()` request tuple and retry shape beside the local
  replacement: standard interface `GET_DESCRIPTOR`, exact class-declared
  length, one zeroed buffer, four attempts, and acceptance of the final positive
  short read. The synchronous Linux parser now owns that buffer and calls the
  generic async-backed `usb_control_msg()` from lifecycle task context. Local
  pool admission waits are bounded and do not consume a wire attempt;
  cancellation wakes the blocked parser while its exact buffer remains live.
  Descriptors up to Linux's 4 KiB limit no longer depend on TinyUSB's 512-byte
  enumeration scratch; configuration descriptors still do.
  A second SHA-pinned build-local source preserves TinyUSB `hid_host.c`'s full
  `hidh_open()`, `hidh_set_config()`, and prefetch blocks commented beside their
  port replacements.
  Its two-pass current-interface scanner mirrors Linux's accepted HID-descriptor
  positions, caps opened/stored endpoints at `bNumEndpoints`, and publishes the
  TinyUSB class slot only after endpoint success. The set-config replacement
  skips enumeration SET_IDLE/SET_PROTOCOL and mounts with `NULL`; task-side
  `usbhid_parse()` retains upstream's SET_IDLE line and is the sole report-
  descriptor owner. No generic SET_PROTOCOL is added because Linux relies on
  the reset-default Report protocol. Firmware-only device/string reconstruction
  is now wholly lifecycle-owned: adjacent upstream `usb_get_descriptor()` /
  `usb_get_string()` calls stay visible while the compact port sends their
  standard request tuples through generic async-backed `usb_control_msg()` and
  one aligned pool scratch. There is no private descriptor kind, FIFO, callback
  continuation, or scratch in `hid_async`. Device-descriptor recovery retains
  four accepted attempts with 100-ms deadline wakeups. The generic call pins
  TinyUSB's address epoch and exact cache record; lifecycle then rechecks its
  separate cache generation before mutation, so detach cannot restamp an old
  preprobe as the new address epoch. The installed callback documentation still
  describes stock behavior; this firmware intentionally passes `NULL`.
- Linux queues a device reset after clear-halt transfer failure or exhausted
  protocol retry. The adjacent upstream `usb_queue_reset_device()` lines remain
  commented; the port publishes the same terminal decision into a lifecycle
  coordinator because TinyUSB callbacks cannot wait for TinyUSB progress.
  The coordinator snapshots only the physical cache epoch/topology, closes and
  drains the old HID graph, gates global EP0 work, then runs full TinyUSB root
  or hub-port teardown/re-enumeration. This is deliberately stronger than
  Linux's successful in-place reset because pinned TinyUSB has no safe API to
  restore configured class state in place. Software-only async-pool exhaustion
  does not take this path; CLEAR_HALT waits for an exact capacity edge, while a
  fatal pre-wire submit error parks and reports a local rearm failure. Reset
  progression is wait-queue shaped rather than polled: exact retirement,
  mount/enumeration, async-slot-release, and host-global-control-IDLE
  publications wake lifecycle, and only the phase deadline supplies a timed
  wake. Async EP0 idle is published after slot release so a hub retry cannot
  race its reserved slot's completion callback. The central TinyUSB control
  transition additionally covers native hub housekeeping, abort, remove, and
  watchdog release.
  Root attach admission is atomic with the matching `enum_active` predicate;
  rejected host admission parks until global control idle or the one TinyUSB
  enum terminal instead of sustaining a lifecycle-to-host defer loop.
  Reset publication also snapshots `report_revision` and atomically rejects a
  close-cancelled work item, preserving upstream `cancel_work_sync()` semantics
  across a concurrent reopen.
- Pinned TinyUSB omits its documented `tuh_mount_cb()` after hub enumeration,
  and `tuh_mounted()` becomes true before class-driver set-config completes.
  The build verifies the pinned `usbh.c` SHA and generates one build-local
  source with five audited deltas: a hub post-`enum_full_complete()` mount
  fence, a weak generation-authorization hook, direct host-owner root/hub
  enumeration helpers, and exact daddr/callback/user-data recovery for a lost
  EP0 completion, plus a global control-IDLE wake. The helpers preserve
  `enum_new_device()` and avoid a blocking send back into TinyUSB's sole host
  queue from its own consumer. EP0 retirement
  permits three SETUP/DATA/ACK drain fences before the exact old owner receives
  TinyUSB's normal terminal TIMEOUT callback; a replacement serial is untouched.
  Exact unique CMake anchors preserve replaced upstream blocks beside the port
  and reject source drift. The SDK installation remains untouched.
- The KeyD queue adapter is firmware glue; no pinned upstream-KeyD comparison
  is claimed.
- `hid_register_field()` retains upstream's single-allocation layout and full
  usage/priority tables. The adjacent port replacement sizes `value` and
  `new_value` by physical `report_count` only for INPUT ARRAY fields; those
  runtime paths index report slots, while selector-to-usage translation still
  keeps all usages. The original signature, allocation, pointer arithmetic,
  and call remain commented beside the RP2040 memory-bounded replacement.
- Parser allocation failure is no longer converted into a missing field and a
  partially bound HID graph. The adjacent port path preserves upstream's
  `HID_MAX_FIELDS` truncation but propagates real `-ENOMEM` through the reduced
  driver-core shim to task-side `usbhid_probe()`, which destroys the complete
  interface. The global heap-failure counter remains diagnostic only and is no
  longer used to attribute another task's allocation failure to this probe.
- `hid_report_enum` retains upstream's `report_list` and complete report-ID
  semantics but omits the dense 256-pointer `report_id_hash`. Exact-ID lookup
  scans the already-owned sparse list, normally one to three entries. The
  original member and every direct upstream access remain commented beside the
  port replacement. ARM32 `hid_device` size falls from 3,712 B to 640 B, saving
  3,072 B per HID interface without truncating the valid 0..255 ID range.
- Plain `kobject_uevent()` calls retain lifecycle counters but do not build a
  2.3 KiB environment: firmware has no userspace/netlink sink for it.
  `kobject_uevent_env()` remains available if a real environment consumer is
  added later.

## Checks

- compared the active HID/input files with Linux `83f14548`
- matched the active vendor allowlist against `CONFIG_HID_*`
- checked disabled source/link status and current proxy declarations
- `cmake --build build -j4`
- confirmed `sizeof(hid_async_request) == 60` and
  `sizeof(hid_async_slot) == 88` on the RP2040 ABI
- confirmed `sizeof(usbhid_device) == 248`, the four RX ownership/completion
  slots are 32 B each (128 B total), and scratch X is 708 B
- confirmed `sizeof(input_event) == 16` and `sizeof(port_input_event) == 8`
- built `device/haptic-touchpad` (`build/ErgoType.uf2`, 159232 bytes)
- `git diff --check`
- observed two-board haptic cursor feedback
