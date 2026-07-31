# HID Host Bring-up and Limits

This is the final short record of the HID host regression investigation. It
replaces the old experiment logs and describes the current design, RAM limits,
diagnostics, and remaining risks.

Keep this as the operational overview. The exact generated TinyUSB patch and
upgrade procedure are maintained only in
[`tinyusb-host-port.md`](tinyusb-host-port.md); current linked-image and runtime
allocation numbers are maintained only in
[`pio-usb-memory.md`](pio-usb-memory.md); dated hardware evidence belongs in
[`async-hid-progress.md`](async-hid-progress.md) and
[`hid-emulator-coverage.md`](hid-emulator-coverage.md).

## Current Status

The following paths have produced input events on hardware:

- a generic USB keyboard, including the keyboard interface of a composite
  ErgoType device;
- the Holtek keyboard emulator, including its report-descriptor fixup;
- the active haptic-touchpad emulator, including pointer events and the
  multitouch/haptic probe path;
- direct HID++ 1.0/2.0 request/reply, identity, battery, timeout, disconnect,
  and reconnect paths;
- the pinned-upstream DJ receiver with standalone and simultaneous M705 and
  ordinary keyboard children, independent unpair, receiver detach, and later
  direct regression;
- the selected UC-Logic Huion H640P and Kamvas 13 profiles plus XP-Pen Deco 01
  V2, including their string/interrupt-OUT probes, generated input nodes, and
  repeated removal;
- modern XP-Pen Deco L/LW and Deco Pro S/SW/MW profiles, including battery
  traffic, wireless reconnect re-probe, dial/mouse input, and the USB Magic
  Trackpad 2 sparse-battery regression;
- XP-Pen Deco 01 original and Parblo A610 Pro, including their distinct raw
  string/request paths, Pen/Pad input, Parblo Mouse/Dial input, and same-PID
  reconnect;
- the selected external Apple USB keyboard and Mighty Mouse paths, including
  Fn/media and button mapping, the 60-second battery request,
  queued-request disconnect, and reconnect;
- the PMW pointing-device path, independently of USB host input.

The current exact-class stage additionally selects M560 `046d:402d`,
T650 `046d:4101`, K400 `046d:4024`, and K750 `046d:4002` upstream classes.
Its byte-exact automatic emulator completed all four class paths and the final
direct regression on hardware on 2026-07-26.

The first linked UC-Logic stage selects only Huion `256c:006d/006e` and XP-Pen
Deco 01 V2 `28bd:0905` from the complete pinned-upstream table. Its automatic
matrix completed on hardware on 2026-07-26 with every expected input phase,
one expected ignored Deco interface, stable removal plateaus, `oom=0`, and a
92-word minimum lifecycle watermark.

The modern extension keeps that narrow table policy and additionally selects
XP-Pen Deco L/LW `28bd:0935` and Deco Pro S/SW/MW
`28bd:0909/0933/0934`. It enables the pinned generic HID battery path for
wireless UGEE-v2 reports and for the already linked Magic Mouse/Trackpad
battery path, reusing the reduced firmware power-supply snapshot boundary.
Its expanded automatic matrix completed on hardware on 2026-07-27 with all
markers, stable cleanup plateaus, `oom=0`, nonzero task watermarks, and no host
`ERR`.

The later exact-ID UC-Logic extension selects only Deco 01 original
`28bd:0042` and Parblo A610 Pro `28bd:1903` from the same complete pinned
table. It adds no compatibility primitive, glue object, mutable state,
battery object, or scheduled work/timer path. Its focused matrix passed on
2026-07-31 with both initial and same-PID reconnect generations, ten balanced
target input lifetimes, terminal `f15, f10`, no host `ERR` or
`HID_REPORT_SKIP`, `oom=0` in all 22 heap snapshots, and nonzero task
watermarks. The minimum lifecycle watermark was 85 words.

The common UC-Logic failed-probe path now frees the separately allocated
combined replacement descriptor before the existing parameter cleanup. It
retains the pinned failure label, the existing `params_initialized` predicate,
and the original return code. A focused hardware fault exercised three reached
`hid_hw_start()` failures after descriptor generation, repeated the same
60,752-byte cleanup plateau, and then completed an ordinary Parblo recovery
generation. The temporary fault hook was removed. `hid_parse()` reaches the
same cleanup label, but its failure branch remains source-audited only.

The exact Artist extension selects XP-Pen Artist 22R Pro `28bd:091b` and
Artist 24 Pro `28bd:092d`. It keeps the complete pinned parameter,
replacement-descriptor, raw-event, Pen, 20-button Pad, and two-Dial paths with
the existing 512-word lifecycle task. No new request type, work item, timer,
battery object, or mutable driver state is introduced.

Its focused host/emulator pair passed on 2026-08-01. A full 22R generation,
same-PID 22R reconnect smoke, and 24 Pro generation produced six balanced
Pen/Pad lifetimes, six expected `HID_IGNORED`, 21 `oom=0` snapshots, a
36-word minimum lifecycle watermark, and terminal `f15, f10` without host
`ERR`, `f12`, or `HID_REPORT_SKIP`. The fixture gates exact endpoint-`0x03`
OUT-before-string-100 ordering. Production logging does not expose the numeric
Artist 24 ABS_X result, so the exact fragmented-X value remains source-audited.
Future lifecycle changes must remeasure the observed 36-word margin.

The Wacom checkpoint additionally links complete pinned Wacom sources. The
hardware-tested base matches CTL-472 `056a:037a`, CTL-672 `056a:037b`,
PTK-450 `056a:0029`, CTH-470 `056a:00de`, PTH-650 `056a:0027`, Yoga 260 AES
`056a:5048`, and receiver `056a:0084`. The later focused Intuos stage adds
external wired `056a:0302/0303/030e/0314/0315/0317/0323/033b/033c/033d/033e`,
and the focused Cintiq stage adds wired `056a:0304`.
Together the selected profiles reach the upstream Pen,
Pad, Touch, ExpressKeys, Touch Ring, LED, arbitration, ordinary/AES battery,
idle-proximity timer, and receiver pair/unpair plus sibling-rebind paths
applicable to those profiles.

The captured `0317` PTH-851 family descriptor repeats one vendor usage across
FEATURE reports as large as 265 values. Linux materializes one usage mapping
per value; the exact-ID firmware quirk for `0314/0315/0317` keeps all report
values and bytes but retains only the explicitly declared mapping entry. This
is a port memory optimization, not an upstream bugfix. On the captured `0317`
descriptor, reused by the `0314/0315` emulator profiles, it removes 626
duplicate usage/priority pairs, about 20 KiB. It raised free heap after Pro Pen
probe from 10,688 to 30,696 B and allowed the paired Finger interface to
register without changing `HID_MAX_USAGES=675`.

The focused Intuos host/emulator pair passed on 2026-07-31. Eleven selected PID
profiles published and removed 29 Pen/Pad/Finger nodes, all 63 heap snapshots
reported `oom=0`, terminal removals returned to 60,736 B free, every task
watermark stayed nonzero, and the fixture ended with `f15, f10` without a host
`ERR`. The matrix exercises every selected identity with documented family
captures: mode and Pen/Pad smoke for all eleven, Pro LED initialization for
three, and Finger smoke for seven. It is not a byte-exact retail descriptor
matrix.

The focused Cintiq 13HD host/emulator pair passed on 2026-08-01. Three
`056a:0304` generations published and removed six Pen/Pad nodes. Its
device-side oracle verified cancellation before the one-second initialization
deadline, then exact Feature report 2 SET/GET with value 2 in each of two fresh
generations. Pen enter/move/exit, serial `0x12345678`, all nine Pad buttons,
and same-PID reconnect completed `f1, f2, f3, f10`. All 21 snapshots reported
`oom=0`, terminal Cintiq removals repeated `60752/48520/9`, every task
watermark remained nonzero, and there was no `f12`, host `ERR`, or
`HID_REPORT_SKIP`. The protocol-equivalent descriptor is not a retail capture;
Touch, Touch Ring, and `ABS_WHEEL` are outside this verdict.

The exact five-profile wired pair recorded in `hid-emulator-coverage.md`
completed
`f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10` on hardware with no `f12` or
host `ERR`. All 23 physical Wacom attachments produced 46 balanced input-device
add/removes. The run covered control requests, pen/pad/touch input, LED work,
disconnect with delayed or running work, active-touch teardown, recovery, and
reconnect stress. All 115 heap snapshots reported `oom=0`, removal returned to
the established plateau, and every task watermark remained nonzero.

PTH-650 battery reports and normal terminal power-queue cleanup occurred in
that sequence, but the production host emitted no value-level `POWER`
diagnostic. The exact detached snapshot fields therefore remain unverified,
as does the failure path where the UI task or its tick timer does not start.
The reduced firmware glue does not implement Linux's power-supply/LED sysfs,
uevent, notifier, or presentation subsystems.

The earlier exact no-PIO CTL-472 host and 32-reconnect emulator remain a
separate historical result: they passed connection, mode/input, pre-deadline
disconnect, held-callback disconnect, recovery, and stable teardown. The
retained source also restores pinned Linux's two-resource managed-input
teardown and `void devm_release_action()` contract, and publishes evdev
identity from `input_dev->id` instead of opaque driver data. The complete
32-reconnect artifact reran against that correction with all 36 Pen
generations published as `056a:037a`, stable teardown, `oom=0`, and no host
`ERR`. The separate Rapoo managed extra-input regression remains pending.

The later AES/receiver pair recorded in `hid-emulator-coverage.md` also passed
on hardware. Four AES and four receiver attachments produced 20 balanced
input-device lifetimes across AES control/input/battery work, receiver
pair/unpair/re-pair, cancellation of original sibling initialization before
dynamic resource release, held rebind/teardown controls, physical disconnect,
and recovery. All 47 heap snapshots reported `oom=0`, removal returned to the
established 60,496/60,752-byte plateaus after terminal physical disconnect,
and every task watermark remained nonzero. The selected receiver child is
active PTH-650 profile `056a:0027`. The available capture reports `033b`;
that child is selected by the later Intuos stage but was not part of this older
artifact, so the test does not claim a captured `0084 -> 0027` pairing.

Production logs still do not expose exact detached battery fields or ordering,
and the AES fixture does not wait for the real 30-minute expiry. The receiver
fixture deterministically covers initial sibling work while pending but cannot
externally hold the short pre-PID callback after workqueue promotion or during
execution. Receiver lookup can select any child PID in the current exact-ID
table, but hardware receiver coverage is limited to `056a:0027`. Bluetooth,
ExpressKey Remote, bootloader, I2C, PCI, and product IDs outside that table
remain excluded.

Earlier bring-up firmware, before the current heap/static-RAM reductions,
reported roughly 43-48 KiB of free FreeRTOS heap before attaching a heavy HID
device. That range is historical, not the expected value for the current
artifact and not a guaranteed allocation size: record the current artifact's
own startup/attach/removal plateaus on hardware, and remember that HID parsing
also needs a sufficiently large contiguous block while a device is being added.

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

The selected UC-Logic paths use this same per-interface lifecycle. Huion reads
decoded firmware string 201 and raw parameter string 200 before parsing its
generated Pen/Pad/Touch Strip/Dial descriptors. Deco 01 V2 exposes three HID
interfaces; interface 2 performs an exact interrupt-OUT probe on endpoint
`0x03`, then reads raw parameter string 100 before publishing its Pen and Pad
nodes. Interface 0 publishes the upstream-generated frame Mouse node and
interface 1 is intentionally rejected by upstream, producing the host's
expected `WARN: HID_IGNORED` topology marker. These synchronous-looking calls
block only the lifecycle task, never TinyUSB's owner.

The selected modern UGEE-v2 models reuse the same three-interface topology.
Deco L and wireless Deco LW share PID `0935` and are distinguished by the
optional product string; a missing string retains the wired Deco L behavior.
The wireless models translate their unsolicited battery report to the
upstream `0xba` capacity/charging descriptor and schedule the existing
re-probe work on the exact reconnect event. This stage ends at Linux
input/evdev and the reduced power snapshot; KeyD tablet policy and UI
presentation remain separate work.

Deco 01 original uses the pinned v1 raw string-100 path without an
interrupt-OUT probe and publishes Pen plus eight-key Pad inputs. Parblo A610
Pro uses the UGEE-v2 three-interface path: its Pen interface completes the
existing endpoint-`0x03` interrupt-OUT probe before raw string 100 is read,
then the driver publishes Mouse, Pen, and nine-key Pad inputs and translates
both Dial directions. These synchronous-looking requests still block only the
lifecycle task. Pad keys cross the current KeyD boundary; pen tool/absolute
events and Dial stop at its existing unsupported-event boundary.

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

The TinyUSB host task is the ordinary `while (1) tuh_task();` loop and has no
fixed polling interval. Inside the generated host core, `tuh_task_ext()` first
services due enum continuations and shortens its private queue wait to the
nearest remaining deadline. HCD/deferred work wakes that same wait earlier; an
idle host with no deadline waits indefinitely. No timer object or wake queue is
added outside TinyUSB. This now includes the error-only 100-ms enumeration
control retry: completion snapshots its callback-local request tuple and
returns, then shallow host-owner service reconstructs it after the deadline and
global EP0 idle. It does not block the event pump or use Pico/FreeRTOS timers.

Host HCD publication uses one ordinary 32-event TinyUSB/FreeRTOS queue. The
generated dynamic-OSAL definition omits TinyUSB's otherwise-unused static
backing array, so there is no second spill FIFO, ordering state, overflow flag,
or firmware fail-stop callback. Queue publication and full-queue behavior stay
on TinyUSB's normal path.

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
run the Linux continuation. A CTRL/interrupt-OUT head rejected before physical
submission remains `PARKED`, like upstream after its RUNNING bit is cleared;
the next same-lane enqueue restarts it in FIFO order without polling. Ordinary
`hid_hw_wait()` excludes that stopped lane, clears any stale GET parser owner,
and can therefore finish while teardown remains responsible for freeing the
logical nodes. Every lease release wakes this potentially nonzero idle
predicate. If the bounded broker is full, synchronous callers, queued report
heads, and CLEAR_HALT link admission nodes into one FIFO. The oldest eligible
endpoint-front enters when a real normal slot is free; no request pre-reserves a
placeholder slot. Normal-slot release, waiter unlink, or matching teardown
notifies the affected task to retry the durable enqueue predicate. Synchronous
callers retain a one-second local admission bound, and their transfer timeout
starts only after admission. Recovery's dedicated HUB_RESET slot remains
outside ordinary admission. A saturated CLEAR_HALT stays in its durable
`WAIT_SLOT` state and its linked node owns the next generic admission wake;
there is no second capacity latch or private edge. The retry timer is reserved
for actual interrupt-I/O protocol failures. The report task sleeps until
notification or the absolute eight-second local deadline and then rechecks the
same predicate, without periodic polling.

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
For non-`ALWAYS_POLL` input, open first waits until the TinyUSB host owner has
made Linux's equivalent first interrupt-IN `usb_submit_urb()` attempt. The
report task then drops initial completions during Linux's 50-ms
`HID_RESUME_RUNNING` drain before enabling parser delivery. It also applies
stopping/recovery policy under the same exact-generation fence. Linux relies on
the USB reset-default Report
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
The one global enumeration owner remains visible even when it began before the
reset gate; root and hub admission wait for its terminal and do not spend their
phase timeout behind it. Exact-topology state still decides the reset result.
Each HID report interface owns one queued reset claim, so close removes only its
own claim; a sibling claim or lifecycle-owned running reset survives just like
upstream running `reset_work`.

The SHA-pinned TinyUSB HID class now scans the bounded current-interface extras
instead of assuming strict interface -> HID -> endpoint order. Like Linux's USB
HID transport, it accepts the HID descriptor after endpoint[0], ignores
non-interrupt endpoints, selects only the first interrupt IN and first
interrupt OUT, and publishes its class slot only after endpoint success.
Enumeration now skips SET_IDLE, SET_PROTOCOL, and
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
across the string chain. Full USB configuration descriptors independently use
TinyUSB's permanent 512-byte scratch when they fit. A validated
`wTotalLength` from 513 through 4096 bytes is allocated exactly once by shallow
host-owner service after the short-GET callback unwinds, retained through full-
GET retries, and freed immediately after the internal synchronous class-open
scan or after terminal EP0 drain. RP2040 implements the mandatory platform hook
with FreeRTOS heap_4; the boundary lets a future ESP port choose DMA-capable
internal memory.

The 600-byte hardware fixture verifies a working HID interface at byte 575,
repeated attach/remove with a stable heap plateau, and recovery from an
injected full-configuration control failure through the 100-ms continuation.
Remaining fault coverage is REMOVE or a foreign ATTACH while that deadline is
armed; a normal small device cannot exercise those cancellation/topology races.

EP0 cancellation cannot wait forever for a PIO completion event. Enumeration
and ordinary HID share one exact host-owned transaction for TinyUSB's global
control owner. PIO's same-CORE1 alarm/abort ownership guarantees that a
completion which won the physical race is already in the queue when abort
returns. The host snapshots that finite prefix, consumes a matching old stage
before it can chain DATA/ACK, and synthesizes TIMEOUT only at exact prefix
exhaustion. There is no SOF, FreeRTOS-tick, or stage-count polling, and the
captured daddr + callback + serial tuple never touches a replacement owner.
If REMOVE invalidates that device generation while the prefix is draining, the
old tuple remains alive only as a suppressed physical fence; unmount wakes the
logical waiter and no stale completion callback is delivered.

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
a partial report graph. `ERR: HID_FIELD_NOMEM` identifies the exact failed
field allocation; absence of that marker is not by itself attachment success.
Upstream's non-OOM `HID_MAX_FIELDS` truncation remains.

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

## Current RP2040 DJ/HID++ Memory Boundary

The retained firmware policy is `HID_MAX_FIELDS=64` and
`HID_MAX_USAGES=675`, versus 256 and 12,288 in upstream Linux. These limits
bound individual parser structures; they do not guarantee that every
combination of otherwise valid fields, reports, interfaces, and virtual
children fits the shared RP2040 heap.

Hardware testing at the retained `64/675` policy showed that an M705 mouse
child (`046d:101b`) and an ordinary DJ keyboard child (`046d:4024`) work both
separately and simultaneously. With both live, the heap reaches `free=5160`,
`min=4008`, and `largest=4184` without an OOM. The complete HID++ eQuad
keyboard connection profile (`046d:4024`) does not fit even as the receiver's
only paired child and adds one OOM count. That profile combines keyboard,
Consumer, power, media-center, and HID++ descriptors.

Those product values describe the recorded historical fixture. The current
emulator moves its generic keyboard and complete-eQuad capacity phases
to unmatched `046d:4003`, reserving `046d:4024` for the exact K400 class.
M560, T650, K400, and K750 then run sequentially in separate receiver
generations. The hardware result therefore measures one exact large child at a
time rather than making a new simultaneous-live-graph promise.

The earlier two-OOM capture used the upstream-sized 256-field table rather than
the retained 64-field table. Under that configuration, both the second
simultaneous child and the complete eQuad child reached the memory boundary.

The complete eQuad keyboard profile also does not fit at `8/675`, but does fit
at the temporary `HID_MAX_FIELDS=8`, `HID_MAX_USAGES=256` checkpoint. Its
Consumer descriptor declares usages 1 through 767, so the temporary run covers
child creation, keyboard input, and lifecycle cleanup while retaining only
Consumer selectors 1 through 256. The detailed measurements are recorded in
[`pio-usb-memory.md`](pio-usb-memory.md).

The focused Logitech/Lenovo fixture later completed `c532/c52f/c534` and
`6009/6047/60ee` at temporary `64/256`. For `c52f/c534`, the physical
652-usage Consumer field and the virtual 675-entry field request 42,696 bytes
at the retained cap, 26,080 bytes more than their pair at 256. The corrected
full `60ee` descriptor left 11,152 bytes free at 256 while its Consumer field
alone grows by 13,408 bytes at 675. The upstream rows remain in source, but
`c52f/c534/60ee` are therefore not selected by the RP2040 special-driver
tables. Their temporary run proves request/input/lifetime logic only.
The gated IDs are not globally ignored: generic HID may publish smaller
physical interfaces, but it does not send receiver startup reports or create
virtual children. A `60ee` generic attempt may therefore publish its smaller
interfaces before the wide mouse/Consumer interface reaches the heap boundary.

Similar capacity limits may affect devices with wide usage ranges, many report
fields, several simultaneous receiver children, or several large composite
HID interfaces. This is not a blanket limitation on keyboards or mice; it is a
property of the complete live descriptor graph. Treat each new large graph as
a measured compatibility case.

Two informational messages make the deliberate policy visible:

- `INFO: HID_FIELDS64_UPSTREAM256` is emitted after every successful
  `hid_open_report()`; it announces the configured field-table difference and
  does not mean that a descriptor reached 64 fields.
- `INFO: HID_USAGES675_UP_12288` is emitted when the DJ HID++ keyboard
  connection branch is selected. It announces the configured policy and is not
  itself an allocation result.

Successful attachment still requires the corresponding `DEVICE: added` and
input marker with no new OOM count. `WARN: HID_USAGE_CAP_DROP` remains a
separate runtime indication that an actual array selector was outside the
retained lookup.

## Retained Memory and Lifecycle Changes

The working configuration intentionally keeps these changes:

- explicit usage ranges reserve parser-local storage once at the final bounded
  size instead of repeatedly growing and copying it;
- `HID_MAX_FIELDS=64`, with `HID_FIELDS64_UPSTREAM256` after every successful
  report open and `HID_FIELD_NOMEM` on an exact persistent-field allocation
  failure;
- `HID_MAX_USAGES=675`, with the DJ-connection policy marker
  `HID_USAGES675_UP_12288` and `HID_USAGE_CAP_DROP` for an actual out-of-range
  selector;
- the CMake HID source list is the driver allowlist, and `CONFIG_HID_*` plus
  special-driver quirk gating mirror the linked drivers and supported features;
- builtin driver descriptors stay const in flash, while their mutable runtime
  records use linker-owned static `.bss` storage instead of startup heap
  allocations;
- the unreachable per-driver `new_id` sysfs attribute is not registered;
- HIDDEV remains unlinked because the firmware has no hiddev consumer;
- synchronous bind failure removes the unused HID object;
- interrupt receive begins only after successful bind/open;
- `cancel_work_sync()` physically unlinks pending work from the FIFO and closes
  the dequeue-to-running race before a driver may free its object.

Builtin driver runtime state now uses linker-owned `.bss` instead of startup
heap allocations. Static `.bss` still consumes physical RAM, but it no longer
depletes or fragments the runtime heap. Excluding a driver from CMake also
excludes its runtime record while leaving its source in the repository. Exact
current sizes are recorded in `pio-usb-memory.md`.

The current allowlist is `hid-generic` plus A4Tech, Chicony, Creative SB0540,
Cypress, ELECOM, EVision, Holtek keyboard and mouse fixups, ITE, Kensington,
Kye, Lenovo `6009/6047`, Microsoft, Apple external USB, Primax, PXRC, Rapoo,
Razer, Saitek, Topre, and Zydacron, plus generic multitouch, HID Haptics, and
the USB-only Magic Mouse 2 / Trackpad 2 driver.
The linked complete Logitech HID++/DJ, UC-Logic, and Wacom sources retain
separate narrow USB ID gates. The Wacom gate contains the seven
hardware-tested base IDs plus eleven external wired Intuos exact-ID selections
hardware-tested with the documented family captures above, plus focused wired
Cintiq 13HD `056a:0304`.
Microsoft is likewise narrow: 14 wired non-gaming USB IDs are selected, while
SideWinder, Bluetooth, Xbox/8BitDo, Surface Dial, and FF remain compile-gated
with matching special-driver gates. The exact per-device-release pair passed
on hardware on 2026-07-31: the two Office interfaces released F14 and F15
independently before removal, all representative profiles completed, removal
heap returned to 60752 bytes, and the terminal result was `f10` with `oom=0`
and no host `ERR`.
Apple is limited to 18 external wired keyboard/Mighty Mouse IDs. Internal and
legacy Apple keyboard/trackpad devices, Bluetooth, Touch Bar, and backlight-only
endpoints remain compile-gated with matching special-driver gates. Touch-ID
models are treated only as keyboards. The focused four-profile artifact passed
on hardware on 2026-07-31: both mapping tables, Mighty Mouse buttons, immediate
and 60-second battery GET, queued-request disconnect, and reconnect completed
before terminal `f10`, with stable removal heap, `oom=0`, and no host `ERR`.
The fixture delivered both relative Z signs, but production CDC has no
REL_HWHEEL marker, so their inversion remains source-audited.
The pinned Logitech DJ runtime selects USB receivers `046d:c52b/c532`.
Upstream `c52f/c534` rows and their mouse-only/HID++ types remain intact behind
the broader gate after their retained-policy RP2040 memory result. Gaming,
Lightspeed/Powerplay, legacy 27 MHz, Bluetooth-proxy, and Dinovo rows remain
gated. Existing `c52b` coverage remains valid; the selected `c532` exact
retained-policy active-ID regression is pending. `c52f/c534` passed
request/input/teardown only at temporary `64/256`.
The complete pinned Lenovo driver selects external USB TrackPoint keyboards
`17ef:6009/6047`; the full `60ee` row is retained but memory-gated. Bluetooth,
I2C, ScrollPoint, dock, tablet, and audio LED-class paths remain gated; Legion
is a separate unlinked driver family. The active paths retain their upstream
report validation, TrackPoint/Fn mapping, middle-button wheel arbitration, and
feature/raw request ordering. Their exact retained-policy active-ID regression
is pending. The current KeyD boundary still drops `KEY_FN_ESC`; `60ee` has only
a temporary `64/256` logic result and is not selected on RP2040.
Stadia rumble through `ff-memless` and Holtek's separate On Line Grip
game-controller driver remain unlinked; their IDs are not advertised as
requiring an absent special driver.

The current heap, task-stack, TinyUSB, and Pico-PIO-USB settings are maintained
in [`pio-usb-memory.md`](pio-usb-memory.md). Do not copy a dirty build's exact
link or allocation figures into this overview.

The workqueue, timer, and HID lifecycle split still costs three tasks.
Consolidating them is a later architecture change, not part of this bring-up
fix; their current stack sizes and measured watermarks belong in the memory
note.
The workqueue task now owns a notification wake rather than a one-entry queue;
its producers and synchronous cancellation paths share a dedicated
priority-inheritance mutex and wait on durable conditions without masking
interrupts or polling. The same mutex protects delayed-work deadlines and
ordinary FIFO promotion. Wacom teardown can therefore remove init work before
its deadline, after promotion, or while its callback runs; simultaneous
synchronous cancelers retain the requeue gate until all have returned.
Deadline comparison is wrap-safe for the linked one-second initialization and
30-minute AES expiry delays. Receiver rebind synchronously cancels both
sibling initialization callbacks before releasing their resource graphs.
Firmware queue destruction promotes owned delayed entries before drain rather
than abandoning their lifetime. The timer task is a parallel task-only
synchronization domain with its own mutex and notification wake. TinyUSB
unmount publishes only stopping; the report task claims retry cancellation
under an `io_pending` lease and waits for the running timer callback outside
every transport lock.
Current workqueue entry owners are the KeyD, HID timer/lifecycle, and
workqueue tasks. TinyUSB callbacks only publish disconnect/report state to
those owners. Workqueue invariant checks execute their FreeRTOS operation
before testing a captured result; `configASSERT()` never contains the
operation itself. Fixed-size `async_msg()` diagnostics are used because the
normal logger's roughly 2 KiB local frame does not fit the 384-word workqueue
and timer stacks.

The focused Wacom hardware matrices specifically covered cancellation before the
one-second deadline, disconnect while mode, LED, rebind, or teardown control
was held, pending AES work, timer cancellation, active-touch removal, and
receiver re-pair/recovery. They did not deterministically force the generic
after-promotion window, simultaneous synchronous cancelers, callback
self-requeue, workqueue destruction with a delayed entry, tick-counter wrap,
or the real 30-minute AES expiry; those remain code-audit or explicit coverage
limits.
The transport-wide invariant audit also leaves no function call or predicate
inside an active `configASSERT()`. A collision in the one-slot async diagnostic
path increments the UI warning counter before dropping the newer text.

Incoming HID reports and KeyD output injection now share Linux input state
through a per-`input_dev` priority-inheritance mutex. It replaces the active
task-context sections of upstream `event_lock`; TinyUSB callbacks never acquire
it. Disconnect releases the mutex before handler close/unregister, and no
protected section waits for USB, workqueue, or timer completion. Its current
per-device allocation cost is tracked in the memory note.

The callback-safe lifecycle transport pool includes fixed probe-identity slots
and one device/string descriptor scratch; callbacks own neither payload.
Lifecycle serializes probe, so
`usbhid_parse()` allocates at most one exact-size report descriptor transiently
(up to 4 KiB), owns it through asynchronous completion/cancel, and frees it
before returning. Include the persistent pool and transient descriptor in
post-enumeration and haptic heap checks; use the memory note for their current
sizes.

Enumeration may now own a different exact-size transient for a 513..4096-byte
full configuration descriptor. A 4096-byte request occupies about 4104 B in
RP2040 heap_4. TinyUSB releases it immediately after its internal synchronous
class-open scan, before class set-config and lifecycle enter Linux
`usbhid_parse()`, so it does not normally overlap the report-descriptor/haptic
probe peak. Failure, timeout, and remove release it only after the matching EP0
owner is drained; a retry reuses the same allocation.

Direct IN uses one persistent per-interface receive allocation; completion
metadata stays in fixed ownership slots and no allocation occurs per report.
Logical CTRL/OUT entries and SET snapshots are allocated only while queued and
are freed after completion or fenced cancellation. Exact structure sizes,
scratch-X placement, mutex/queue blocks, the shared request-memory budget, and
the distinction between fragmentation and a leak are maintained in
[`pio-usb-memory.md`](pio-usb-memory.md).

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
| `ERR: HID_EVDEV_FAIL` | The evdev input handler itself could not register. TinyUSB and the HID core continue for other input consumers. |
| `ERR: HID_INITCALL_FAIL` | At least one linked Linux module initcall failed; every remaining initcall still ran and host startup continued. |
| `ERR: HID_DRIVER_FAIL` | At least one linked HID driver failed to register; unrelated drivers and host startup continued. |
| `ERR: EVDEV_CONNECT_NOMEM` | The evdev handler could not allocate its private handle; as in Linux, the HID and other input consumers remain registered. |
| `ERR: EVDEV_CLIENT_NOMEM` | The automatic firmware evdev client or its queue could not be allocated; that client is skipped without unbinding the HID. |
| `ERR: EVDEV_PREPARE_FAIL` | Automatic evdev client preparation failed for a reason other than allocation; that client is skipped. |
| `ERR: EVDEV_OPEN_FAIL` | Opening one automatic evdev client failed; its private state is released and the HID remains bound. |
| `ERR: EVDEV_PUBLISH_FAIL` | One evdev client could not join devmon; it is closed and released without unbinding the HID. |
| `ERR: HID_USB_DEV_ALLOC_FAIL` | Device publication reached the bounded cache without the required admission slot. Ordinary cache pressure is now parked event-first, so this indicates an admission/lifetime invariant failure rather than expected OOM. |
| `ERR: HID_PROBE_DEFER_FAIL` | Mount/pre-probe identity could not be retained, the bounded probe slots were occupied, or device-descriptor pre-probe exhausted its attempts. |
| `ERR: HID_PROBE_NOMEM` | A parser/probe allocation failed, including the transient exact-size report-descriptor buffer. |
| `ERR: HID_USB_PARENT_MISSING` | A child was observed after its hub cache epoch disappeared; it was rejected instead of attached to the root hub. |
| `ERR: HID_REPORT_SKIP` | A successful endpoint giveback exceeded or otherwise violated its armed buffer bounds; payload was rejected before parsing. |
| `ERR: HID_RX_REARM_FAIL` | Receive could not be armed again after a report callback. |
| `ERR: HID_RX_STALL` | Interrupt IN stalled. Payload was discarded and asynchronous endpoint clear-halt recovery started. |
| `DBG: HID_CLEAR_HALT_OK` | Remote endpoint halt was cleared; the host owner may now reset the local PIO toggle to DATA0 and rearm. |
| `ERR: HID_CLEAR_HALT_FAIL` | Remote clear-halt transfer failed; terminal recovery queues coordinated device teardown/re-enumeration. |
| `ERR: HID_RX_XFER_FAIL` | Interrupt IN failed or timed out. Payload was discarded and upstream-style delayed retry started. One occurrence immediately adjacent to physical REMOVE can be abort-before-remove ordering; repetition while attached or missing resumed input is a failure. |
| `ERR: HID_RX_INVARIANT` | Interrupt-IN completion did not match its live owner/epoch/buffer tuple. The callback parked it before parser publication. |
| `ERR: HID_ASYNC_XFER_TUPLE` | A serial-matched broker completion violated its address/epoch/endpoint/payload tuple; the existing task-side `-EIO` retirement path ran. Zero-data EP0 completion is normalized from TinyUSB/PIO's eight-byte SETUP accounting to Linux's zero payload bytes before this check. |
| `ERR: HID_ASYNC_HUB_PIN` | A pinned hub-reset slot changed identity during its immediate host-owner completion handoff. |
| `ERR: HID_WAIT_UNLINK` | The intrusive task waiter could not be removed exactly once under the transport mutex. Linking is unconditional under that mutex and has no separate diagnostic. |
| `ERR: HID_WAIT_TIMEOUT` | Ordinary upstream-shaped `hid_hw_wait()` did not reach its durable idle predicate within 10 seconds. Teardown uses a separate unbounded lifetime fence. |
| `ERR: HID_TEARDOWN_WAIT` | The combined IN/CTRL/OUT teardown fence returned an error after producers were stopped; the HID lifetime must not be considered cleanly retired. |
| `DBG: HID_RESET_Q` | Terminal report recovery published an exact physical-device reset to lifecycle. |
| `DBG: HID_RESET_OK` | Old transport state retired and a fresh same-topology TinyUSB mount completed. |
| `ERR: HID_RESET_FAIL` | Coordinated teardown/reset/re-enumeration exhausted its bounded phase or hub retry deadline. |
| `ERR: HID_ASYNC_CANCEL_FAIL` | Pending async HID requests could not be cancelled during detach. |
| `ERR: HID_CTRL_DISPATCH_FAIL` | A completed control GET could not be published to its ordinary report queue or direct probe owner. |
| `ERR: HID_XFER_TO` | A request which reached TinyUSB exceeded its active wire timeout. Synchronous generic USB messages retain their caller-supplied timeout from accepted queueing; asynchronous `hid_hw_request()` reports wait on the exact EP0/endpoint-idle event and have no firmware-only pre-wire timeout, matching the upstream URB queue. |
| `ERR: HID_DEV_DESC_XFER` / `HID_DEV_DESC_SHORT` / `HID_DEV_DESC_TYPE` | One full device-descriptor refetch attempt failed, returned fewer than 18 bytes, or returned the wrong descriptor type. The first three retain the pending HID and retry after 100 ms; the fourth terminates that preprobe. |
| `ERR: HID_EP0_CALLBACK_LOST` | Exact host cancellation reported that it invoked the terminal callback, but the retained async slot did not observe it. |
| `ERR: HID_EP0_OWNER_MISMATCH` | The cancel helper found a different live daddr + callback + serial owner and deliberately left it untouched. |
| `ERR: HID_ATTACH_OVERFLOW` | More distinct topologies arrived during one enumeration than the bounded device table can retain. The excess ATTACH was dropped instead of blocking TinyUSB on its own queue. |
| `ERR: HID_ENUM_CONFIG_NOMEM` | A validated 513..4096-byte full configuration descriptor could not obtain its exact transient host buffer; the enumeration epoch failed cleanly. |
| `ERR: HID_ENUM_CONFIG_TOO_LARGE` | The short configuration header declared `wTotalLength` above the firmware's 4096-byte bound; no full GET was submitted. |
| `ERR: HID_ENUM_CONFIG_INVALID` | The short header or completed full configuration descriptor was short, malformed, or inconsistent with its retained `wTotalLength`; no class parser received it. |
| `ERR: HID_WQ_INIT_TWICE` | Workqueue initialization was invoked after its mutex had already been published. |
| `ERR: HID_LOCK_INIT_TWICE` | Shared transport synchronization was initialized twice. Ordinary lock misuse is debug-asserted after evaluating the FreeRTOS call; it has no separate async diagnostic/fail-stop path. |
| `ERR: HID_FIELD_NOMEM` | Allocation of one exact persistent `hid_field` graph failed; parser and driver-core cleanup reject the interface rather than bind a partial graph. |
| `INFO: HID_FIELDS64_UPSTREAM256` | A report descriptor opened successfully with the firmware's 64-field table rather than upstream Linux's 256-field table. This is a policy marker, not proof that the limit was reached. |
| `INFO: HID_USAGES675_UP_12288` | The DJ HID++ keyboard-connection path selected the firmware's 675-usage policy rather than upstream Linux's 12,288. This policy marker is not itself an OOM result. |
| `WARN: HID_USAGE_CAP_DROP` | A report used an array selector outside the retained 675-entry field lookup. |
| `DBG: HID_REPORT_OUT_Q` / `DBG: HID_REPORT_OUT_OK` | `.request()` routed an OUTPUT report through interrupt OUT and it completed. |
| `DBG: HID_REPORT_SET_Q` / `DBG: HID_REPORT_SET_OK` | `.request()` routed SET_REPORT through EP0 (FEATURE or no interrupt OUT) and it completed. |
| `WARN: HID_CTRL_QUEUE_FULL` / `WARN: HID_OUT_QUEUE_FULL` | One per-HID logical `.request()` FIFO reached the corresponding upstream-effective limit (255 CTRL or 63 OUT entries); the void request was dropped as Linux does on a full ring. |
| `WARN: HID_REPORT_QUEUE_MEMORY` | Compact logical FIFO nodes reached the shared 4096-byte firmware budget before the upstream count limit. Increase it only after measuring heap with the target work device. |
| `DBG: EVDEV_KEY_Q` | A key event reached the evdev-to-KeyD queue. |
| `WARN: EVDEV_BATCH_CAP` | An unusual input device computed more than the bounded 62-value host batch; input remains best-effort. |

The retired aggregate message `HID_ASYNC_INVARIANT` and the former
`HID_ASYNC_HOST_OWNER` check are not present in the current firmware. Classify
completion failures by `HID_ASYNC_XFER_TUPLE` and `HID_ASYNC_HUB_PIN` above.

The old `USB_MOUNT_CB` and `HID_MOUNT_CB` callback markers are intentionally
gone: callback context no longer calls the logger. A connection that never
reaches TinyUSB still has to be diagnosed from USB enumeration/TinyUSB tracing,
power, wiring, D+/D-, the breakout, and the connector—not parser changes.

## Enabling Another HID Driver

Drivers remain in the repository but are linked selectively. To enable one:

1. Add its source to the HID allowlist in `CMakeLists.txt`.
2. Enable the matching `CONFIG_HID_*` symbol in `hid_compat.h`.
3. Do not enable optional FF, HIDDEV, HIDRAW, LED, battery, PID, or haptic
   feature symbols unless every reached operation has a firmware contract.
   Reduced LED and detached power-snapshot glue exists for the exact linked
   callers above; it is not the full Linux class/sysfs/uevent subsystem.
4. Build and check both link-time RAM and runtime free heap.
5. Test enumeration, actual input/output, repeated unplug/replug, and the
   expected fallback to `hid-generic` for unrelated devices.
6. Record emulator or hardware coverage in `hid-emulator-coverage.md`.

A linked source with a missing config symbol can compile without registering
the intended driver. A config symbol without the matching source does not add
the driver. Keep both sides synchronized.

## Force Feedback and Remaining Risks

No gaming FF driver is active. The tree retains the audited
`hid-google-stadiaff.c` plus `ff-memless.c` implementation for Stadia
`FF_RUMBLE`, but both CMake entries are commented out until firmware has a
product client. The driver's upstream spinlock and ff-memless's two input
`event_lock` scopes are represented by task-context priority-inheritance
mutexes, with the original upstream lines retained beside the port.

The retained pair was tested before deferral with a temporary targeted client:
two complete cycles separated by reconnect passed upload, duration-timer stop,
same-ID replay, unplug during the exact running Stadia work item, and clean
remove. Exact fixture and image identities are recorded in
`hid-emulator-coverage.md`. To restore the experiment, enable both
`usb_host/linux/drivers/hid/hid-google-stadiaff.c` and
`usb_host/linux/drivers/input/ff-memless.c` in the root `CMakeLists.txt`;
`ff-core.c` must remain linked for the independently active HID Haptics path.

### Future firmware FF client hook

The Linux drivers expose force feedback through the task-context evdev
reverse-writer boundary, not through TinyUSB callbacks. The active standard
HID Haptics policy snapshots `has_haptic` after complete probe, preloads five
waveforms per KeyD device, and turns a virtual `DEVMON_HAPTIC` request into an
indexed PLAY. Its upload/play/erase helpers are deliberately scoped to that
policy rather than exported as a generic FF API.

The historical Stadia run temporarily added a targeted raw client which
performed the Linux userspace sequence: upload an `FF_RUMBLE` definition with
`id = -1`, write `EV_FF` with the returned ID to play or stop it, then erase
that ID. It tested upload, play, automatic timer stop, and same-ID replay; the
temporary raw client was removed when Stadia and `ff-memless` returned to the
deferred set.

The client owns the returned effect ID only for that `struct device` lifetime.
Discard both on `EV_DEV_REMOVE`; after reconnect, upload again with `id = -1`.
For `ff-memless`, a nonzero play value is the repeat count and
`replay.length` schedules the automatic stop. The normal output path is
asynchronous/fire-and-forget: a successful writer call is not USB completion.

The known Stadia test selected `18d1:9400` directly. A future generic client
must extend the current `has_haptic` snapshot to the required Linux `ffbit`
capabilities instead of identifying devices by VID/PID or probing with failed
uploads. `FF_RUMBLE` and the standard HID Haptics Page's `FF_HAPTIC` are
different effect types; use only a capability advertised by that input device.
The firmware definition of `struct ff_effect` comes from
`usb_host/linux/include/uapi/linux/input.h`; keep that dependency in the
evdev/KeyD FF glue rather than spreading it through unrelated code.

The generic `ff-core`, `hid-haptic`, `hid-multitouch`, evdev
upload/play/stop/erase boundary, workqueue bridge, and async output transport
are active for the standard haptic-touchpad path. The two-Pico emulator has
verified enumeration, pointer input, haptic cursor feedback, strict five-slot
output ordering, numbered and report-ID-zero output routes, queued-work unplug,
repeated teardown, and reconnect. A real physical touchpad and deterministic
control-failure injection still need hardware coverage.

The current haptic lifecycle restores DEVICE auto-trigger after the final
Press/Release replacement or erase, cancels a queued PLAY before its slot
buffer is rewritten, and drains every PLAY/STOP work item before the HID
reference and buffers are released. The KeyD policy deliberately keeps its
preloaded Press/Release definitions for the attachment, so normal operation
remains in HOST mode until removal. The replacement, erase, output-routing,
queued-work unplug, teardown, and reconnect paths passed the 2026-07-23
hardware fixture.

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
