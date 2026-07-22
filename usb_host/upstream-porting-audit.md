# Upstream Porting Audit

Updated: 2026-07-22

Rules: `usb_host/upstream-porting-rules.md`.

This document records current audit findings, accepted port boundaries, and
rules for future imports. Hardware chronology belongs in
[`async-hid-progress.md`](async-hid-progress.md), current memory accounting in
[`pio-usb-memory.md`](pio-usb-memory.md), and the SHA-pinned TinyUSB maintenance
contract in [`tinyusb-host-port.md`](tinyusb-host-port.md).

Linux baseline: `../linux-upstream-hid` at
`83f1454877cc292b88baf13c829c16ce6937d120`.
Post-baseline upstream fix: multitouch active-slot bitmap commit
`8813b0612275cc61fe9e6603d0ee019247ade6be`.

## Result

Active-path conformance for the linked keyboard, mouse, multitouch, haptic,
input-core, and FF paths through the Linux input-event publication boundary. A
repeated semantic and lock-order comparison found no remaining P0/P1
divergence from the pinned Linux behavior in that scope.
The port is not byte-identical: Linux-only presentation subsystems and the
TinyUSB/FreeRTOS ownership boundary remain explicit structural exceptions.
This is a porting audit, not a runtime safety certification.

The downstream Linux-to-KeyD consumer is a separate boundary and is not covered
by that result. The working tree now carries upstream KeyD commit
`0cbe717b63c73de7872013b0834d90d802047546`: unsupported `EV_ABS` returns
`NULL` instead of returning a stale static KeyD event. It also carries the
explicit silent Type-B `ABS_MT_*` returns from main-repository `pio-hid-host`
commit `3da259a`; Linux pointer emulation has already consumed those codes.
This deliberately does not add `SYN_DROPPED` resync or rewrite Linux
`hid-multitouch`/`input-mt`; no resynchronization claim is made here.

Checkpoint `hid: stabilize stadia ff teardown` preserves the Stadia/`ff-memless` implementation and teardown
fixes. The working tree keeps that gaming path unlinked and instead links
upstream `hid-multitouch` plus `hid-haptic`. Both firmware images build; a
two-Pico run confirms haptic OUTPUT through emulator cursor feedback.

The current dirty tree descends from hardware-verified `usb: complete event-driven enumeration path`; its latest
pre-audit executable checkpoint also passed repeated automatic driver,
long-configuration, input, haptic, and removal cycles. Exact current image
identity and hardware status live only in
[`pio-usb-memory.md`](pio-usb-memory.md). This whole-file audit changes comments
and documentation only, but moving compiled-source line numbers can still
change a UF2 hash; a new hash must not inherit an earlier hardware verdict.
The tree removes mutex/readiness polling, restores the missing FF event-lock
sections, replaces compatibility-wide FreeRTOS critical regions with Linux-like
atomic operations, replaces TinyUSB submit retry polling with exact owner-idle
edges, and applies safe line-preserving upstream cleanup. Priority-inheritance
mutexes now block directly; workqueue/timer waiters sleep on notifications and
durable predicates instead of retrying every tick.
The repeated active-path audit found no remaining P0/P1 lifetime, lock-order,
or polling defect. Bounded and dormant exceptions are listed below instead of
being hidden by speculative rewrites.

The whole-tree diagnostic pass is also applied. Every active application/host
`configASSERT()` receives an already computed identifier or pointer;
allocation, lock, task, and notification calls execute outside the assertion.
The port does not invent a generic release-build fail-stop state machine for
ordinary mutex/assert misuse. Report-slot teardown retains the HID/buffer
lifetime unless every physical-transfer, deferred-host, detach, and waiter
predicate is released. A canceled async slot likewise stays in `RETIRING`
while TinyUSB still owns its exact cancel callback/fence; logical device
cancellation cannot release that fixed slot early.

Startup follows the hardware-tested direct task order from `HEAD`: TUD starts
as soon as its shared prerequisites exist, and TUH starts after its async,
workqueue, timer, lifecycle, and report runtime has been created. There is no
global READY/FAILED protocol or USB-owner startup barrier. Device ADD events
retain the ordinary devmon/KeyD event ordering.

## Deferred P2 Boundaries

These are explicit follow-up boundaries, not hidden claims of full-kernel
support:

- `Pico-PIO-USB/src/pio_usb_host.c` still has the backend-specific abort
  `busy_wait_ms(1)` loop. Under the current same-CORE1 SOF/host-owner contract
  it normally observes an already retired transfer; a second HCD needs a real
  cancel-completion edge before this assumption can be shared.
- `evdev_client.c` reports fixed-queue overflow but does not yet implement
  Linux `SYN_DROPPED` plus state resynchronization. That belongs to the later
  Linux-to-KeyD boundary; an unusually large multitouch frame can still lose a
  release when the client queue is exhausted.
- The current PIO HCD has no runtime bandwidth scheduler: its eight endpoint
  slots are reserved during class open, before a Linux HID object exists.
  Runtime submit therefore has no truthful `-ENOSPC`/`HID_NO_BANDWIDTH`
  outcome; owner contention, removal, STALL, timeout, and other I/O failure are
  still distinguished. A future scheduled HCD must extend the typed firmware
  submit adapter rather than infer `-ENOSPC` from TinyUSB's final `bool`.
- The single firmware workqueue worker is round-robin fair but still serializes
  unrelated work behind a long synchronous haptic output. Split workers only
  if measured hardware latency requires them.
- UI owner publication and diagnostic counters are atomic, but layout/output
  text payloads remain lockless cross-core snapshots. The current single
  producer per payload limits impact to a torn display line; a queue or owned
  double buffer is required before adding more producers.
- WebHID uses zero-time FAT mutex attempts and returns BUSY instead of polling,
  and bus reset/unplug now releases a read-held mutex plus abandoned LIST/WRITE
  buffers. Accepted FAT/flash work still runs in the TinyUSB device owner. A
  device worker is the future boundary if those callbacks become
  latency-sensitive.
- The one-tick delay after a pointing IRQ is sensor-burst coalescing, not lock
  acquisition. It remains a hardware policy rather than part of the HID host
  async model.
- The restored CDC logger still wakes through `usbd_defer_func()`, whose device
  queue exists only after `tusb_init()`. The normal hardware-tested startup fits
  in its ring, but verbose overflow and failure diagnostics before the
  higher-priority TUD task first runs remain unsupported. Resolve and
  hardware-test this interaction in the main repository before carrying an
  event-driven replacement here.
- A long device-side USB disconnect can fill the finite virtual keyboard
  output queue and drop later transitions. Onboard input producers instead use
  blocking queue backpressure without holding another lock; this cannot form a
  TinyUSB cycle, but it differs from Linux evdev overflow/resync policy.
- SSD1306 writes use blocking I2C without an error/timeout contract and can
  strand only the UI task. Configured KeyD macro delays intentionally pause the
  single KeyD loop. Neither wait belongs to the TinyUSB-to-Linux transport
  layer, but both remain explicit whole-tree latency boundaries.
- Compatibility `delayed_work`, runtime dynamic quirks, public input grab,
  second input clients, and generic Linux logging remain compile-gated or
  dormant. Enabling a caller requires re-auditing the reduced compatibility
  contract first.
- Terminal interrupt-I/O recovery currently performs full TinyUSB
  teardown/re-enumeration rather than Linux's in-place `usb_reset_device()`.
  A backend-neutral reset/cancel contract is required before a second HCD.
- The compact firmware `usb_host_interface` exposes a deliberately selected
  transport-only view: the first interrupt IN and first interrupt OUT from the
  complete interface stream. TinyUSB retains the physical altsetting, while no
  linked HID caller consumes bulk or duplicate interrupt endpoints through the
  shim. This removes the former first-four-descriptors truncation and reduces
  each snapshot to two endpoint records; the two endpoint-filter fixtures are
  still required before claiming hardware coverage.

## Scope

The audit covers all 68 imported HID `.c` files (26 linked and 42 unlinked), all
six files under `linux/drivers/input`, all 53 headers under `linux/include` (45
compatibility-facing `linux/**` files plus eight asm/dt/UAPI files), all 18
top-level host glue/header/CMake files, the root CMake wiring, and the SHA-pinned
generated TinyUSB/Pico-HCD transformations. This is the complete imported/host-
port tree, not only the Git diff or linked objects.
The later `keyd/port/device.c` event consumer was inspected for reachability but
is not included in the upstream-conformance result above.

## Audit Method and Per-File Result

The Linux source tree was first verified clean at the baseline commit above.
Each linked Linux-derived file was then compared with `git diff --no-index`
using the function order and exact statements, not only compiled behavior.
Every active hunk was classified as a missing-kernel-subsystem adapter, an
RP2040 memory policy, a FreeRTOS ownership primitive, or an explicitly named
generic bug fix. The original Linux statement/block is retained beside every
active replacement. The independent lifetime pass also followed each mutex,
waiter, task notification, callback publisher, and disconnect owner through
its final release. No unexplained active-path hunk or P0/P1 issue remains in
the audited scope.

The inactive files were also compared, rather than inferred safe merely because
CMake omits them. Most differ only by an adjacent, explained immutable driver
descriptor. Three are explicit enablement blockers:

- `usbhid/hiddev.c` is a firmware proxy rewrite, not a line-preserving port of
  Linux's fd/ioctl/fasync implementation. Move that proxy to glue and restore
  the Linux-derived file before enabling it.
- `hid-google-stadiaff.c` directly embeds FreeRTOS semaphore API in a
  Linux-derived driver. Keep it unlinked until the lock/lifetime adapter lives
  behind the compatibility or workqueue boundary.
- `ff-memless.c` is unlinked and its two upstream `event_lock` scopes are not
  implemented. They are now marked as mandatory work before relinking it.

For completeness, the disabled HID sources whose only function-level change is
that adjacent immutable-descriptor replacement are: `hid-accutouch`,
`hid-appleir`, `hid-aureal`, `hid-belkin`, `hid-cherry`, `hid-ezkey`,
`hid-gembird`, `hid-glorious`, `hid-gyration`, `hid-huawei`, `hid-icade`,
`hid-jabra`, `hid-keytouch`, `hid-lcpower`, `hid-macally`, `hid-maltron`,
`hid-monterey`, `hid-nti`, `hid-ortek`, `hid-penmount`, `hid-petalynx`,
`hid-plantronics`, `hid-redragon`, `hid-retrode`, `hid-samsung`, `hid-semitek`,
`hid-sigmamicro`, `hid-speedlink`, `hid-sunplus`, `hid-tivo`, `hid-topseed`,
`hid-twinhan`, `hid-viewsonic`, `hid-vivaldi`, `hid-vrc2`, `hid-waltop`,
`hid-xiaomi`, and `hid-xinmo`. `hid-vivaldi-common.c` is byte-for-byte baseline.
Together with `hid-cmedia.c`, Stadia, and hiddev, this accounts for all 42
unlinked HID `.c` files.

The dormant `hid-core.c` `new_id` block is unreachable because firmware
publishes no driver groups, but it remains a partial local parser/lockless
runtime-ID rewrite. It should be compile-gated or restored to upstream form
before any runtime new-ID interface is added. `hid-cmedia.c` is line-preserving,
but its CM6533/hiddev half remains intentionally disabled. These dormant debts
do not weaken the active-path result and are not certified for enablement.

The compatibility-header pass found no active P0/P1 contract break, but it did
make the reduced boundaries explicit beside their definitions. The sole active
`array3_size()` caller lacks Linux's generic overflow helper but is bounded by
the 4096-byte report-descriptor cap. `DEFINE_MUTEX()` does not construct its
FreeRTOS handle, `mdelay()` sleeps instead of busy-waiting, and the reduced
`kfifo` omits Linux locking/typed/power-of-two behavior; none has a linked
caller which depends on the missing contract. Generic typed/devm allocation
helpers pre-multiply sizes without Linux overflow semantics, while current
counts are bounded. Any new caller must re-audit or compile-gate these helpers.
The port also assumes little-endian targets, valid for RP2040/RP2350 and ESP,
without an explicit compile-time byte-order guard. Finally, `input.c` currently
gets generic compatibility primitives by including `hid.h`; that upward layer
dependency is marked as cleanup debt, not claimed as upstream structure.

The linked non-compatibility private headers `hid-ids.h`, `hid-haptic.h`, and
`input-core-private.h` are byte-for-byte identical to the pinned baseline.

| Linux-derived file | Remaining explained differences |
| --- | --- |
| `hid-input.c` | Local include path and immutable `hid_driver` pointer only. |
| `input-mt.c` | Local includes, task-context event-mutex boundaries, and upstream commit `8813b061` for the active-slot bitmap. |
| `ff-core.c` | Explicit unlocks replacing Linux `guard(mutex)`, `port_event_mutex` replacing IRQ event-lock scopes, and checked/destroyed heap-backed FreeRTOS mutex state. |
| `hid-haptic.c` | Five-slot firmware RAM policy plus the documented unassigned-usage, unnumbered-report-ID, HOST/DEVICE mode, erase, and queued-work lifetime fixes. |
| `hid-core.c` | Sparse full-range report-ID lookup, heap-backed parser locals, constrained INPUT-array value storage, explicit hidraw omission, and mutable runtime state beside flash-resident driver descriptors. |
| `input.c` | Task-context input event mutex, firmware devres/action ownership, direct object release, and Linux presentation/PM/userspace code retained exactly under `#if 0` at its omitted runtime boundary. |
| `hid-magicmouse.c` | USB-only Mouse 2/Trackpad 2 IDs, three unreachable delayed-work statements retained beside the firmware gate, and an immutable driver descriptor. Raw parsing and MT event flow remain upstream. |
| linked vendor drivers | Local includes, immutable driver descriptors, and the required generic post-`hid_hw_start()` probe unwind; the Rapoo replacement retains both complete upstream return branches. |
| `usbhid.c`, `evdev.c`, host task files | Deliberate TinyUSB/FreeRTOS glue, audited against the corresponding Linux lifecycle rather than claimed as copied source. |

The exact scalar timestamp replacements now retain Linux's `ktime_set()`,
`ktime_compare()`, and `ktime_get()` lines. The firmware devres path likewise
retains the complete upstream wrapper, allocation, register, error-label, and
two-phase unregister blocks beside its one-argument action replacement. The
out-of-line firmware definition of `input_sync()` also retains the exact
upstream header block beside the linkage-only replacement.

### Downstream generic patch series

The following fixes are intentionally not port-boundary adaptations. They are
named downstream Linux-generic patches absent from the pinned `83f14548`
baseline and its only local ref, and must be rechecked independently when that
baseline is updated:

- `hid-haptic`: skip an unhandled usage instead of storing an uninitialized or
  stale value; preserve the reserved byte-zero convention for an unnumbered
  report; use the retained effect during erase; restore DEVICE mode after the
  final Press/Release owner; cancel work before rewriting/freeing its buffers;
  roll back the optional haptic capabilities when FF setup fails; and detach the
  devres-owned haptic object from ff-core's generic `kfree(ff->private)` path.
- `hid-rapoo`: call `hid_hw_stop()` if allocation or input registration fails
  after a successful `hid_hw_start()`.
- `input.c`: pass the UAPI `*_CNT` bit counts to `bitmap_subset()` instead of
  the pinned baseline's last-valid-bit `*_MAX` values, so handler matching does
  not silently omit the final capability bit in each bitmap.

Every replaced baseline statement remains commented beside these fixes. The
local upstream checkout is shallow/grafted, so this audit claims only that the
fixes are absent from the pinned baseline/local refs; it does not invent later
upstream commit IDs.

## Wait and Lock Inventory

| Domain | Current wait primitive | Audit result |
| --- | --- | --- |
| workqueue state | PI mutex plus task notification and durable pending/running predicates | No retry sleep; the initial test and waiter link share one mutex scope before it is released. |
| Linux timer bridge | PI mutex plus nearest-deadline notification wait | No retry sleep; callback runs outside the mutex and sync deletion tests/links under one mutex scope before waiting on `running`. |
| HID transport/lifecycle state | PI mutex plus per-owner task notifications | No critical-section polling and no lock is held across TinyUSB progress. |
| async EP0/OUT submission | exact TinyUSB control/endpoint owner-release callbacks plus absolute watchdog | No periodic submit retry; notification is only an edge and slot state is durable. |
| TinyUSB HCD event publication | one ordinary 32-entry RTOS queue | One FIFO preserves event order. The port adds capacity but no spill state machine or firmware-specific overflow/fail-stop policy. |
| interrupt-IN close/teardown | per-interface predicate plus notification | Matches the `usb_kill_urb()` ownership fence without one-tick polling. |
| evdev/KeyD writes and FF | PI writer mutex; per-input PI event mutex | Final close is serialized with writers; no global scheduler/IRQ masking. |
| device-side virtual HID | queue plus completion/reset notification bits | Ready and submit execute in the TinyUSB device owner; no readiness polling. |
| CDC throttle | bounded two-tick retry loop, with a one-time 1024ms ceiling and a later 16-tick ceiling | Intentional device-side logger policy retained from the hardware-tested baseline; it is outside host HID lifecycle/lock acquisition. |
| `down_trylock()` in `hid-core.c` | one nonblocking semaphore attempt | Exact upstream parser gate, with no retry loop. |
| post-abort HCD retirement | one global EP0 cancel transaction plus exact cancel-time FIFO prefix; one-shot non-control fence | No tick/SOF/stage-count polling. Current proof relies on PIO alarm IRQ and HCD abort sharing CORE1; ESP/DWC2 needs an explicit backend cancel-completion edge. |

WebHID's zero-time FAT mutex attempts intentionally return busy from a USB
callback and do not retry. Queue receives with zero timeout are bounded drains,
not mutex acquisition loops. No lifecycle/workqueue/timer compatibility lock
masks interrupts. The only new generated-host critical section is the bounded
ISR/task event handoff: zero-time queue operation, counters, or one event copy;
it contains no callback, logging, allocation, or wait.

## Conforming Areas

- CMake/`CONFIG_HID_*` agree on 19 linked vendor driver descriptors across 18
  vendor config families, generic `hid-multitouch`, and `hid-haptic`. The
  compound Holtek config now links both keyboard and mouse descriptor-fixup
  drivers; the unlinked game-controller-only `hid-holtekff` ID is left generic
  instead of being marked as having an absent special driver. Active
  vendor/generic changes keep adjacent upstream lines and reasons; no unrelated
  vendor-flow rewrite was found.
- The imported `hid-magicmouse.c` matches only USB Magic Mouse 2 and Trackpad 2
  IDs. Its synchronous mode SET runs from lifecycle task context through the
  existing async EP0 owner. USB Mouse 2 returns before the upstream delayed
  retry site, while USB Trackpad 2 cannot satisfy that Mouse-2-only condition;
  the three delayed-work calls and every excluded Bluetooth/legacy ID remain
  visible beside the explained firmware boundary. Full 15-contact behavior is
  not claimed because the later evdev-to-KeyD queue remains a separate bounded
  consumer.
- `hid-haptic.h` is byte-for-byte baseline. `hid-multitouch.c` retains three
  adjacent, explained `jiffies` member-token substitutions and imports upstream
  `8813b061`: active contacts use a `maxcontacts`-sized bitmap and the RUNNING
  flag is again bit zero, avoiding out-of-bounds bit access on 32-bit RP2040.
- `hid-haptic.c` keeps upstream flow and adjacent upstream anchors. Its
  explained generic fixes skip unassigned report values, preserve the reserved
  byte-zero convention for unnumbered manual-trigger reports, restore DEVICE
  mode after the last Press/Release effect, and cancel work before HID/report-
  buffer release. The explicit five-slot replacement for Linux's 96 userspace
  effect slots is the firmware RAM-bound policy. Heap-backed FreeRTOS mutex
  creation is checked and unwound. The linked input has no second FF owner, so
  failed optional FF publication clears the just-published `EV_FF`/`FF_HAPTIC`
  bits before leaving `dev->ff == NULL`; dynamic mutex backing is destroyed.
- Resolution-multiplier setup is back to the upstream synchronous block; the
  former local continuation functions are gone.
- `ff-core` remains linked; `ff-memless`/Stadia do not. Evdev clears `EV_REP`
  because KeyD owns held-key state and discards Linux repeat value `2`.
- AppleIR remains unlinked. The timer bridge now serves multitouch's 100-ms
  sticky-contact release; haptic waveform duration is device-managed and has
  no FF duration timer.
- `hid-drivers.c` is marked port-only linker/initcall glue; its Linux-derived
  directory placement remains an explicit exception. Like Linux module/initcall
  startup, one failed entry is reported without suppressing unrelated entries
  or stopping the TinyUSB host owner.

## Porting-Rule Boundaries

1. The largest inactive Linux-source block is `input.c`'s omitted presentation
   layer. In baseline `83f14548`, this runs from
   `#ifdef CONFIG_PROC_FS` at line 1019 through `input_dev_uevent()` at line
   1703 and implements procfs, sysfs attributes, modalias/uevent construction,
   PM/class presentation, and device release. Firmware has none of those
   consumers. The exact baseline block is now retained byte-for-byte under
   `#if 0` beside the firmware omission, closing the former literal
   porting-rules exception. Active toggle/allocation/timestamp helpers resume in
   upstream order.

2. `input_reset_device()` is retained only as a dormant upstream API. Its two
   upstream `guard()` lines remain visible, but the body deliberately does not
   invent a lock order while there is no caller. The reduced-header declaration
   has a compile-time error attribute, so a future caller cannot silently use
   the unlocked body; enabling it requires the input-device lifecycle mutex
   plus the `port_event_mutex` equivalent of `event_lock`.

3. `hid-core.c` keeps immutable driver descriptors in flash and places mutable
   driver-core/dynamic-ID state in `hid_driver_runtime`. That remains a
   structural port boundary, but the changed signatures, dynamic-ID lock
   sites, match/reprobe paths, register branches, and unregister calls now keep
   their exact upstream lines adjacent to each replacement. It is no longer an
   untraceable major exception.

4. `usbhid.c`, queue-backed `evdev.c`, and port-only `task.c` are deliberate
   glue rather than line-preserving Linux files. `usbhid.c` retains the
   relevant upstream USB lifecycle statements beside its async replacements;
   `task.c` carries an explicit TinyUSB/Linux provenance note.

5. Dynamic quirk mutation remains dormant. Its upstream lock calls stay
   visible, but the compatibility mutex is not initialized; enabling a runtime
   writer or bus-owner unload requires restoring that lock as one unit.

6. `hid-drivers.c` is port-only linker/initcall glue stored in the Linux-derived
   directory. Reduced compatibility headers are contracts rather than copied
   upstream implementations. Both remain intentionally identified exceptions.

7. `hid-core.c` allocates parser-local usage arrays lazily. Upstream embeds and
   zeroes those arrays, so `open_collection()` reads a zero usage for a
   malformed-but-accepted COLLECTION with no preceding Usage. The port checks
   `usage_index` before dereferencing its nullable array and preserves that
   exact zero default instead of faulting during probe.

The current tree also closes three former traceability findings: ff-core's
manual replacement for Linux `guard(mutex)` now preserves every affected
upstream early return beside the explicit unlock; the parser usage-cap warning
crosses `hid_port_usage_cap_drop()` in transport glue instead of including the
CDC logger from `hid-core.c`; and the port-only locked report entry no longer
contains a hypothetical NULL check that no current caller can exercise.

## Fixed Runtime Boundary

- After successful driver probe, lifecycle drains the interface's finite
  `io_pending` leases before evdev activation. This preserves the ordering of
  probe-originated mode SETs when firmware's direct interrupt-IN scheduler is
  independent from EP0/interrupt-OUT; the wait excludes continuously armed IN.
- `evdev_connect()` now registers only an inactive input handle. After the
  complete synchronous `hid_add_device()` probe, `evdev_activate_hid()`
  prepares every client/queue privately, opens every matching handle, and only
  then publishes the surviving input snapshots and writers to devmon. This matches
  Linux's private evdev object versus successful later eventX-open separation:
  another core cannot call a writer before its input handle is open. A failed
  prepare, open, or publication is logged and unwinds only that unpublished
  evdev client; it does not fail HID probe or disturb successful siblings.
  Already published clients receive normal removal on physical disconnect.
  Synchronous input generated by an open hook is suppressed until its empty
  queue has joined the QueueSet. Completions during activation retain normal
  transport recovery/rearm but skip parser delivery until `driver_ready`,
  which is published under the transport lock only if detach/stop has not
  already been published. As in upstream Linux, an individual handler-connect
  error remains local to that handler: input-device registration continues and
  other current or future input consumers are unaffected.
- One mutex serializes synchronous KeyD write/upload/erase with unregister.
  Input disconnect releases pressed keys but leaves the sole evdev handle's
  open count intact. `evdev_disconnect()` takes that mutex, waits the current
  writer, nulls `client->evdev`, and releases it; every later writer therefore
  returns `-ENODEV`. It can then run final `input_close_device()`/`hid_hw_close()`
  outside the global mutex, send the existing removal sentinel, and leave KeyD
  to delete the queue/client. Thus an in-flight KeyD output cannot overlap
  transport close or FF/input destruction. No second output queue/task or
  handshake is required.
- Linux protects each `input_dev` event state with its IRQ-safe `event_lock`.
  Compatibility spinlock operations are compile-gated, while the task-only
  port keeps the same per-device critical sections with a priority-inheritance
  mutex: report parsing on CORE1 and KeyD LED/FF injection on CORE0 cannot race
  active `key`/`led`/ABS/MT state, `vals`, or `num_vals`. TinyUSB callbacks do
  not enter the input core. FF upload publishes `effects[]` and owners under
  this same boundary; erase protects playback/owner clear and rollback. Its
  order is `ff->mutex -> port_event_mutex`, while `input_ff_event()` inherits
  the already-held event mutex from `input_handle_event()`. The haptic private
  object is devres-owned, so its destroy hook clears `ff->private` before
  generic ff-core resumes the restored upstream `kfree(ff->private)` path.
  Disconnect takes this mutex only for Linux's
  release-key/SYN section and releases it before closing handlers. The active
  order is `driver_input_lock -> port_event_mutex` or
  `evdev_writer_mutex -> port_event_mutex`; no event-mutex scope waits for a
  USB completion, timer callback, or writer mutex. Holtek LED forwarding is the
  bounded exception: while holding the event mutex it briefly takes the
  transport mutex only to acquire a sibling-interface lease, then releases it
  before any USB request. Report and lifecycle paths drop transport before the
  input core, so there is no inverse edge. The embedded
  handle grows the allocator block by 8 B and its heap-backed FreeRTOS object
  costs 96 B, for 104 B per live `input_dev`; allocation failure follows the
  ordinary `input_allocate_device() == NULL` probe unwind.
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
  `CLOSED/RESUMING/OPEN` state under the transport mutex. A stack-owned host
  continuation makes open wait for the first physical interrupt-IN arm attempt,
  matching Linux's synchronous first `usb_submit_urb()`, before its 50-ms
  `HID_RESUME_RUNNING` drain. Callbacks take neither lifecycle mutex. FreeRTOS
  allocates both restored mutex objects dynamically, so the
  port checks construction as ordinary control flow and explicitly destroys
  them only after their Linux-owned objects are no longer reachable.
- The exact interrupt callback resolves its stable context through the live
  registry slot before dereferencing it, validates the full arm tuple, and only
  then publishes `QUEUED`. Invalid owner/epoch/buffer state clears the completed
  physical serial. A tuple with no live registry owner is retired and its fixed
  slot is cleared; a live owner has its back-reference restored, input stopped,
  and cleanup delegated to the teardown owner. Diagnostics are a bit in the
  existing lifecycle fault word, so the
  TinyUSB callback neither logs nor asserts after mutating report ownership.
  Recoverable host-owner and pinned hub-reset completion invariants
  use their existing task-side `-EIO` retirement path with the same
  lifecycle-owned diagnostic boundary. The hub callback always releases a
  slot it still owns from `HOST_COMPLETING`, so a diagnostic cannot pin the
  global EP0 lane.
  The common EP0/interrupt-OUT broker likewise selects completion by its unique
  request serial and validates slot state, generation, callback-provided
  address/endpoint, and bounded actual length before publication. It
  deliberately trusts the SHA-pinned TinyUSB callback to return its retained
  request object instead of rebuilding and comparing the SETUP tuple or buffer
  identity, and retires an invalid physical giveback as task-side `-EIO`.
- Async submission no longer treats every TinyUSB boolean rejection as a
  one-tick retry. A host-only tri-state precheck distinguishes gone, owned, and
  ready EP0/endpoint state. Each slot arms its durable wait before the deferred
  host call; central control-IDLE and exact non-control completion/abort/release
  edges clear it in either `SUBMIT_PENDING` or `QUEUED`. If a TinyUSB completion
  callback reacquires the resource after its idle publication, a BUSY precheck
  re-arms the exact wait in host-owner context before event dispatch can resume.
  A false submit after READY is terminal `-EIO`, while the absolute timeout
  remains only a watchdog.
  High-rate unrelated IN endpoints cannot wake an OUT waiter.
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
  captured boolean. `xSemaphoreTake(..., portMAX_DELAY)` blocks once on the
  FreeRTOS event list; the helper adds no retry loop or release-build fail-stop
  task.
  That assertion rule also covers `hid_async.c`, `usbhid.c`,
  `usbhid_report.c`, and the transport mutex; imported Linux/FreeRTOS sources
  remain untouched. Since TinyUSB callbacks also enter the transport mutex, its
  failure branches publish a fixed bit through lifecycle's existing indexed
  task notification instead of recursing through that mutex or calling the
  device-side async logger from the callback.
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
- The input grab path again calls the grabbed open handle directly, as Linux
  does. A valid grab already owns an open handle; the removed hypothetical
  `grab->open` check could silently discard all input while masking corrupted
  lifecycle state. Two port-only input-device discovery helpers with no caller
  were also removed from `input.c` and its reduced header.
- The device-side virtual keyboard now treats TinyUSB readiness as a durable
  predicate and report completion/failure plus mount-state transitions as wake
  edges. The former nine one-tick readiness polls are gone. Its sole producer
  waits for every keyboard or mouse IN completion before consuming the next
  event, replacing the producer-side one-ms cross-endpoint ordering guess.
  Ready-check plus submit runs as one deferred operation in TinyUSB's device
  task, ordered with reset/mount/unmount instead of racing endpoint state from
  the vkbd task. Synchronous submit rejection retains the report until a real
  lifecycle edge, and accepted reports retry after failed completion. The
  in-flight instance is atomically retired by unmount or the next mount:
  TinyUSB bus reset can discard an IN transfer without calling unmount or the
  failed-report hook.
  INPUT callbacks are filtered by exact instance, stale post-retirement
  givebacks are ignored, and reset/failure wins if notification bits coalesce
  with success. The producer publishes only its stack request pointer; the
  TinyUSB device owner claims the transfer instance immediately before the
  ready/submit pair, so an old completion cannot match a newly reused instance
  during that handoff.
- CDC output retains the hardware-tested logger policy: a producer whose chunk
  does not fit kicks the TinyUSB device owner with `usbd_defer_func()`, waits
  two ticks, and rechecks the mutex-protected free-space count. The first
  pressure event has a deliberate 1024ms ceiling so `/dev/ttyACM*` can appear
  and minicom can be opened; later connected waits use a 16-tick ceiling.
  This bounded device-side diagnostic delay is not a host lifecycle lock.
- Linux wait queues pair a durable condition with a wake edge. The lifecycle
  glue now follows that split directly: flags/cache slots own the condition and
  a dedicated indexed task notification wakes their sole task owner. This
  removes the firmware-only one-entry event queue without changing any
  Linux-derived source. Fixed-slot admission now follows the same model inside
  the generic USB bridge: lifecycle/workqueue callers, queued report heads, and
  CLEAR_HALT link nodes into one FIFO. Eligibility is the oldest endpoint-front
  with a real normal slot available; no waiter owns a placeholder reservation.
  Normal-slot release, unlink, or teardown supplies the wake, and synchronous
  callers retain a separate one-second local bound. HUB_RESET's dedicated
  physical recovery slot is excluded. Descriptor callers contain no FreeRTOS
  retry logic; `hid_get_class_descriptor()` is once again line-identical to
  upstream.
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
  `hid_async_task`; caller tasks wait, TinyUSB callbacks do not wait for request
  progress. Raw GET/SET and
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

- Linux `usbhid` keeps a 256-entry control FIFO per interface (effective ring
  capacity 255, explicitly sized for devices with more than 100 reports) and a
  64-entry interrupt-OUT FIFO (capacity 63). The port now preserves those two
  logical per-HID lanes ahead of its nine shared physical request slots. Only
  the current CTRL and OUT heads of each interface may enter `hid_async`; a GET
  retains its CTRL head through `hid_ctrl()` parsing, while unrelated raw EP0
  traffic can still pass it as in upstream. Every SET uses an enqueue-time
  snapshot. Queued, active, and parsing entries each retain one `io_pending`
  lifetime lease, and stop/unplug cancel both logical and physical layers.
  A pre-wire submit failure retains the lane head as `PARKED`, exactly like an
  upstream queue whose RUNNING bit was cleared. It is neither retried on a tick
  nor counted by ordinary `hid_hw_wait()`; the next same-lane enqueue is the
  restart edge. Teardown still owns and drains every parked lease. A parked GET
  also clears an ordinary waiter's parser-owner identity atomically with the
  idle decision so later restart cannot notify a returned stack owner. Since
  this predicate may become idle at a nonzero lease count, every lease release
  publishes the upstream-shaped waiter wake.
  Rather than embedding about 5 KiB of rings in every live HID, compact nodes
  are allocated on demand with the upstream count limits and a shared 4096-byte
  logical-node budget. Allocation/budget exhaustion is diagnosed and drops the
  void `.request()`, matching upstream's nonblocking full/OOM policy. A GET
  buffer is allocated only when that node reaches the CTRL head, avoiding one
  receive allocation per queued GET. The tested haptic-touchpad descriptor
  still queues only two mode SETs; a >9-feature-report fixture remains required
  to validate the newly decoupled logical depth on hardware.
- Upstream `hid-haptic` built manual-trigger output at byte zero even though
  `hid_alloc_report_buf()` reserves that byte for an absent report ID when the
  report is unnumbered. The adjacent firmware replacement shifts only that
  payload and counts the reserved byte exactly as `__hid_request()` does.
  Numbered reports, including tested `cafe:1004` report ID 6, are unchanged;
  unnumbered EP0 fallback and interrupt OUT now both remove the zero transport
  byte before sending the complete payload. A dedicated ID-zero fixture is
  still required before claiming hardware coverage for this generic bug fix.
- The emulator confirms the active multitouch `FF_HAPTIC` output path. A real
  touchpad and the remaining teardown/order checks are not hardware-verified.
- The compatibility delayed-work API has no active linked caller and is now an
  explicit compile-time port boundary. The removed split implementation could
  publish `delayed_pending`, lose a synchronous cancel, and then arm its timer;
  `mod_delayed_work()` could also leave an already queued work item beside a
  second delayed execution. A future driver using `INIT_DELAYED_WORK()` or its
  queue/mod/cancel APIs therefore fails compilation instead of acquiring a
  silently unsafe lifetime contract.
  `flush_work()` currently waits for full idle rather than Linux's generation
  boundary. Active LED/haptic work is bounded, and `cancel_work_sync()` retains
  Linux's false return for running-only work while still waiting for it.
  The clean future implementation should keep delayed entries and deadlines
  under the workqueue mutex, promote due work in the same worker, and use its
  existing notification with a nearest-deadline timeout. That removes the
  current timer-arm/cancel cross-lock race without another task or periodic
  poll. `destroy_workqueue()` also rejects callback-chained enqueue once drain
  starts, unlike Linux; no active callback relies on that behavior.
- `input_grab_device()` and `input_release_device()` have no linked caller and
  are now compile-gated instead of exporting weaker no-RCU semantics. The
  internal release helper remains active only inside lifecycle-owned final
  close. A future grab/proxy client must add mutation and cross-task delivery
  fences as one feature before removing the gate.
- Runtime dynamic quirks are now compile-gated as one exact upstream block.
  Active `hid_lookup_quirk()` reads only immutable static tables, so it no
  longer exposes an unlocked mutable list while the firmware lacks module
  parameter ingress and a matching mutex/module lifecycle.
- The retained upstream autorepeat callback does not enter the port event
  mutex. Evdev clears `EV_REP` before opening an input device, so no active
  repeat timer can be armed; restoring kernel repeat requires adding that
  task-context serialization at the same time.
- Upstream sysfs publication calls remain visible in HID core and multitouch,
  but the compatibility boundary now treats publication/removal as success/
  no-op. The firmware has no sysfs tree and a repository-wide search found no
  attribute accessor, iterator, or `sysfs_notify()` consumer. The dead registry
  lists, notification snapshots, and per-attribute heap nodes are gone;
  accessor/iterator APIs fail compilation until a real firmware proxy is added.
  A future proxy must restore publication and access together rather than
  silently relying on an empty fake registry.
- `device_enable_async_suspend()` is an explicit no-op because `CONFIG_PM` is
  disabled and firmware has no PM scheduler. Likewise, `hid_debug_*()` retains
  HID core's unconditional init/register/unwind shape but owns no state because
  no debugfs sink is linked. Both reductions are documented at their active
  compatibility definitions; enabling PM or a debug proxy requires replacing
  the corresponding stub, not inheriting it silently.
- `CONFIG_HID_BPF` is disabled and its compatibility hooks are no-ops. The BPF
  descriptor-rescan branch currently ignores `hid_set_group()`'s port-only
  `-ENOMEM` propagation; it is unreachable with the present identity fixup.
  Enabling HID-BPF requires auditing that rescan and runtime driver mutation as
  one feature rather than widening the active glue pre-emptively.
- The compatibility layer still uses one worker task instead of Linux's worker
  pools, so a blocking callback delays unrelated queues. Selection itself is
  round-robin by workqueue: after taking one item the supplying queue moves
  behind its siblings, preventing a replenished haptic queue from starving
  `system_wq` or another device. Active LED/haptic callbacks are bounded; add a
  separate worker only when an in-scope driver has a demonstrated independent
  blocking requirement worth its stack cost.
- The former two one-tick post-abort delays are gone. Report-IN and interrupt
  OUT use one TinyUSB FIFO continuation. Enumeration and ordinary HID share one
  global EP0 cancel transaction: raw abort success terminates immediately; a
  lost race captures only the already-published queue prefix and consumes the
  matching stage before DATA/ACK can chain. The active PIO alarm IRQ and every
  HCD abort share CORE1, so this is an exact finite fence rather than SOF/tick
  or stage-count polling. This is not yet a controller-neutral contract: an
  ESP/DWC2 adapter must publish explicit cancel completion before the common
  layer can reuse callback/buffer identity. The vendored PIO abort busy-wait is
  recorded separately in `tinyusb-host-port.md` as low-priority backend work.
- TinyUSB reports endpoint-submit failure as a boolean, so the transport cannot
  preserve Linux's separate `-ENOSPC`/`HID_NO_BANDWIDTH` classification. A
  physical interrupt-IN submit failure therefore enters the generic bounded
  retry/reset path. Recovering that distinction requires a richer TinyUSB/HCD
  result contract, not a parser-side guess.
- `usbhid_start()` does not apply Linux's polling module parameters or
  `HID_QUIRK_FULLSPEED_INTERVAL` before opening endpoints. The only pinned
  quirk-table user is the AFATECH AF9016 DVB device, outside the linked
  keyboard/mouse/multitouch/haptic profile; enable that class only together
  with this interval policy.
- TinyUSB's null-callback `tuh_control_xfer()` branch is a blocking compatibility
  path and may busy-wait while the caller pumps `tuh_task()`. No linked caller
  uses it: all firmware control transfers provide a completion callback and are
  owned by the host task plus a waiting non-host task. A future direct caller
  must not treat the null callback form as part of this asynchronous contract.
- Reduced compatibility `wait_event*()` and spinlock operations now fail a
  future caller at compile time. Their type/initializer shells remain because
  active HID objects contain dormant Linux members, but no linked path receives
  fake waiter-list or mutual-exclusion semantics. A future driver must add a
  real wait-list or explicit task-context ownership boundary before removing
  either gate.
- Compatibility `BUG_ON(expression)` evaluates its condition exactly once into
  a local boolean and passes only that identifier to `configASSERT()`. There is
  no extra silent busy-loop or release-build fail-stop policy. Its current
  repository caller is in the unlinked `ff-memless` path, but linking that
  driver no longer changes whether the expression itself is evaluated.
- The timer bridge does not reject a timer whose callback is `NULL`, and a
  zero-delay self-rearming callback could monopolize its single task. All
  active timers have non-NULL callbacks and rearm in the future, so neither
  compatibility difference is reachable in the linked driver set.
- Repository-wide wait review also found two intentionally separate non-host
  cases: the pointing task's single-tick IRQ burst coalescing delay, and CDC
  logger's bounded producer-throttle loop. Sensor `busy_wait_us` calls are
  datasheet SPI timing. None guards HID lifecycle state; changing the logger or
  sensor timing is outside this transport checkpoint.
- Device-side WebHID and MSC callbacks still execute FatFS and flash work in the
  TinyUSB device owner. Their FatFS mutex attempts are zero-time and cannot form
  the host self-deadlock fixed here, but a slow filesystem/flash operation can
  stall device-side USB service. A future device request worker/status state
  machine is a separate boundary; do not mix it into the host checkpoint.
- Vendored Pico-PIO-USB still has its documented abort busy-wait. Its dormant
  `pio_usb_host_stop()`/`pio_usb_host_restart()` APIs also wait on flags with no
  active clearing owner, but no linked firmware path calls them. Both belong to
  a future backend cancel/quiesce-completion contract; the active TinyUSB/Linux
  glue must not compensate with polling.
- Device-side keyboard `SET_REPORT` retains the hardware-tested shared
  `devmon_queue` path. Its blocking send can delay the TinyUSB device owner if
  the discovery queue is full, but KeyD independently drains that queue, so it
  does not form the host-side TinyUSB self-deadlock fixed here. WebHID and MSC
  callbacks remain a separate device-side worker boundary to audit.
- Linux runtime PM, autosuspend, remote-wakeup policy, and in-place usbcore
  reset are not present. The lifecycle coordinator deliberately performs a
  stronger teardown/re-enumeration because pinned TinyUSB cannot restore class
  state in place; this costs continuity but leaves no half-restored HID graph.
- Compatibility `atomic_t`, device refcounts, semaphore-owner metadata, and
  atomic bitops use GCC `__atomic` operations rather than FreeRTOS task critical
  regions. The `__*` bitops are again deliberately non-atomic like upstream.
  RP2040 implements atomic RMW with its dedicated Pico hardware spinlock and a
  very short local IRQ mask; it does not acquire FreeRTOS's global task/ISR
  locks. The compiler ABI also keeps this glue portable to the future ESP port.

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
- Audited task-owned `raw_event`, synchronous HID report request/wait, and
  returned GET data are active. Bounded task-side USB control and interrupt-OUT
  adapters are active with per-interface cancellation. HID report and generic
  control requests share same-device EP0 order; all interrupt-OUT sources share
  endpoint-keyed order. Interrupt-IN STALL recovery uses that generic EP0 lane
  for remote clear-halt, then performs the required PIO DATA0 reset and rearm
  in the TinyUSB host owner. A physical arm failure publishes only its exact
  generation/open revision; the report task applies `hid_io_error()` and the
  same bounded FAILED/TIMEOUT retry. Old-open failures cannot schedule recovery
  after close/reopen. Local CLEAR_HALT slot admission is wait-queue shaped rather
  than timer-polled: its linked generic admission node owns the capacity wake,
  and the report task rechecks the durable `WAIT_SLOT` state and actual free-slot
  predicate. There is no private latch or duplicated capacity state. Its task
  notification wait retains the absolute eight-second local bound without
  another timer. Once its wire request is active, close no
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
  enumeration scratch. Full configuration descriptors now independently keep
  that 512-byte static fast path and use an exact transient host-owner buffer
  for validated lengths through 4 KiB.
  A second SHA-pinned build-local source preserves TinyUSB `hid_host.c`'s full
  `hidh_open()` and `hidh_set_config()` blocks commented beside their two port
  replacements.
  Its two-pass current-interface scanner mirrors Linux's accepted HID-descriptor
  positions, caps opened/stored endpoints at `bNumEndpoints`, and publishes the
  TinyUSB class slot only after endpoint success. The set-config replacement
  skips enumeration SET_IDLE/SET_PROTOCOL and mounts with `NULL`; task-side
  `usbhid_parse()` retains upstream's SET_IDLE line and is the sole report-
  descriptor owner. No generic SET_PROTOCOL is added because Linux relies on
  the reset-default Report protocol. TinyUSB's upstream prefetch switch branch
  remains unchanged but unreachable because the replacement set-config path
  directly completes the class mount. Firmware-only device/string reconstruction
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
  does not take this path; CLEAR_HALT waits through generic admission, while a
  pre-wire submit error parks the lane and reports a local rearm failure. Reset
  progression is wait-queue shaped rather than polled: exact retirement,
  mount/enumeration, async-slot-release, and host-global-control-IDLE
  publications wake lifecycle, and only the phase deadline supplies a timed
  wake. Async EP0 idle is published after slot release so a hub retry cannot
  race its reserved slot's completion callback. The central TinyUSB control
  transition additionally covers native hub housekeeping, abort, remove, and
  watchdog release.
  Root and hub admission are atomic with one persistent global enumeration-owner
  predicate. That owner is recorded even if native enumeration began before the
  reset gate was acquired, and its terminal publication refreshes a phase which
  waited behind it. Exact-topology `enum_active` still determines the reset's
  own terminal result. Rejected host admission therefore parks until global
  control idle or the one TinyUSB enum terminal instead of sustaining a
  lifecycle-to-host defer loop.
  Reset requests use a generation-scoped per-interface claimant mask. Close can
  cancel only its own queued claim; sibling claims survive, while lifecycle's
  already-running reset is uncancellable like upstream running `reset_work`.
  `report_revision` separately prevents stale report recovery across reopen.
- Pinned TinyUSB omits its documented `tuh_mount_cb()` after hub enumeration,
  and `tuh_mounted()` becomes true before class-driver set-config completes.
  The build verifies the pinned `usbh.c` SHA and generates one build-local
  source with exact host-owner, enumeration-terminal, bounded-recovery, and
  global-control deltas. The complete active source inventory and grouped
  maintenance contract are recorded in
  [`tinyusb-host-port.md`](tinyusb-host-port.md). The helpers preserve
  `enum_new_device()`'s root/hub continuation semantics while replacing its
  blocking 50-ms root reset, 450-ms connection settle, 2-ms address recovery,
  and error-only 100-ms control retry with host-owned deadlines. The retry
  replacement preserves TinyUSB's bounded attempt count by copying the
  callback-local setup tuple into the enum epoch and reconstructing the request
  after callback unwind and EP0 idle. The sole TinyUSB event pump therefore
  remains runnable. Its public loop is the upstream-shaped
  `while (1) tuh_task();`.
  Inside generated `tuh_task_ext()`, the nearest armed deadline shortens the
  private queue wait while real HCD/deferred events wake it earlier; an idle
  host waits indefinitely, with no fixed periodic timeout. Foreign
  ATTACH events are deduplicated in bounded topology storage, pruned by REMOVE,
  and started by the next host-service iteration after terminal/mount unwind
  instead of being sent back into the sole consumer's queue with an infinite
  wait. Enumeration EP0 retirement now uses `hcd_edpt_abort_xfer()` as the
  physical cancel result. If PIO reports that completion won the race, the
  sole host consumer snapshots and drains exactly the FIFO prefix already
  present at that instant, consumes the matching EP0 event before it can chain
  DATA/ACK, and never extends the wait for later producers. No elapsed-time,
  queue-empty, or queue-capacity heuristic can clear a replacement owner. The
  fence captures `(daddr, complete_cb, user_data)` and clears its bookkeeping
  before invoking a callback, so a synchronously restarted enum epoch cannot be
  mistaken for the retired one. This proof is limited to current PIO/CORE1
  ordering; ESP/DWC2 requires an explicit backend cancel-completion contract.
  Exact unique CMake anchors preserve replaced upstream blocks beside the port
  and reject source drift. The pinned TinyUSB source inputs remain untouched.
  The same generated core validates the nine-byte configuration header. A
  513..4096-byte `wTotalLength` is retained as pending state until shallow
  host-owner service can allocate and submit outside the completion callback.
  `task.c` supplies mandatory platform allocator/free hooks; RP2040 uses
  FreeRTOS heap_4, while a future ESP backend can select DMA-capable internal
  memory without modifying the pinned TinyUSB delta. The allocation survives
  full-GET retries and is freed after synchronous class-open parsing or after
  exact terminal EP0 retirement on every failure/remove path. No Linux-derived
  parser or driver owns this buffer.
  A separate one-anchor generated copy of TinyUSB's PIO HCD completes
  `hcd_edpt_clear_stall()` by resetting Pico-PIO-USB's local DATA toggle. The
  HID report transport now calls only that generic HCD API; the vendored PIO
  implementation is unchanged by this checkpoint.
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
  The preliminary group scan now propagates its dynamic-table `-ENOMEM` too;
  an MT/haptic device therefore cannot fall through to generic binding merely
  because the first of its two parser passes met temporary heap pressure.
- `hid_report_enum` retains upstream's `report_list` and complete report-ID
  semantics but omits the dense 256-pointer `report_id_hash`. Exact-ID lookup
  scans the already-owned sparse list, normally one to three entries. The
  original member and every direct upstream access remain commented beside the
  port replacement. Sparse report IDs first reduce ARM32 `hid_device` from
  3,712 B to 640 B; removing the unused embedded sysfs registry reduces the
  current object further to 608 B without truncating the valid 0..255 ID range.
- Plain `kobject_uevent()` calls retain lifecycle counters but do not build a
  2.3 KiB environment: firmware has no userspace/netlink sink for it.
  `kobject_uevent_env()` remains available if a real environment consumer is
  added later.

## Checks

Verified through hardware checkpoint `usb: complete event-driven enumeration path`:

- compared the active HID/input files with Linux `83f14548`
- matched the active vendor allowlist against `CONFIG_HID_*`
- checked disabled source/link status and current proxy declarations
- `cmake --build build -j4`
- confirmed `sizeof(hid_async_request) == 60` and
  `sizeof(hid_async_slot) == 88` on the RP2040 ABI
- confirmed `sizeof(usbhid_device) == 248`, the four RX ownership/completion
  slots are 32 B each (128 B total), and scratch X is 708 B
- confirmed `sizeof(input_event) == 16` and `sizeof(port_input_event) == 8`
- confirmed the generated host source retains 34 exact upstream anchors and
  the public host loop remains `while (1) tuh_task();`; the new error retry and
  600-byte long-configuration paths passed their dedicated hardware fixture
- built and exercised `device/work-input-drivers`, including three repeated
  `cafe:1005` cycles after its injected full-configuration GET failure
- `git diff --check`
- observed two-board haptic cursor feedback

Current dirty-tree audit candidate:

- re-diffed all 29 linked Linux-derived C translation units against clean
  `83f14548` plus the named `8813b061` multitouch fix. Twenty-eight retain an
  upstream counterpart; `hid-drivers.c` is the documented firmware-only linker
  registry. No unexplained active-path replacement or P0/P1 divergence remains;
  bounded P2 contracts are listed above
- confirmed no periodic mutex/readiness polling remains in host/vkbd glue
- audited every remaining task wait: workqueue/timer/transport/vkbd loops sleep
  on a mutex, queue, or task notification and recheck a durable predicate. The
  former post-abort one-tick loops are now exact TinyUSB FIFO continuations or
  the common EP0 cancel-time prefix under the documented PIO same-core ordering
  contract.
  `msleep(50)` in `usbhid_open()` is the unchanged upstream resume drain and now
  begins only after the TinyUSB owner returns from its first physical arm
  attempt. Compatibility delay helpers are not lock polling.
- changed the workqueue, timer, and shared transport lock helpers to one
  blocking priority-inheritance mutex acquisition. The old failed-trylock
  retry loops and one-tick delays are gone. Their mutexes are released before
  callbacks and before every notification/USB/lifecycle wait; a separate
  lock-order pass found no active callback-dependent holder or reverse edge
- a repository-wide wait scan found one explicit task-level resource retry
  outside that glue: the documented bounded CDC producer-throttle loop.
  Remaining application delays are periodic key scanning, sensor
  protocol/reset timing, UI startup timing, and one pointer IRQ settle;
  WebHID's zero-time takes return BUSY immediately. Neither the CDC exception
  nor WebHID waits for a host lifecycle lock owner
- confirmed 45 exact SHA-pinned replacement anchors: 40 in the enum helper,
  two additional host-core anchors, two HID-class anchors, and one PIO-HCD
  anchor;
  async EP0/OUT admission wakes only on exact TinyUSB owner-release edges
- replaced the port-only 16+16 host-event handoff with one ordinary 32-entry
  TinyUSB/FreeRTOS queue. The dynamic definition omits OSAL's unused backing
  array, and TinyUSB's normal queue publication owns full-queue behavior
- made Linux `usb_device` cache exhaustion an event-driven enumeration
  admission condition. Fresh ATTACH, direct reset/re-enumeration, native
  replacement, and duplicate-restart paths all test the bounded free-epoch
  predicate before address assignment. If every epoch is active or retiring,
  TinyUSB retains the topology in its existing FIFO; final lifecycle release
  posts a deferred-function wake and the host rechecks capacity. A parked
  restart has an explicit owner-release callback, so it is neither reported as
  reset success/failure nor left holding the global enum gate. There is no
  allocation, retry sleep, periodic wake, or extra cache slot
- replaced enumeration's former time/capacity retirement heuristic with exact
  PIO abort result plus the FIFO prefix captured at the lost-abort instant. The
  matching EP0 completion is consumed before DATA/ACK can chain. A later
  matching REMOVE now clears an earlier duplicate-ATTACH restart request, while
  a genuinely later ATTACH sets it again; all four immediate/lost-abort event
  orderings were re-audited
- replaced generic HID's remaining three-stage EP0 retry/recovery heuristic
  with that same single global cancel transaction. A pending prefix retains the
  exact callback, buffer, async slot, and control-lane ownership; enum-first
  suppress wakes it on control idle, while HID-first cancellation finishes its
  callback before enumeration teardown proceeds
- retained every pre-wire-failed CTRL/interrupt-OUT head in an explicit stopped
  lane. A new same-lane enqueue restarts it in FIFO order; ordinary
  `hid_hw_wait()` may return without treating that stopped logical queue as live
  wire I/O, whereas teardown remains its lifetime owner. Parked GET abandonment
  clears the stale parser task under the same transport-lock predicate, and
  every lease release wakes the now-nonzero idle predicate
- split reset publication into per-interface claim bits and an uncancellable
  running owner, so one closing sibling cannot erase another interface's reset;
  retained TinyUSB's single global enumeration owner independently of the reset
  gate, preventing root/hub reset admission from colliding with enumeration that
  began before the gate
- restored upstream-shaped per-HID CTRL/OUT logical FIFOs in front of the nine
  physical slots. Dynamic metadata nodes use a shared 4096-byte request budget;
  SET payload is in the same allocation, and only an active GET head allocates
  receive backing. Admission is one generic FIFO over actual normal-slot
  capacity: request builders do not pre-reserve placeholder slots, and
  CLEAR_HALT keeps its admission node plus reconciliation bit in the RX owner
  rather than duplicating capacity-edge state. Current linked-image and object
  sizes are recorded only in [`pio-usb-memory.md`](pio-usb-memory.md).
- fixed the sole compound `CONFIG_HID_*`/CMake mismatch by linking the Holtek
  mouse fixup and leaving the unlinked game-controller ID generic
- made the UI diagnostic counters atomic across both cores; the restored
  one-slot callback diagnostic publisher intentionally retains its earlier
  `volatile` policy pending the main-repository logger work described above
- kept startup close to the hardware-tested `HEAD` order: TUD starts directly,
  and TUH starts after its five host runtime owners are created. No global
  READY/FAILED acknowledgements or USB-owner barrier remain
- made report release preserve its slot, HID pointer, and transfer buffer until
  host, transfer, and detach predicates are durably retired. The owner sleeps on
  exact wake edges. It emits one diagnostic immediately when the first checked
  predicate is still live, then waits without a timeout until an exact owner
  edge changes that predicate; it does not clear the slot early or add a
  release-build fail-stop state machine
- made the single firmware worker select workqueues round-robin instead of
  repeatedly draining the newest haptic queue ahead of every sibling
- made concurrent `cancel_work_sync()` callers retain a counted cancellation
  gate until every caller crosses its own completion boundary; one boolean
  could reopen `queue_work()` when only the first canceler returned
- compile-gated runtime dynamic quirks and public input grab/release rather
  than leaving callable APIs with deliberately omitted Linux mutex/RCU
  semantics; the complete upstream bodies remain visible beside each boundary
- compile-gated the dormant input refcount, handler iteration/removal, flush,
  and compatibility waitqueue/spinlock contracts whose Linux ownership is not
  present. Software-repeat setup remains linked because registration calls it;
  the active evdev boundary clears `EV_REP`, so its callback cannot arm in this
  firmware. A future consumer which retains `EV_REP` must add the event-lock
  bridge rather than treating repeat as a dormant API
- canceled an exact haptic effect slot's pending/running PLAY before in-place
  upload rewrites its report buffer. `ff->mutex -> cancel_work_sync() ->
  manual_trigger_mutex` has no inverse worker edge, so the old PLAY can no longer
  transmit the replacement payload
- restored `input.c`'s exact baseline procfs/sysfs/modalias/uevent presentation
  block under `#if 0` beside the firmware omission; this closes the only literal
  line-retention exception without linking dead kernel presentation code
- changed the build-local HID class endpoint pass to mirror upstream
  `usbhid_start()`: non-interrupt descriptors are ignored and only the first
  interrupt IN and first interrupt OUT are opened. Later endpoints no longer
  overwrite the class pair or consume PIO slots; the independent bounded raw
  snapshot still supplies the Linux interface shim. Dedicated bulk-plus-INT
  and multiple-INT fixtures remain hardware coverage, not a current claim
- made compatibility `BUG_ON()` evaluate its condition once outside
  `configASSERT()`; the already-computed boolean is a debug assertion, with no
  extra release-build stop loop. Device and kref lifetime counters use ordinary
  atomic positive-reference increment/decrement and a final release/acquire
  fence. They deliberately do not import Linux `refcount_t` saturation policy
- retained `*_CNT` for input-handler bitmap matching beside Linux's commented
  `*_MAX` block: `bitmap_subset()` consumes a bit count, so the upstream spelling
  omits each final valid capability bit in this reduced implementation
- restored Linux's direct grabbed-handle dispatch and removed two unused
  port-only input registry APIs. Failed optional haptic publication removes
  the `EV_FF`/`FF_HAPTIC` capability it just published; no second FF owner is
  linked for that input device
- preserved upstream's zero usage for a malformed COLLECTION with no preceding
  Usage: the embedded Linux parser-local array is always present and zeroed,
  while the port's lazy array must not be dereferenced before `usage_index`
  becomes nonzero
- moved the immutable multitouch driver descriptor to flash and made `pm_ptr()`
  match Linux's `NULL` expansion when `CONFIG_PM` is disabled; the unused MT PM
  callbacks no longer link. All active `configASSERT()` arguments in the host
  glue are captured booleans or identifiers, with no function call or side
  effect
- kept a canceled async slot in `RETIRING` until an outstanding TinyUSB
  cancel-completion/fence or resource-idle predicate is exact; cancellation no
  longer bypasses physical retirement and reuses the slot while EP0 can still
  retain its request/buffer
- made WebHID reset/unplug an idempotent abort edge for a retained read mutex
  and LIST/WRITE buffers
- confirmed no application compatibility helper retains
  `taskENTER_CRITICAL()`. The port-only TinyUSB host-event spill lock was also
  removed; publication now follows TinyUSB's ordinary queue path
- replaced the old noinline host-event adapter with TinyUSB's upstream-shaped
  inline `queue_event()`. The single 32-entry dynamic queue omits OSAL's unused
  static backing array; there is no spill FIFO, overflow flag, or firmware
  fail-stop callback
- `cmake --build build -j4`, followed by a no-work incremental rebuild
- current build-candidate size and UF2 hash are recorded only in
  [`pio-usb-memory.md`](pio-usb-memory.md); this file does not duplicate mutable
  dirty-build measurements
- `git diff --check`
- exact build identity and hardware verdict are recorded only in
  [`pio-usb-memory.md`](pio-usb-memory.md); do not infer either from this audit
