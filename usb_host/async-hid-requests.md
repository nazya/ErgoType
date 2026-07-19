# Async HID Requests Direction

> Historical conversion plan. The active port now uses task-owned
> sync-over-async request/wait paths; TinyUSB callbacks publish bounded records
> and do not run Linux continuations. See `async-hid-progress.md` and
> `deferred-hid-drivers.md` for the current boundary.

This note records the next HID host direction after the working generic HID
input slice.

The current working model is:

- TinyUSB interrupt reports are parsed directly from `tuh_hid_report_received_cb()`.
- Linux HID parser/input mapping stays active.
- Linux synchronous hardware request/wait paths are disabled until they are
  converted to async state machines.

The next goal is to re-enable selected driver/control behavior without bringing
back Linux-style blocking waits.

## Current Anchors

Repo paths are relative to `../ErgoType` unless noted.

- `usb_host/task.c:67`: host task currently loops on `tuh_task()`.
- `usb_host/usbhid.c`: TinyUSB-to-Linux USB HID transport glue now holds the
  `hid_ll_driver` boundary that was split out during earlier bring-up work.
- `usb_host/usbhid.c:328`: interrupt report callback copies the TinyUSB const
  report into a writable stack buffer and calls `hid_safe_input_report()`.
- `usb_host/usbhid.c:442`: `.request` queues `hid_hw_request()` through
  `hid_async_queue_report()`, matching upstream `usbhid_submit_report()` as a
  best-effort async submit path.
- `usb_host/usbhid.c:455`: `.wait` is currently a no-op with the
  upstream `usbhid_wait_io()` anchor.
- `usb_host/usbhid.c:466`: `.raw_request` queues raw SET_REPORT
  through `hid_async_queue_raw_set_report()` and still returns `-ENOSYS` for
  raw GET_REPORT until each caller has an explicit async continuation.
- `usb_host/hid_async.c:217`: `hid_async_queue_raw_get_report()` queues raw
  GET_REPORT for explicit async continuations. It mirrors Linux
  `usbhid_get_raw_report()` handling for unnumbered reports by keeping report
  ID 0 in byte 0 and receiving payload at byte 1.
- `usb_host/hid_async.c`: the generic `usb_control_msg()` /
  `usb_interrupt_msg()` bridge is kept as a deferred `#if 0` block. Current
  linked drivers use report GET/SET/OUTPUT paths only; blocking USB control
  call sites must still be converted deliberately before their drivers are
  linked.
- `usb_host/usbhid.c:494`: `.output_report` queues interrupt OUT
  reports through `hid_async_queue_output_report()` and returns after enqueue.
- `usb_host/usbhid.c:509`: `.idle` defers synchronous `SET_IDLE`.
- `usb_host/usbhid.c`: synchronous `usb_control_msg()` currently returns
  `-ENOSYS`; any caller reaching it must be converted or kept out of CMake.
- `usb_host/linux/drivers/hid/hid-core.c:2097`: upstream `__hid_request()`
  expects `hid_hw_raw_request()` to return data immediately.
- `usb_host/hid_workqueue.c`: active `schedule_work()` users run from a
  dedicated firmware workqueue task.
- `usb_host/hid_timer.c`: active `timer_list` users run from a dedicated
  firmware timer task.
- `usb_host/linux/drivers/hid/hid-input.c:1874`: LED worker path can use
  `SET_REPORT`; active `schedule_work()` reaches the async `.request` path.
- `usb_host/linux/drivers/hid/hid-input.c:1978`: resolution multiplier
  `GET_REPORT + hid_hw_wait()` is converted to explicit async continuation.
- `usb_host/linux/drivers/hid/hid-input.c:2007`: resolution multiplier
  `SET_REPORT` is queued through the HID async task.

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
2. Raw SET_REPORT transport branch:
   - `usbhid.c` queues raw SET_REPORT through
     `hid_async_queue_raw_set_report()`.
   - `hid_async_queue_raw_get_report()` now provides the matching raw
     GET_REPORT building block for driver-specific continuations. It does not
     make `.raw_request` synchronous; callers still need explicit state.
   - `hid-razer.c` keeps its upstream `hid_hw_raw_request()` call active; the
     TinyUSB transport boundary turns that SET_FEATURE payload into an async
     queue entry.
   - The `.raw_request` return value means "queued", not "USB transfer
     completed"; drivers that need returned data or completion status still need
     explicit state.

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
   - currently compiled out by `CONFIG_HID_BATTERY_STRENGTH`
   - do not enable until GET_REPORT query path has async continuation support

## Nonblocking Rules

Hard requirements:

- No blocking waits in TinyUSB callbacks.
- No blocking logging in TinyUSB callbacks.
- No semaphore/event-group wait to emulate `hid_hw_wait()`.
- No flash writes from runtime HID paths.
- No heap allocation in completion callbacks.
- No unbounded queues.
- No silent drop of control requests.

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
