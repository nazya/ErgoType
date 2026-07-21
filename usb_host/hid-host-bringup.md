# HID Host Bring-up and Limits

This is the final short record of the HID host regression investigation. It
replaces the old experiment logs and describes the current design, RAM limits,
diagnostics, and remaining risks.

## Current Status

The following paths have produced input events on hardware:

- a generic USB keyboard, including the keyboard interface of a composite
  ErgoType device;
- the Holtek keyboard emulator, including its report-descriptor fixup;
- the active haptic-touchpad emulator, including pointer events and the
  multitouch/haptic probe path;
- the PMW pointing-device path, independently of USB host input.

Working firmware runs before attaching a heavy HID device have reported roughly
43-48 KiB of free FreeRTOS heap. That is an observed operating margin, not a
guaranteed allocation size: HID parsing also needs a sufficiently large
contiguous block while a device is being added.

The original intermittent generic-keyboard failure did not produce a clean
single-commit regression result. Two independent problems were present:

- real heap pressure made HID parsing and binding sensitive to small changes;
- an unreliable USB-C breakout/breadboard connection sometimes produced no
  mount activity at all, and moving the wires made reports start arriving.

The deterministic Holtek failure was separate: the driver must be selected in
both the build and the port configuration, and its fixed Consumer range is too
large for the Linux dense HID-field representation on RP2040 without a cap.

## Host Lifecycle

The active path is:

```text
TinyUSB mount
  -> capture HID instance/interface identity and wake lifecycle task
  -> publish global mount state from the existing USB cache epoch
  -> task-side reset-state observation, when recovery owns the port
  -> task-side device/string descriptor pre-probe
  -> build usb_device/usb_interface from the lifecycle cache and raw snapshot
  -> atomically replace the probe token with the live HID transport object
  -> hid_add_device() enters Linux usbhid_parse()
  -> validate HID class descriptor and derive report size
  -> exact async-backed SET_IDLE, with Linux-compatible ignored status
  -> exact async-backed GET_DESCRIPTOR(report), capped at 4 KiB
  -> Linux HID report parse and release the transient descriptor buffer
  -> synchronous driver match and probe
  -> register Linux input devices and inactive evdev handles
  -> drain finite probe-originated control/OUT requests
  -> privately prepare all evdev queues, then open all matching handles
  -> publish all devmon ADDs/writers only after every open succeeds
  -> atomically publish driver_ready unless physical detach already won
  -> direct TinyUSB endpoint-IN receive starts
  -> Linux HID input mapping -> evdev -> KeyD
```

All linked HID drivers are registered before TinyUSB enumeration. Therefore
`device_add()` treats failure to bind as final for this firmware: it removes the
unbound HID-bus object and returns the error to `usbhid` for destruction. There
is no module loader, runtime `new_id` writer, or useful later rebind path.

Each interface of a composite USB device is handled independently. An
unsupported WebHID interface may produce `WARN: HID_IGNORED` while the keyboard
and mouse interfaces continue working. CDC is not HID and never enters this
probe path.

Interrupt-IN receive is armed from `usbhid_open()` or `usbhid_start()`, not
from probe. Ignored or failed interfaces therefore do not keep delivering
reports to an unbound Linux HID object.

The corresponding remove path is split at the same ownership boundary:

```text
TinyUSB physical remove/app-driver close
  -> publish disconnect for every live interface and stop direct IN
  -> rotate/cancel the physical-address async epoch and retire the cache entry
TinyUSB HID class close
  -> invalidate the exact pending probe token
  -> idempotently publish the exact interface disconnect
lifecycle task
  -> usbhid_disconnect(struct usb_interface *)
  -> synchronize async/report/I/O owners
  -> hid_destroy_device() and free the USB-HID transport object
```

The physical publication is intentionally earlier than HID class close: it is
the producer fence for both ordinary devices and hubs. TinyUSB omits the common
unmount callback for hubs, so the raw application-driver close remains the
device-level fallback. No callback waits for USB/lifecycle progress or frees
Linux-owned state; bounded publication may briefly take the shared transport
mutex, whose holder never depends on further TinyUSB progress.

Lifecycle destruction uses one exact-interface completion barrier after that
producer fence. It cancels only async slots whose owner is the exact `hid`, then
waits until aggregate I/O is zero, no executor slot retains the HID, and direct
interrupt-IN has no owner, deferred host pass, or pending physical-detach fence.
This is the TinyUSB adapter for upstream `usb_kill_urb()`: durable state is the
condition, the existing per-interface wait head carries coalesced wake edges,
and there is no polling or additional FreeRTOS object.

Linux-shaped output requests are serialized by the HID async task. For
`.request(HID_REQ_SET_REPORT)`, OUTPUT reports use interrupt OUT when present
and otherwise EP0; FEATURE reports and `.raw_request()` stay on EP0, while
`.output_report()` is interrupt-only. Boot-keyboard start clears NumLock
through the `.request()` route.

EP0 report requests are submitted as direct asynchronous TinyUSB control
transfers from the host owner task. Completion is matched by request serial and
preserves the real transfer result and actual length. SET_IDLE and upstream
raw GET/SET use the generic device-level EP0 lane; an interface owner tag makes
stop/cancel wake the task immediately. HID report requests and generic control
messages share same-device EP0 ordering; completed GET_REPORT parser storage no
longer blocks unrelated physical EP0 work. Caller tasks may wait for the Linux
ll-driver contract, but TinyUSB callbacks never wait for request completion or
run the Linux continuation. If the bounded broker is full, either synchronous
caller task sleeps through a stack-owned FIFO admission waiter instead of
polling. A normal
slot release or matching teardown wakes it to retry the durable enqueue
predicate for at most one second; the transfer timeout starts only after
admission. Nonblocking report
and CLEAR_HALT work cannot steal logically reserved capacity, and recovery's
dedicated slot remains outside ordinary admission. A saturated CLEAR_HALT does
not poll that pool: the report task parks its durable reset-work state until a
normal slot is released or an unused reservation is removed. Capacity changes
during the unlocked enqueue attempt are latched, so the transition to sleep
cannot lose the only wake edge. The retry timer is reserved for actual
interrupt-I/O protocol failures; the report task sleeps until a capacity edge
or the absolute eight-second local saturation deadline. Unrelated notifications
only recheck that same durable predicate and deadline, without periodic polling.

Interrupt OUT is also submitted directly from the host owner. Each asynchronous
`.request()` SET owns an exact enqueue-time report snapshot until completion,
matching upstream's FIFO semantics. `.output_report()` uses the upstream
synchronous helper over the generic bridge and keeps its caller buffer alive
while blocked. Both sources are ordered per device endpoint instead of globally
or by ll-driver hook, and TinyUSB preserves the real transfer result, actual
length, and request serial. The old report-sent facade, which could only
manufacture success for whichever request happened to be active, is not part
of this path.

Interrupt IN follows the same exact-completion boundary without entering the
serialized control/OUT broker: every HID interface may have one independent IN
transfer. The port selects the first interrupt-IN endpoint, as upstream does,
and arms upstream's per-interface `inbuf` for the largest parsed INPUT report
(report ID included, capped at 16 KiB). Completion publishes raw result/length
in the exact fixed arm slot, which already retains `inbuf`; the report task
claims that slot and runs the Linux HID parser before the buffer can be rearmed.
The backing allocation is INPUT-only, at least one 64-byte packet, and rounded
through the final advertised endpoint packet because the pinned PIO HCD copies
that packet before checking the logical remainder. The TinyUSB transfer and
parser lengths remain the exact INPUT size. This removes the old callback copy,
retains the real HCD result, and keeps failed or stalled payload out of Linux
HID and KeyD. On unplug, callback-published state is fenced through the report
and host tasks until TinyUSB has completed class/HCD close; only then may task
teardown free `inbuf`. The completion callback does not select HID parser
policy; it snapshots the byte-sized `CLOSED/RESUMING/OPEN` transport state.
For non-`ALWAYS_POLL` input the report task drops the initial completions during
Linux's 50-ms `HID_RESUME_RUNNING` drain, then enables parser delivery. It also
applies stopping/recovery policy under the same exact-generation fence. Linux
relies on the USB reset-default Report
protocol, so there is no retained mode or port-only BOOT suppression. STALL
queues the standard endpoint clear-halt request
through the generic per-device EP0 lane. After remote success the TinyUSB host
owner resets the PIO endpoint toggle to DATA0 and rearms. If close races an
already active request, success still resets that local toggle and wire failure
still requests device reset, matching running Linux `reset_work`; only rearm is
suppressed while closed. Stop/unplug cancels the obsolete transport epoch.
Failed physical arm is also policy-free in the host owner: it publishes its
generation/open revision, and the report task starts the same upstream bounded
retry only if that epoch is still current. Protocol errors use that delayed
retry as well. The remaining difference
from Linux is the implementation behind `usb_queue_reset_device()`. After
recovery exhaustion this port makes the same reset decision, but its lifecycle
owner performs full TinyUSB teardown/re-enumeration so configured class state
cannot survive an electrical reset. Root and hub-child paths run behind a
global EP0 gate; the exact old cache generation is drained before a fresh mount
may probe. The reset owner waits on durable retire/mount/enumeration and EP0-idle
publications instead of polling every 10 ms. Its only timed waits are exact
phase deadlines; hub retry is woken again after the reserved control slot has
actually been released. TinyUSB's central control-stage transition publishes
global idle for native hub housekeeping and every abort/remove release too.
Root admission waits for matching enumeration, global control idle, or the one
enum terminal instead of repeatedly deferring an attach which the single host
owner must reject.

The SHA-pinned TinyUSB HID class now scans the bounded current-interface extras
instead of assuming strict interface -> HID -> endpoint order. Like Linux's USB
HID transport, it accepts the HID descriptor after endpoint[0], opens no more
than the interface's advertised endpoint count, and publishes its class slot
only after endpoint success. Enumeration now skips SET_IDLE, SET_PROTOCOL, and
the duplicate report-descriptor read through the shared 512-byte enumeration
buffer. It mounts with a NULL descriptor pointer and publishes only TinyUSB's
ephemeral class identity. Each fixed 16-byte probe slot
pairs a host-owned publication serial with a lifecycle-owned handled serial;
unmount revokes the former and an old probe never clears a fast-replug record.
After the exact global-mount fence and shared device/string pre-probe,
lifecycle builds the retained
interface shim. Linux-shaped `usbhid_parse()` then validates its HID descriptor,
sends the upstream SET_IDLE request, derives the report size, and performs the
single authoritative read with an exact-size local buffer and four real USB
attempts. SET_IDLE status is ignored as upstream does; no generic SET_PROTOCOL
is sent because USB reset already selects Report protocol. Local async-pool admission
waits do not consume those attempts and are bounded by the control timeout. The
firmware-only full device-descriptor refetch separately gets four accepted
attempts with 100-ms backoff and carries both cache and TinyUSB address epochs
across the string chain. The configuration-descriptor limit remains 512 bytes.

EP0 cancellation cannot wait forever for a PIO completion event. The executor
allows three two-SOF/FIFO fences for TinyUSB's SETUP/DATA/ACK stages. If the
exact old daddr + callback + serial still owns EP0, a SHA-pinned host helper
finishes it with a synthetic TIMEOUT; a replacement owner is never touched.

## What `HID_REPORT_SKIP` Meant

`ERR: HID_REPORT_SKIP` used to be a late receive-path symptom rather than the
original parser error: either no active `hid_device` matched or the parser
rejected the report. The exact fixed-slot handoff removes both meanings. It now
means a nominally successful PIO/TinyUSB giveback violated the armed buffer
contract, for example `actual_len > bufsize`; such payload is rejected before
`hid_safe_input_report()`. A late aborted completion misses its cleared serial
silently, and the parser return value is not a transport failure.

If `HID_REPORT_SKIP` appears now, inspect the endpoint giveback and armed length;
it is no longer evidence of a key mapping or ordinary parser rejection.

## Why Wide Usage Ranges Exhaust RAM

The large allocations are not a single global key-code lookup table. Linux HID
materializes usage metadata separately for every parsed report field, and that
field remains allocated for the lifetime of the connected interface.

On the current RP2040 ABI:

```text
sizeof(struct hid_field) = 100 B
ordinary field           = 100 + usage_count * 40 B
INPUT ARRAY field        = 100 + usage_count * 32 B
                              + report_count * 8 B
```

Examples:

| Usages | Requested persistent allocation | Relevant descriptor |
| ---: | ---: | --- |
| 675 | 21,748 B (21.2 KiB) | Current six-slot Consumer array |
| 1,024 | 32,916 B (32.1 KiB) | Six-slot ErgoType Consumer range `0x000..0x3ff` before the cap |
| 12,288 | 393,324 B (384.1 KiB) | One-slot Holtek fixed range `0x000..0x2fff` before the cap |

The parser also keeps three temporary arrays costing 9 B per local usage. At
675 entries they require 6,075 B and are freed after parsing. With the former
12,288 limit a one-shot reserve would require 110,592 B before the persistent
field could even request 393,324 B. The older incremental 2,048-to-4,096 growth
temporarily needed both allocations at once, or 55,296 B.

These are allocation requests. If a persistent report or field allocation
fails, the port now propagates `-ENOMEM` through parser and driver-core cleanup;
the lifecycle task rejects and destroys the whole interface instead of binding
a partial report graph. Upstream's non-OOM `HID_MAX_FIELDS` truncation remains.

Consequently, merely moving these objects to flash is not possible: report
descriptors determine their contents at runtime. A static generated mapping or
sparse range representation would require a larger departure from the Linux
parser.

## The 675-Usage Limit

`HID_MAX_USAGES` is currently 675. It is a per-field entry capacity, not a
global table and not a universal maximum numeric usage ID.

For the relevant Consumer arrays whose minimum is zero, 675 entries retain
selectors `0x000..0x2a2`. `0x2a2` is the highest Consumer usage that currently
maps to an event KeyD accepts. The explicit Linux Consumer mappings above it
map to event codes that the current KeyD port drops, so retaining them would
consume RAM without producing usable output.

Important boundary behavior:

- a wider `Usage Minimum..Usage Maximum` range keeps its first 675 entries;
- a nonzero range therefore keeps `minimum..minimum+674`;
- an individual Usage ID may numerically exceed `0x2a2`; the cap counts entries;
- a 676th separate Usage item or `Report Count > 675` is a parse error;
- an array selector outside the retained entries produces no input event and
  emits `WARN: HID_USAGE_CAP_DROP` when the new selector appears.

The cap should only be raised after the downstream KeyD mapping is expanded
and the resulting per-field RAM cost is measured. At the current limit, one
maximal field plus live parser-local arrays is about 33 KiB before allocator
overhead and the rest of the HID device state.

## Retained Memory and Lifecycle Changes

The working configuration intentionally keeps these changes:

- explicit usage ranges reserve parser-local storage once at the final bounded
  size instead of repeatedly growing and copying it;
- `HID_MAX_USAGES=675`, with `HID_USAGE_CAP_DROP` for runtime visibility;
- the CMake HID source list is the driver allowlist, and `CONFIG_HID_*` symbols
  in `hid_compat.h` mirror only the linked drivers and supported features;
- builtin driver descriptors stay const in flash, while their mutable runtime
  records use linker-owned static `.bss` storage instead of startup heap
  allocations;
- the unreachable per-driver `new_id` sysfs attribute is not registered;
- HIDDEV remains unlinked because the firmware has no hiddev consumer;
- synchronous bind failure removes the unused HID object;
- interrupt receive begins only after successful bind/open;
- `cancel_work_sync()` physically unlinks pending work from the FIFO and closes
  the dequeue-to-running race before a driver may free its object.

Before static builtin runtime storage, registering 23 drivers and their
per-driver state/attribute data consumed 4,952 B of FreeRTOS heap in the tested
build. The current 14 runtime records occupy 784 B of static `.bss`. Static
`.bss` still consumes physical RAM, but it no longer depletes or fragments the
runtime heap. Excluding a driver from CMake also excludes its runtime record
while leaving its source in the repository.

The current allowlist is `hid-generic` plus A4Tech, Chicony, Creative SB0540,
Cypress, Holtek keyboard, ITE, Kye, Primax, PXRC, Rapoo, Razer, Saitek, and
Zydacron.

Current memory-related settings are:

```text
configTOTAL_HEAP_SIZE       218.75 KiB
configMINIMAL_STACK_SIZE    384 words
hid_async_task              512 words
keyd_task                   5,120 words
TinyUSB host task           512 words
HID report task             1,024 words
PIO_USB_EP_POOL_CNT         8
PIO_USB_DEVICE_CNT          4
PIO_USB_ROOT_PORT_CNT       1
```

The workqueue, timer, and HID lifecycle split still costs three tasks. The
first two use 384-word stacks and lifecycle uses 512 words; together with task
overhead they consumed about 6.6 KiB in the measured build. Consolidating them
is a later architecture change, not part of this bring-up fix.
The workqueue task now owns a notification wake rather than a one-entry queue;
its producers and synchronous cancellation paths share a dedicated
priority-inheritance mutex and wait on durable conditions without masking
interrupts or polling. The timer task is a parallel task-only synchronization
domain with its own mutex and notification wake. TinyUSB unmount publishes only
stopping; the report task claims retry cancellation under an `io_pending` lease
and waits for the running timer callback outside every transport lock.
Current workqueue entry owners are the KeyD, HID timer/lifecycle, and
workqueue tasks. TinyUSB callbacks only publish disconnect/report state to
those owners. Workqueue invariant checks execute their FreeRTOS operation
before testing a captured result; `configASSERT()` never contains the
operation itself. Fixed-size `async_msg()` diagnostics are used because the
normal logger's roughly 2 KiB local frame does not fit the 384-word workqueue
and timer stacks.
The transport-wide invariant audit also leaves no function call or predicate
inside an active `configASSERT()`. A collision in the one-slot async diagnostic
path increments the UI warning counter before dropping the newer text.

The callback-safe lifecycle transport pool now has a 2,756 B payload (plus
allocator overhead) in the FreeRTOS heap at startup with the current
`CFG_TUH_DEVICE_MAX=4`, `CFG_TUH_HUB=1`,
`CFG_TUH_HID=4` configuration. It includes four 16-byte probe-identity slots
and the single aligned 256-byte device/string descriptor scratch; callbacks own
neither payload. Lifecycle serializes probe, so
`usbhid_parse()` allocates at most one exact-size report descriptor transiently
(up to 4 KiB), owns it through asynchronous completion/cancel, and frees it
before returning. Include both the persistent pool and transient descriptor in
post-enumeration and haptic heap checks.

The report executor now has one persistent queue. Five 28-byte ordinary-control
results plus the 84-byte FreeRTOS queue object request 224 B and occupy a
232-byte heap_4 block. Direct interrupt-IN completion instead uses four fixed
32-byte slots (128 B in scratch X), with no queue allocation or address-based
HID lookup. Completion validates the slot through its one live registry owner.
Removing the former four-entry input queue returns its 176-byte heap_4 block
and removes one 4-byte queue handle from `.bss`; expanding the old 20-byte slots
adds 48 B to scratch X. Net live RAM occupancy falls by 132 B and one persistent
heap block, with no allocation churn per report. Probe-owned GET completion
bypasses the remaining control queue and reuses its request buffer through one
per-interface pointer; the former global handoff is gone. Relative to that
earlier implementation this separately saved 24 B of persistent heap and 36 B
of `.bss`; the temporary GET header is 16 B. The coalesced reconcile table
costs 32 B of static RAM. The async request is 60 B and its slot is 88 B. Ten
metadata slots request 880 B from heap_4 and occupy an 888 B block.
Device/string policy and its aligned 256-byte scratch live in the existing
lifecycle transport pool, whose 2,756-byte payload occupies a 2,768-byte block.
Together those two persistent blocks use 3,656 B, before synchronization. The
explicit transport mutex occupies a 96-byte heap_4 block; these three core
transport allocations therefore use 3,752 B. The remaining 232-byte control
queue is additional. All are startup allocations and do not churn during
attach/report traffic. Lifecycle's indexed notification removes its former
96-byte queue block. Exact endpoint callbacks add 1,280 B of TinyUSB state.
Each attached HID interface also owns the two priority-inheritance mutexes
present in upstream's lifecycle: generic HID's `ll_open_lock` and
`usbhid->mutex`, 96 B each in FreeRTOS. Restoring the latter's 4-byte handle
also rounds its heap_4 object up by 8 B, for a total lifecycle cost of 200 B per
interface (600 B for a three-interface emulator). These objects are allocated
once at probe and destroyed after the complete disconnect fence; attach/report
traffic does not churn them.
The workqueue similarly replaces its former 96-byte one-entry wake queue with
one 96-byte mutex block, so runtime heap use is unchanged; its task handle and
stack-waiter head add 8 B of `.bss`. The timer makes the same 96-byte
queue-to-mutex exchange; its task handle and stack-waiter head add another 8 B
of `.bss` without changing persistent heap use.
Direct IN/OUT leave one-byte class placeholders. Each attached HID interface
owns one task-allocated receive buffer of
`max(64, round_up(input_size, wMaxPacketSize))` bytes. A normal 64-byte backing
costs a 72-byte heap_4 block; the 16 KiB logical limit plus worst packet tail
can cost up to a 16,456-byte block. There is no allocation or free per report.
Host transfer storage now occupies 708 B in scratch X and ends 1,340 B below
the core-1 stack. Removing transitional descriptor ownership first shrank
`struct usbhid_device` from 248 B to 240 B. Restoring upstream's lifecycle
mutex handle and the stack-owned close-fence waiter makes it 248 B and returns
its heap_4 block from 248 B to 256 B per attached HID. The linked image reports
243,136 B of `.bss` and keeps 200 B of main-SRAM link headroom.

Queued asynchronous SET reports now allocate their upstream-style snapshot at
the exact report size and release it after completion or fenced cancellation.
heap_4 coalesces adjacent frees, so repeated equal-size LED/haptic traffic must
not produce a falling `free` value. Mixed report sizes interleaved with
persistent device allocations can still leave temporary holes: `largest` may
fall even after total `free` returns, which is fragmentation rather than a leak.

The haptic driver now allocates at most five application effect records and
five report snapshots. The adjacent commented upstream allocation remains 96
slots; that userspace-oriented capacity would
reserve 91 unused records plus 91 device-sized report buffers from the same
heap needed to probe the remaining interfaces of a composite touchpad.
Its two driver mutexes and ff-core mutex are heap-backed FreeRTOS semaphores,
unlike embedded Linux mutexes. Every construction is now checked before first
use. Failure unwinds as probe `-ENOMEM` and clears the provisional
`EV_FF`/`FF_HAPTIC` bits, so the multitouch driver can retain ordinary pointer
input without publishing an FF-capable device whose `dev->ff` is NULL.

Each multitouch device now owns one devres-managed active-slot bitmap after
`maxcontacts` is finalized. Its payload is 4 B through 32 contacts and at most
32 B at the `u8` 255-contact maximum, plus heap_4 metadata. This small bounded
allocation replaces using a single 32-bit flags word as both a lock bit and an
unbounded slot bitmap; the former RUNNING bit 32 and any slot at or above 32
wrote into adjacent `mt_device` state on RP2040.

## Log Reference

| Message | Meaning |
| --- | --- |
| `WARN: HID_IGNORED` | No linked driver accepted this HID interface; other composite interfaces are unaffected. |
| `ERR: HID_ADD_FAIL` | `hid_add_device()` failed for an error other than `-ENODEV`, such as parse, registration, or start failure. |
| `ERR: HID_EVDEV_ACTIVATE_FAIL` | Driver probe completed, but post-probe devmon publication or input open failed; the fully bound HID is torn down. |
| `ERR: HID_USB_DEV_ALLOC_FAIL` | The bounded physical-device cache has no reusable slot. |
| `ERR: HID_PROBE_DEFER_FAIL` | Mount/pre-probe identity could not be retained, the bounded probe slots were occupied, or device-descriptor pre-probe exhausted its attempts. |
| `ERR: HID_PROBE_NOMEM` | A parser/probe allocation failed, including the transient exact-size report-descriptor buffer. |
| `ERR: HID_USB_PARENT_MISSING` | A child was observed after its hub cache epoch disappeared; it was rejected instead of attached to the root hub. |
| `ERR: HID_REPORT_SKIP` | A successful endpoint giveback exceeded or otherwise violated its armed buffer bounds; payload was rejected before parsing. |
| `ERR: HID_RX_REARM_FAIL` | Receive could not be armed again after a report callback. |
| `ERR: HID_RX_STALL` | Interrupt IN stalled. Payload was discarded and asynchronous endpoint clear-halt recovery started. |
| `DBG: HID_CLEAR_HALT_OK` | Remote endpoint halt was cleared; the host owner may now reset the local PIO toggle to DATA0 and rearm. |
| `ERR: HID_CLEAR_HALT_FAIL` | Remote clear-halt transfer failed; terminal recovery queues coordinated device teardown/re-enumeration. |
| `ERR: HID_RX_XFER_FAIL` | Interrupt IN failed or timed out. Payload was discarded and upstream-style delayed retry started. |
| `ERR: HID_RX_INVARIANT` | Interrupt-IN completion did not match its live owner/epoch/buffer tuple. The callback parked it before parser publication. |
| `ERR: HID_ASYNC_HOST_OWNER` / `HID_EP0_ABORT_OWNER` | A deferred TinyUSB call ran outside the registered host owner, or a validated physical EP0 abort found no matching TinyUSB logical owner. |
| `ERR: HID_ASYNC_XFER_TUPLE` | A serial-matched broker completion violated its address/epoch/endpoint/payload tuple; the existing task-side `-EIO` retirement path ran. Zero-data EP0 completion is normalized from TinyUSB/PIO's eight-byte SETUP accounting to Linux's zero payload bytes before this check. |
| `ERR: HID_ASYNC_HUB_PIN` | A pinned hub-reset slot changed identity during its immediate host-owner completion handoff. |
| `DBG: HID_RESET_Q` | Terminal report recovery published an exact physical-device reset to lifecycle. |
| `DBG: HID_RESET_OK` | Old transport state retired and a fresh same-topology TinyUSB mount completed. |
| `ERR: HID_RESET_FAIL` | Coordinated teardown/reset/re-enumeration exhausted its bounded phase or hub retry deadline. |
| `ERR: HID_ASYNC_CANCEL_FAIL` | Pending async HID requests could not be cancelled during detach. |
| `ERR: HID_CTRL_DISPATCH_FAIL` | A completed control GET could not be published to its ordinary report queue or direct probe owner. |
| `ERR: HID_SUBMIT_TO` / `ERR: HID_XFER_TO` | A request exceeded its bounded timeout. Pre-probe allows one second for local slot admission without consuming a wire attempt, then uses the normal five-second Linux USB GET timeout; other generic messages use their caller-supplied timeout. |
| `ERR: HID_DEV_DESC_XFER` / `HID_DEV_DESC_SHORT` / `HID_DEV_DESC_TYPE` | One full device-descriptor refetch attempt failed, returned fewer than 18 bytes, or returned the wrong descriptor type. The first three retain the pending HID and retry after 100 ms; the fourth terminates that preprobe. |
| `ERR: HID_EP0_EVENT_LOST` | Three bounded SETUP/DATA/ACK drains found the same exact EP0 owner; TinyUSB received a synthetic TIMEOUT giveback for the lost HCD event. |
| `ERR: HID_EP0_CALLBACK_LOST` | TinyUSB EP0 was already idle after bounded drains, but the async slot had no callback completion; teardown remains bounded. |
| `ERR: HID_EP0_OWNER_MISMATCH` | The serial-safe recovery helper found a different live EP0 owner and deliberately left it untouched. |
| `ERR: HID_WQ_NOT_READY` / `HID_WQ_LOCK_FAIL` / `HID_WQ_UNLOCK_FAIL` | A task-side workqueue mutex invariant failed. The checked FreeRTOS call was evaluated before the following assert. |
| `ERR: HID_WQ_WAITER_BAD` / `HID_WQ_WAITER_LOST` / `HID_WQ_SELF_WAIT` | A synchronous workqueue waiter invariant failed outside TinyUSB callback context. |
| `ERR: HID_WQ_INIT_TWICE` | Workqueue initialization was invoked after its mutex had already been published. |
| `ERR: HID_LOCK_NOT_READY` / `HID_LOCK_TAKE_FAIL` / `HID_LOCK_GIVE_FAIL` | The shared transport mutex failed an invariant. Its callback-safe path publishes this reason as a lifecycle-notification bit; only lifecycle enters the logger, and the following assert uses the precomputed boolean. |
| `WARN: HID_USAGE_CAP_DROP` | A report used an array selector outside the retained 675-entry field lookup. |
| `DBG: HID_REPORT_OUT_Q` / `DBG: HID_REPORT_OUT_OK` | `.request()` routed an OUTPUT report through interrupt OUT and it completed. |
| `DBG: HID_REPORT_SET_Q` / `DBG: HID_REPORT_SET_OK` | `.request()` routed SET_REPORT through EP0 (FEATURE or no interrupt OUT) and it completed. |
| `DBG: EVDEV_KEY_Q` | A key event reached the evdev-to-KeyD queue. |
| `WARN: EVDEV_BATCH_CAP` | An unusual input device computed more than the bounded 62-value host batch; input remains best-effort. |

The old `USB_MOUNT_CB` and `HID_MOUNT_CB` callback markers are intentionally
gone: callback context no longer calls the logger. A connection that never
reaches TinyUSB still has to be diagnosed from USB enumeration/TinyUSB tracing,
power, wiring, D+/D-, the breakout, and the connector—not parser changes.

## Enabling Another HID Driver

Drivers remain in the repository but are linked selectively. To enable one:

1. Add its source to the HID allowlist in `CMakeLists.txt`.
2. Enable the matching `CONFIG_HID_*` symbol in `hid_compat.h`.
3. Do not enable optional FF, HIDDEV, HIDRAW, LED, battery, PID, or haptic
   feature symbols unless the required firmware proxy is also implemented.
4. Build and check both link-time RAM and runtime free heap.
5. Test enumeration, actual input/output, repeated unplug/replug, and the
   expected fallback to `hid-generic` for unrelated devices.
6. Record emulator or hardware coverage in `hid-emulator-coverage.md`.

A linked source with a missing config symbol can compile without registering
the intended driver. A config symbol without the matching source does not add
the driver. Keep both sides synchronized.

## Force Feedback and Remaining Risks

No gaming FF driver is active. The tested Stadia/`ff-memless` implementation is
preserved in checkpoint `hid: stabilize stadia ff teardown`, but both sources are excluded from CMake.

The generic `ff-core`, `hid-haptic`, `hid-multitouch`, evdev
upload/play/stop/erase boundary, workqueue bridge, and async output transport
are active for the standard haptic-touchpad path. The two-Pico emulator has
verified enumeration, pointer input, and haptic cursor feedback. A real
touchpad, strict output ordering, repeated teardown, and the new exact-control
completion checkpoint still need hardware coverage.

The current dirty haptic lifecycle step additionally restores DEVICE
auto-trigger after the final Press/Release replacement or erase, cancels a
queued PLAY before its slot buffer is rewritten, and drains every PLAY/STOP
work item before the HID reference and buffers are released. These cases still
need the next hardware pass before they become a checkpoint.

The evdev writer lifetime fix remains active because keyboard LED writes share
the same KeyD-versus-disconnect ownership boundary even without an FF driver.
Activation keeps every writer private until all matching input handles have
opened. Host queues hold a complete 62-value MT batch plus two reserved records;
overflow drops the incoming value without directly receiving from a QueueSet
member, and lifecycle removal may wait in its task for KeyD to make room.

## Related Notes

- [`tinyusb-host-port.md`](tinyusb-host-port.md): active SHA-pinned TinyUSB
  substitutions, their runtime contract, and the required SDK upgrade process.
- [`pio-usb-memory.md`](pio-usb-memory.md): static RAM and Pico-PIO-USB pools.
- [`async-hid-requests.md`](async-hid-requests.md): nonblocking TinyUSB request model.
- [`async-hid-progress.md`](async-hid-progress.md): implemented async-driver coverage.
- [`deferred-hid-drivers.md`](deferred-hid-drivers.md): drivers intentionally left out.
- [`hid-emulator-coverage.md`](hid-emulator-coverage.md): coverage history; use
  CMake, not this older matrix, as the current driver allowlist.
- [`upstream-porting-rules.md`](upstream-porting-rules.md): rules for Linux-port diffs.
- [`upstream-porting-audit.md`](upstream-porting-audit.md): remaining port deviations.
