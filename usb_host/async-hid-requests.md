# Async HID Requests Direction

> Historical conversion plan. The active port now uses task-owned
> sync-over-async request/wait paths; TinyUSB callbacks publish bounded records
> and do not run Linux continuations. See `async-hid-progress.md` and
> `deferred-hid-drivers.md` for the current boundary.

This note records the historical design that preceded the active transport.
Its old line-number anchors and proposed state-machine conversions are retained
below as rationale, not as a description of the current tree.

The active implementation now has these properties:

- TinyUSB callbacks copy or publish bounded records and return; Linux parsing,
  probe, remove, and continuations run in task context.
- Physical/HID unmount callbacks publish an idempotent disconnect fence; the
  lifecycle task alone invokes upstream-shaped `usbhid_disconnect()`, waits for
  report/async/I/O ownership, and destroys the HID object. Interrupt completion
  likewise publishes transfer metadata only; boot/report parser policy belongs
  to the report task.
- `hid_hw_request()` queues work and `hid_hw_wait()` waits through control
  parsing, matching the upstream caller contract without blocking TinyUSB.
  Ordinary GET completion is report-task-owned; a GET queued while probe owns
  `driver_input_lock` is returned directly to that lifecycle owner. An inner
  `hid_hw_wait()` consumes it while retaining the lock, whereas the outer
  activation fence can consume it after probe releases the lock; glue selects
  the matching parser entry. The existing interface wait head receives
  completion edges while `io_pending` remains the durable predicate, so this
  wait does not poll or allocate another synchronization object.
- Raw GET/SET and interrupt output keep their synchronous ll-driver contracts
  through the upstream usbhid helpers and generic task-side
  `usb_control_msg()` / `usb_interrupt_msg()` waits.
- The task-only workqueue now owns a wrap-safe delayed-work deadline list.
  The five active wired Wacom profiles use it for pinned one-second
  initialization callbacks whose Feature SET/GET operations run through the
  existing synchronous-looking raw-control path; PTK-450 and PTH-650 also use
  ordinary work for their reached LED and battery paths. Pending, promoted, and
  running delayed-work cancellation are fenced before driver resources are
  released. The expanded exact host/emulator pair passed disconnect before the
  deadline, disconnect while mode or LED work was running, active-touch
  teardown, recovery, repeated mode/input cycles, and stable teardown on
  hardware for CTL-472, CTL-672, PTK-450, CTH-470, and PTH-650. All 115 heap
  snapshots reported `oom=0` and every task watermark remained nonzero.
  PTH battery reports and normal terminal power-queue cleanup were exercised,
  but the production log contained no value-level power snapshots and did not
  exercise UI task/timer startup failure. Promotion-before-callback,
  simultaneous synchronous cancel, callback self-requeue, queue destruction,
  and tick wrap remain statically audited generic branches rather than
  hardware-covered claims.
- Device and string pre-probe policy is lifecycle-owned and uses that same
  generic `usb_control_msg()` path with one transport-pool scratch. The async
  executor has no descriptor-specific request kind, FIFO, or continuation.
- HID EP0 GET/SET use direct asynchronous `tuh_control_xfer()` so
  completion retains the real TinyUSB result, actual length, and request serial.
- Task-context `usb_control_msg()` and interrupt-OUT `usb_interrupt_msg()` now
  use fixed metadata slots with physical-device epoch leases. Synchronous
  callers retain their direct transfer buffers; each asynchronous SET owns an
  exact upstream-style enqueue snapshot. Interface/endpoint owner tags make
  those slots participate in per-HID stop/cancel. HID report requests and
  generic messages share same-device EP0 order, while all OUT sources share
  per-physical-endpoint order rather than global or hook-specific FIFOs.
  SET_IDLE and raw GET/SET exercise the generic EP0 path.
- Interrupt-IN STALL recovery queues endpoint `CLEAR_FEATURE(HALT)` on that
  same per-device EP0 lane, then resets the local PIO toggle to DATA0 and rearms
  from the TinyUSB host owner. Protocol failure uses the upstream delayed retry
  cadence. Terminal transfer/retry failure now publishes the final USB-core
  reset fallback to the lifecycle task, which performs full TinyUSB
  teardown/re-enumeration without retaining a HID pointer. A global EP0 gate
  parks normal logical requests while root or hub-port reset owns enumeration.
  Lifecycle sleeps on exact reset publications/deadlines rather than a periodic
  poll; the async owner publishes gated-EP0 idle only after its control slot is
  reusable, preserving hub-reset retry admission. Matching active enumeration
  is another durable admission predicate. TinyUSB also publishes its physical
  global control-IDLE transition for native hub requests and teardown. Root
  recovery therefore parks on exact idle/terminal edges instead of bouncing
  rejected attach calls through the host queue.
  Known-success recovery enters the pinned `enum_new_device()` path directly
  from the host owner, avoiding a recursive send to TinyUSB's only host queue;
  exact physical and parent generations fence any raced replacement epoch.
- Generic synchronous callers, logical usbhid report heads, and CLEAR_HALT use
  one FIFO of durable admission nodes. A waiting node does not reserve a
  physical slot: the oldest endpoint-front becomes eligible only when a real
  normal slot is free. Slot release, node unlink, logical-head change, or
  teardown supplies a coalesced wake. Lifecycle and `hid_workqueue_task` use
  stack-owned nodes with a one-second admission bound; their transfer deadline
  starts separately. The dedicated HUB_RESET recovery slot stays outside this
  FIFO. CLEAR_HALT retains its node and recovery state in the fixed RX owner;
  the report task sleeps on the common admission edge or its absolute
  eight-second local bound. Its old 32-ms polling retry is not part of the
  protocol timer, which remains dedicated to upstream's interrupt-I/O cadence.
- Arbitrary URBs and synchronous interrupt-IN messages remain deferred. Report
  descriptors are now fetched by task-side `usbhid_parse()` at their
  class-declared size through the generic asynchronous EP0 owner, up to Linux's
  4 KiB limit;
  full configuration descriptors use TinyUSB's permanent 512-byte scratch or
  one exact-size host-owner buffer for validated lengths through 4 KiB. The
  control callback only records pending state; allocation, initial submission,
  and retry run from shallow TinyUSB host-owner service after callback unwind.
  Successful configuration parse frees immediately in TinyUSB's internal enum
  continuation; failure frees after terminal EP0 drain. Direct interrupt-IN
  uses upstream's per-interface buffer ownership and Linux's 16 KiB HID limit.
  Generic synchronous messages use the native 16-bit USB length and HID
  `.request()` uses the same 16 KiB Linux limit.

See `hid_async.c`, `usbhid.c`, and `async-hid-progress.md` for current anchors.

## Remapper Reference

Reference project: `../hid-remapper`.

Useful anchors:

- `../hid-remapper/firmware/src/out_report.cc:27`
  queues OUTPUT and SET_FEATURE requests.
- `../hid-remapper/firmware/src/out_report.cc:48`
  queues GET_FEATURE requests.
- `../hid-remapper/firmware/src/out_report.cc:62`
  pumps one queued request when `ready_to_send` is true.
- `../hid-remapper/firmware/src/out_report.cc:81`
  handles SET_REPORT completion.
- `../hid-remapper/firmware/src/out_report.cc:86`
  handles GET_REPORT completion and forwards the result to a continuation.
- `../hid-remapper/firmware/src/remapper_single.cc:101`
  keeps interrupt report callback short and immediately rearms receive.
- `../hid-remapper/firmware/src/remapper_single.cc:119`
  wraps queueing for OUTPUT / SET_FEATURE / GET_FEATURE.
- `../hid-remapper/firmware/src/main.cc:315`
  calls `send_out_report()` from the main loop.
- `../hid-remapper/firmware/src/ps_auth.cc:62`
  shows a real async state machine that queues requests only when not busy.
- `../hid-remapper/firmware/src/ps_auth.cc:144`
  advances state from GET_REPORT completion.
- `../hid-remapper/firmware/src/ps_auth.cc:170`
  advances state from SET_REPORT completion.

The important pattern is not "wait in a callback". It is:

1. Queue a request.
2. Return to the scheduler.
3. Submit when transport is ready.
4. Complete from TinyUSB completion callback.
5. Continue explicit state.

## Design Direction

Use a small async HID control/request layer for operations that Linux normally
handles through `hid_hw_request()`, `hid_hw_raw_request()`, `hid_hw_wait()`, or
`usb_control_msg()`.

Do not put this pump after `tuh_task()` in the current host task. The current
host task can stay inside `tuh_task()` and should not own extra request
progress. Use a separate small FreeRTOS task for HID control requests.

Proposed tasks:

- TUH task:
  - owns `tuh_task()`
  - receives TinyUSB callbacks
  - keeps callbacks short
  - does not block, log directly, or run state-machine work

- HID control task:
  - owns a small request queue
  - owns at most one active control/output transfer initially
  - submits `tuh_hid_get_report()` / `tuh_hid_set_report()`
  - receives short completion events from callbacks
  - runs continuations/state machines outside TinyUSB callbacks

## Submit Ownership

Default preference: submit TinyUSB HID control/output requests from one place,
the HID control task. This reduces races and makes one active request easier to
reason about.

This is not a hard rule. If a specific future case becomes much simpler by
calling `tuh_hid_get_report()` or `tuh_hid_set_report()` directly from another
nonblocking context, that is allowed. The requirement is that it must not block,
must not run from a heavy callback path, and must not make ownership of the
active request ambiguous.

## Request Queue Shape

Start small and bounded.

Suggested request types:

- `GET_REPORT`
- `SET_REPORT`
- `OUTPUT_REPORT`
- later `SET_IDLE`

Suggested request fields:

- `dev_addr`
- `instance`
- `report_id`
- `report_type`
- `reqtype`
- `len`
- fixed report buffer, currently 96 bytes so the Razer Blackwidow
  91-byte SET_FEATURE init report fits without dynamic allocation
- continuation kind
- continuation context pointer or small inline context id

Queue length should stay small, for example 4 or 8. Overflow must fail
explicitly. Do not silently drop control requests.

Callbacks should not allocate. For GET_REPORT, the transfer buffer must live
until completion; model this like remapper's static `get_buffer[64]`, or store
the buffer inside the active request.

## Completion Flow

TinyUSB completion callbacks should do only short work:

- validate/copy `dev_addr`, `instance`, `report_id`, `report_type`, `len`
- copy GET_REPORT data if needed
- send a small completion event to the HID control task
- return

The HID control task then:

- matches completion against `active_request`
- clears active state
- runs the continuation
- submits the next queued request

Completion matching should at least use:

- `dev_addr`
- `instance`
- `report_id`
- `report_type`
- request kind

The callback-side completion queue is an internal wakeup channel for the one
active transfer, not a report/event queue. Completion callbacks should ignore
stale completions that do not match the current active transfer. The HID async
task should also discard stale wakeups before submitting a new transfer, so a
late completion from an old/canceled request cannot make the next active
request wait forever.

## Why Linux Wait Cannot Be Used Directly

Historical note: this restriction applied when probe/driver code still ran in
TinyUSB callback or host-owner context. The active lifecycle/workqueue model
does preserve Linux waits in ordinary task context; only callbacks and the
TinyUSB/executor owner tasks are forbidden from blocking on the bridge.

Linux call sites often assume this shape:

```c
hid_hw_request(hid, report, HID_REQ_GET_REPORT);
hid_hw_wait(hid);
/* same C stack continues with updated report data */
```

That cannot be preserved literally in TinyUSB callbacks or in our current host
task model. Once we return from the function, the C stack is gone. Therefore
each such site must become an explicit state machine:

```text
STATE_NEEDS_GET
  queue GET_REPORT
  return

GET_REPORT completion
  copy data into the Linux report/state that the old code expected
  advance to STATE_NEEDS_SET or STATE_DONE

STATE_NEEDS_SET
  queue SET_REPORT
  return

SET_REPORT completion
  advance to STATE_DONE or failure
```

Do not try to make `hid_hw_wait()` block on a semaphore/event group. That was
the failure mode this direction is meant to avoid.

## First Concrete Target

Start with resolution multiplier support in `hid-input.c`.

Converted upstream anchors:

- `hid-input.c:1978`: `hid_hw_request(... HID_REQ_GET_REPORT)`
- `hid-input.c:1979`: `hid_hw_wait(hid)`
- `hid-input.c:2007`: `__hid_request(... HID_REQ_SET_REPORT)`

Reason this is a good first target:

- It is already isolated.
- It has clear GET then SET behavior.
- It is less broad than enabling all driver raw/control hooks.
- It exercises the exact hardware-wait conversion we need.

Expected conversion:

1. Keep the upstream lines commented next to the port-specific replacement.
2. Add a small resolution-multiplier async context.
3. Queue GET_REPORT only when the old code would have done it.
4. On GET completion, fill the report values needed by the old logic.
5. Queue SET_REPORT when `update_needed` remains true.
6. On SET completion, call/trigger the equivalent of the old refresh path:
   `hid_setup_resolution_multiplier(hid)`.

## Later Targets

After resolution multiplier works:

1. LED output reports (transport implemented; hardware route checkpoint
   pending):
   - `.request(HID_REQ_SET_REPORT)` uses interrupt OUT only for OUTPUT on an
     interface that exposes it and EP0 otherwise
   - FEATURE/raw requests stay on EP0; `.output_report()` is interrupt-only
   - `usbhid_start()` clears boot-keyboard NumLock through this route
2. Raw SET_REPORT transport branch (completed by the later generic bridge):
   - the specialized raw GET/SET builders were removed
   - `usbhid_get_raw_report()` and `usbhid_set_raw_report()` again follow the
     upstream bodies and call generic `usb_control_msg()`
   - `hid-razer.c` keeps its upstream `hid_hw_raw_request()` call active; the
     caller waits in task context while TinyUSB remains free to complete it
   - `.raw_request` now returns the completed USB byte count or errno, matching
     upstream; it no longer means merely "queued"

3. Driver feature/raw hooks:
   - `hid-vivaldi-common.c` is the first feature_mapping conversion; it queues
     one async GET_REPORT for the feature report and completes outside TinyUSB
     callbacks
   - `hid-kye.c` is the first imported probe-time SET_REPORT user that keeps
     upstream `hid_hw_request()` in place and relies on the async `.request`
     transport adapter
   - only enable one driver/hook at a time
   - keep raw_event/report hooks disabled until their callback ownership is
     explicit
   - drivers needing blocking wait or synchronous raw GET_REPORT stay out until
     converted; delayed work/timers are available but still need driver-by-driver
     ownership review

4. HIDRAW/proxy:
   - current `hid-core.c` HIDRAW branch intentionally does not buffer
     userspace/proxy data
   - reconnect later only after ownership and queueing are explicit

5. Battery/power supply:
   - at the time of this historical plan, this was compiled out by
     `CONFIG_HID_BATTERY_STRENGTH`; the current port instead has a reduced
     detached-snapshot boundary documented in the current-status notes
   - the old enablement blocker was GET_REPORT continuation support; the
     current async-backed request path has since removed that blocker

## Nonblocking Rules

Hard requirements:

- No callback waits for USB, parser, queue, lifecycle, or request-completion
  progress. Normal shared-state publication briefly acquires the transport
  mutex; no holder depends on further TinyUSB work. Exceptional diagnostics
  for failure of that mutex itself set a fixed lifecycle-notification bit.
- No blocking logging in TinyUSB callbacks.
- No semaphore/event-group wait to emulate `hid_hw_wait()`.
- No flash writes from runtime HID paths.
- No heap allocation in completion callbacks.
- No unbounded queues.
- No silent drop of control requests.
- No transport mutex held across TinyUSB/HCD calls, host-task handoff, parser
  entry, heap operations, logging, or a blocking wait.

Soft preference:

- One task should normally own TinyUSB HID control/output submit.
- Direct submit elsewhere is acceptable only when it keeps the lifecycle simpler
  and remains nonblocking.

## Porting Rules Reminder

When changing Linux-derived files:

- keep the original upstream line/block commented next to the replacement
- put the port-specific replacement immediately below it
- explain why the port differs
- preserve upstream function names/order/flow where possible
- do not invent a local mini-parser or mini-mapper when upstream logic can be
  preserved behind a small async boundary

For async conversion, the old `hid_hw_wait()` line should remain visible next to
the state-machine boundary that replaces it.

## Open Questions

- What exact return code should deferred request submission use at each old
  synchronous call site? Some Linux functions treat negative return as failure,
  so not every call site can be converted by returning `-EINPROGRESS`.
- Should the first queue be global, per HID device, or per interface? Start
  global with one active request unless a real device needs more.
- What timeout policy should apply to an active request? The control task can
  own a tick deadline and fail the continuation without blocking.
- How should completion mismatch be reported without logging from callbacks?
  Likely a short debug event queue, same style as current HID host log events.
