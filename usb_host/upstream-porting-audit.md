# Upstream Porting Audit

Updated: 2026-07-31

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
input-core, and FF paths through the Linux input-event publication boundary at
the preceding clean checkpoint. The current Wacom checkpoint activates seven
exact USB IDs. Its wired matrix passed after the devres and evdev identity
corrections; the later AES/receiver matrix passed after the receiver
sibling-initialization and HID-ordering lifetime corrections described below.
The separate Rapoo managed extra-input regression remains pending.
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

Checkpoint `hid: stabilize stadia ff teardown` preserves a historical Stadia/`ff-memless` implementation and
successful emulator run. The retained source now replaces its direct FreeRTOS
semaphore use and the two omitted ff-memless event-lock scopes with
compatibility priority-inheritance mutexes. A targeted `FF_RUMBLE` client
retested that conversion on 2026-07-22 in a temporary linked/instrumented host
image: two full cycles separated by reconnect passed timer stop, same-ID
replay, running-work unplug, and remove. The pair is now unlinked until
firmware has a product client; the ordinary layout hook publishes a virtual
`DEVMON_HAPTIC` request that plays a preloaded `FF_HAPTIC` effect and remains a
separate active path.
Upstream `hid-multitouch` plus `hid-haptic` remain linked, and their separate
two-Pico OUTPUT path has hardware cursor-feedback coverage.

The current source descends from hardware-verified `usb: complete event-driven enumeration path`; its latest
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
The repeated active-path audit, including the corrected Wacom-reachable devres,
receiver rebind, and HID-ordering contracts described below, found no remaining
P0/P1 lifetime, lock-order, or polling defect in the inspected
HID/input/work call graph. This result does not certify value-level power
snapshots, elapsed AES expiry, or removal-queue cleanup when the UI consumer
does not start; those bounded exceptions are listed below instead of being
hidden by speculative rewrites.

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

## Current Input/devres Contract Result

The working tree restores pinned Linux's `void devm_release_action()` and adds
`devres_destroy()` with Linux `0/-ENOENT` results and destroy-without-release
behavior. Managed input allocation and registration again use two
`struct input_devres` records in upstream order. CTL-472's unused touch and pad
inputs remove their allocation records before the final `input_put_device()`;
registered Pen teardown releases the later unregister record first and the
allocation reference second. The reduced device refcount and final
`input_dev_release()` own the port mutex and embedded devres cleanup, so no
local ownership flag or recursive action callback is needed.

The generic evdev identity path now reads `input_dev->id.vendor/product`, as
Linux evdev does, and treats input driver data as opaque. This preserves
Wacom's `struct wacom *` value and also covers generic hid-input, HID++, and
Rapoo, whose real registration paths all populate `input_dev->id`.

The devres compatibility scope is now documented beside its declarations, and
CMake follows the pinned aggregate order `wacom_wac.c`, then `wacom_sys.c`.
The compatibility header now documents the linked byte/record kfifo forms,
allocation, caller serialization, and unsupported extension boundary. Several
broad Wacom compile gates still lack immediate boundary notes; that is strict
porting-rule hygiene, not a runtime blocker. The input/devres and identity
contracts first passed the exact 32-reconnect CTL-472 artifact and then the
five-profile wired plus AES/receiver matrices with real `056a:*` identities,
balanced teardown, `oom=0`, and no host `ERR`. The separate Rapoo managed
extra-input regression remains pending.

## Current USB Wacom Contract Result

The active allowlist contains only seven USB Wacom products:
PTH-650 `056a:0027`, PTK-450 `056a:0029`, receiver `056a:0084`, CTH-470
`056a:00de`, CTL-472 `056a:037a`, CTL-672 `056a:037b`, and Yoga 260 AES
`056a:5048`. It reaches the pinned Pen/Pad/Touch, ExpressKeys, Touch Ring, LED,
arbitration, ordinary/AES battery, idle-proximity timer, and receiver
pair/unpair plus dynamic sibling-rebind paths applicable to those exact
profiles. Receiver lookup can select any child PID already in this table;
hardware receiver coverage is limited to child `056a:0027`. Bluetooth,
ExpressKey Remote, bootloader, I2C, PCI, and product IDs outside the
seven-entry table remain outside this checkpoint.

Exact `056a:0084` and `056a:5048` descriptors contain large VARIABLE Feature
reports whose one vendor usage is repeated by Linux to the report count. The
port quirk retains every report value and wire byte but materializes callbacks
only for explicitly declared usages, matching the linked Wacom consumers
without allocating hundreds of duplicate usage entries. The quirk is limited
to those two exact IDs.

Receiver work snapshots PID under the monitor parser lock, releases that lock
before rebuilding siblings, and lets a later monitor report queue the next
coherent pass. It synchronously cancels both original sibling `init_work`
objects before either dynamic resource group is released. Ordinary and AES
battery callbacks use nonblocking parser-lock retry because firmware
power-supply unregister frees directly while physical removal owns the same
lock and synchronously cancels work.

Receiver rebind exposes a connect-lifetime ordering allocation retained by
pinned `hid_disconnect()`: repeated `hid_hw_stop()`/`hid_hw_start()` otherwise
rebuilds `field_entries` and overwrites the old pointer. The port keeps
`hid_disconnect()` unchanged, calls the low-level synchronous stop, then clears
the field-ordering lists and allocations. The next connect rebuilds them after
the current Wacom input mapping. Allocation failure retains Linux's nonfatal
descriptor-order fallback and does not change `hid_connect()` return
semantics. A bare repeated `hid_connect()` without the required stop remains
outside this lifecycle contract.

If initial receiver-monitor probe queued `init_work` and its later
`hid_hw_open()` fails, the common failure path synchronously cancels that
callback before devres releases `struct wacom`. Reversible `usbhid_start()`
checks physical disconnect before and after buffer allocation, opens the
transport gate only for the same live interface generation, and preserves
`-ENODEV`/`-ENOMEM`. The pinned late-error shape remains: input or LED
registration failure after a successful restart releases the input resources
but leaves transport started until the next rebind or disconnect, and an
unchanged PID is not automatically retried. These error paths are
source-audited, not hardware-injected.

The CTH descriptor contains two padding-only reports whose ordering pass has
zero fields. Pinned `hid_report_process_ordering()` consequently calls
`kzalloc(..., 0)` twice. Linux returns `ZERO_SIZE_PTR`, permits the empty loops
to complete, and makes the later `kfree(ZERO_SIZE_PTR)` a no-op. The former
compatibility shim passed zero to FreeRTOS, which returned `NULL` and invoked
the malloc-failure hook even though the upstream zero-size request was not an
allocation failure. The compatibility contract now returns Linux's sentinel
from zero-size `kmalloc()`, `kzalloc()`, and `kcalloc()` and recognizes it in
`kfree()`. This removes the observed two false OOM increments per CTH attach
without changing pinned `hid-core.c` flow or adding allocation state.

The exact wired host
`4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5`
and emulator
`037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b`
completed all 23 physical Wacom attachments. The log contained 46 matching
input-node adds and removes, all 115 heap snapshots reported `oom=0`, and no
host `ERR`, transfer failure, timeout, or evdev input-drop diagnostic appeared.
The nine `HID_IGNORED` warnings belong to the nine CTL ghost interfaces; the
nine `EVDEV_BATCH_CAP` warnings belong to the four CTH and five PTH touch
inputs and were not accompanied by a recorded drop.

Ordinary power-supply cleanup has only indirect runtime evidence. One immediate
PTH disconnect left the expected single 256-byte queue allocation temporarily
visible, and a later snapshot returned from 60,496 to the normal 60,752-byte
removal plateau; five PTH generations did not accumulate queue storage. The
temporary value logger was removed before the successful run, so exact
`ADDED`/`CHANGED` fields (`present`, `status`, and `capacity`) were not observed.
Queue deletion still belongs exclusively to the UI consumer after it receives
`REMOVED`; failure to create or wake that consumer was not injected and is not
covered by this verdict.

The separate AES/receiver host
`8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8`
and emulator
`8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48`
completed four AES and four receiver attachments. All 20 input-node additions
had matching removals; all 47 heap snapshots reported `oom=0`; removal
returned to the established 60,496/60,752-byte plateaus after terminal physical
disconnect; every task watermark remained nonzero; and no host `ERR` or
input-drop diagnostic appeared. The receiver phase covers pending original
sibling initialization, held rebind/teardown controls, pair/unpair/re-pair,
physical disconnect, and recovery. The fresh rebind control stays open beyond
the stale deadline, so a surviving original callback would create a rejected
duplicate.

The available receiver capture reports child PID `033b`, which is outside the
active table and would be ignored. The emulator selects active profile `0027`
as protocol-equivalent coverage and does not claim that pairing was captured.
Production logs do not expose exact power-snapshot values or ordering, and a
device-side report acknowledgement cannot prove every pending battery-work
enqueue. The AES fixture does not wait for the real 30-minute expiry. The short
pre-PID receiver callback cannot be externally held after promotion or while
running without a host hook or SWD.

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
- Detached power-supply queues are deleted only when the UI task consumes
  their terminal `REMOVED` snapshot. The normal Wacom matrix indirectly showed
  that cleanup through recovered heap plateaus, but task/timer startup failure
  was not injected and the host startup path does not establish a separate
  power-consumer readiness contract. Exact snapshot fields also remain
  unverified without a production consumer.
- Runtime dynamic quirks, public input grab, second input clients, generic
  Linux logging, `mod_delayed_work()`, and `INIT_DEFERRABLE_WORK()` remain
  compile-gated or dormant. Ordinary delayed work is active for Wacom
  initialization and AES battery expiry and is audited below.
- `queue_work()` still checks `!hid_workqueue_mutex`. Current startup creates
  and validates the workqueue before TinyUSB host creation, so no linked HID
  caller can reach this branch. It predates the Wacom series and has no runtime
  effect, but it is proven dead defensive code under the current call graph and
  remains porting-hygiene debt until a separately authorized runtime cleanup.
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

The audit covers all 69 imported HID `.c` files (27 linked and 42 unlinked), all
six files under `linux/drivers/input`, all 53 headers under `linux/include` (45
compatibility-facing `linux/**` files plus eight asm/dt/UAPI files), the host
glue/header/CMake files, the root CMake wiring, and the SHA-pinned generated
TinyUSB/Pico-HCD transformations. This is the complete imported/host-port tree,
not only the Git diff or linked objects.
The later `keyd/port/device.c` event consumer was inspected for reachability but
is not included in the upstream-conformance result above.

## Audit Method and Per-File Result

The Linux source tree was first verified clean at the baseline commit above.
Each linked Linux-derived file was then compared with `git diff --no-index`
using the function order and exact statements, not only compiled behavior.
Every active hunk was classified as a missing-kernel-subsystem adapter, an
RP2040 memory policy, a FreeRTOS ownership primitive, or an explicitly named
generic bug fix. Each active replacement retains the original Linux
statement/block beside it; compile-time capability gates retain the exact
upstream block in their adjacent inactive branch and state the stage boundary
locally. The independent lifetime pass also followed each mutex, waiter, task
notification, callback publisher, and disconnect owner through its final
release. No unexplained active-path hunk or P0/P1 issue remains in the audited
scope.

The inactive files were also compared, rather than inferred safe merely because
CMake omits them. Most differ only by an adjacent, explained immutable driver
descriptor. One is an explicit enablement blocker:

- `usbhid/hiddev.c` is a firmware proxy rewrite, not a line-preserving port of
  Linux's fd/ioctl/fasync implementation. Move that proxy to glue and restore
  the Linux-derived file before enabling it.

`hid-google-stadiaff.c` and `ff-memless.c` are retained, audited, and currently
unlinked by product policy rather than by a compatibility blocker. Stadia uses
the compatibility `struct mutex` instead of embedding FreeRTOS API in the
Linux-derived driver; every play, work, and remove caller is task-context, and
the adjacent upstream spinlock lines remain visible. The two ff-memless
upstream `event_lock` scopes use the same per-input `port_event_mutex` as the
input/FF paths. The firmware timer callback runs outside the timer mutex, so
this adds no reverse timer/event lock edge. Their hardware retest passed on
2026-07-22 as recorded below and in `hid-emulator-coverage.md`.

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
Together with `hid-cmedia.c`, `hid-google-stadiaff.c`, and hiddev, this accounts
for all 42 unlinked HID `.c` files.

The dormant `hid-core.c` `new_id` block is unreachable because firmware
publishes no driver groups, but it remains a partial local parser/lockless
runtime-ID rewrite. It should be compile-gated or restored to upstream form
before any runtime new-ID interface is added. `hid-cmedia.c` is line-preserving,
but its CM6533/hiddev half remains intentionally disabled. These dormant debts
do not weaken the active-path result and are not certified for enablement.

The compatibility-header pass now finds Linux-visible `devm_release_action()`
and managed-input return/ownership semantics on the linked paths. The sole
active `array3_size()` caller lacks Linux's generic overflow helper but is
bounded by the 4096-byte
report-descriptor cap. `DEFINE_MUTEX()` does not construct its FreeRTOS handle,
and `mdelay()` sleeps instead of busy-waiting. The kfifo glue implements the two
linked shapes: byte FIFO and two-byte-header record FIFO, with Linux
power-of-two allocation and return counts; locking and the broader typed API
remain unsupported because every linked owner serializes its own accesses.
The `kmalloc()`/`kzalloc()`/`kcalloc()` contract now returns Linux's
`ZERO_SIZE_PTR` for zero-size requests and `kfree()` accepts that sentinel;
this is reached by CTH padding-report ordering rather than being a hypothetical
future compatibility case.
Selective nested devres groups release only their enclosed resources in reverse
order, while final input/HID teardown releases all remaining resources.
Devres documents its teardown-serialization contract beside the declarations,
and kfifo documents its two linked forms and caller-owned serialization beside
the compatibility types. Generic typed/devm allocation helpers pre-multiply
sizes without Linux overflow semantics, while current counts are bounded. Any
new caller must re-audit or compile-gate these helpers.
The port also assumes little-endian targets, valid for RP2040/RP2350 and ESP,
without an explicit compile-time byte-order guard. Finally, `input.c` currently
gets generic compatibility primitives by including `hid.h`; that upward layer
dependency is marked as cleanup debt, not claimed as upstream structure.

The linked non-compatibility private headers `hid-ids.h`, `hid-haptic.h`,
`input-core-private.h`, and Wacom's `wacom_wac.h` are byte-for-byte identical
to the pinned baseline. The table also records the two audited deferred FF
sources so their enablement contract remains visible.

| Linux-derived file | Remaining explained differences |
| --- | --- |
| `hid-input.c` | Local include path and immutable `hid_driver` pointer only. |
| `input-mt.c` | Local includes, task-context event-mutex boundaries, and upstream commit `8813b061` for the active-slot bitmap. |
| `ff-core.c` | Explicit unlocks replacing Linux `guard(mutex)`, `port_event_mutex` replacing IRQ event-lock scopes, and checked/destroyed heap-backed FreeRTOS mutex state. |
| `ff-memless.c` | Its two upstream `event_lock` scopes use the per-input task-context PI mutex; the original `guard(spinlock_irq*)` lines remain adjacent. |
| `hid-haptic.c` | Five-slot firmware RAM policy plus the documented unassigned-usage, unnumbered-report-ID, HOST/DEVICE mode, erase, and queued-work lifetime fixes. |
| `hid-google-stadiaff.c` | Upstream spinlock sections use the compatibility task-context PI mutex, which is checked and destroyed because its firmware backing is heap-owned; no direct FreeRTOS API remains in the driver. |
| `hid-core.c` | Sparse full-range report-ID lookup, heap-backed parser locals, constrained INPUT-array value storage, exact-ID explicit-feature-usage compaction for Wacom `056a:0084/5048`, restored reduced HIDRAW lifecycle/report calls, raw-event-only protocol ingress before final evdev activation, mutable runtime state beside flash-resident driver descriptors, and post-transport-stop release of connect-lifetime field ordering for reversible Wacom rebind. |
| `input.c` | Task-context input event mutex; pinned two-resource managed-input lifetime and `input_put_device()` final release through the reduced device refcount; Linux presentation/PM/userspace code retained under `#if 0` around the active upstream `input_dev_release()` callback. |
| `hid-magicmouse.c` | USB-only Mouse 2/Trackpad 2 IDs, three unreachable delayed-work statements retained beside the firmware gate, sparse full-range report-ID lookup, a documented 90-second firmware battery interval beside upstream's 60 seconds, and an immutable driver descriptor. Raw parsing and MT event flow remain upstream. |
| `hid-logitech-hidpp.c` | Full pinned source with direct request/reply, pre-connect identity, and battery stage gates; sparse report-ID lookup; cross-task response-state lock; exact-interface wait cancellation; two direct USB IDs; and an immutable driver descriptor. The production path has no test trace API or otherwise unused RAP/FAP probe; broader upstream subsystems remain visible but unreachable. |
| `hid-logitech-dj.c` | Full pinned source with receiver `046d:c52b` active and the other upstream receiver IDs retained behind `CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS`; firmware work/lifecycle integration, final evdev activation, sparse report-ID lookup, immutable driver metadata, and virtual-child raw requests routed through the physical receiver. The upstream multi-slot mouse/keyboard/HID++ descriptor and child model remains intact. |
| `wacom_sys.c`, `wacom_wac.c`, `wacom.h` | Full pinned Wacom flow with exact USB allowlist `056a:0027/0029/0084/00de/037a/037b/5048`; four report-ID hash reads use the sparse registry; the shared-device list and receiver sibling lookup rely on the lifecycle owner; Pen/Pad/Touch, LED, ordinary/AES/receiver battery, timer, and receiver rebind paths are active. Receiver lookup can select any child PID in that table; only child `0027` has receiver-path hardware coverage. Bluetooth, Remote, bootloader, I2C, PCI, and all other product IDs remain gated; the driver descriptor is immutable. |
| linked vendor drivers | Local includes, immutable driver descriptors, and the required generic post-`hid_hw_start()` probe unwind; the Rapoo replacement retains both complete upstream return branches. |
| `usbhid.c`, `hidraw.c`, `power_supply.c`, `leds.c`, `evdev.c`, host task files | Deliberate TinyUSB/FreeRTOS glue, audited against the corresponding Linux lifecycle rather than claimed as copied source. HIDRAW is lifecycle-only; power-supply events cross as detached coalesced value snapshots; Wacom LEDs retain control/work lifetime without Linux sysfs; receiver rebind uses lifecycle-owned borrowed sibling lookup. |

The exact scalar timestamp replacements now retain Linux's `ktime_set()`,
`ktime_compare()`, and `ktime_get()` lines. The managed-input path restores the
complete upstream wrapper, allocation, register, error-label, and two-phase
unregister blocks; only final device release is connected directly to the
reduced device core. The out-of-line firmware definition of `input_sync()` also
retains the exact upstream header block beside the linkage-only replacement.

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
- `hid-core`: after `hid_hw_stop()` has disconnected clients and the low-level
  driver has synchronously stopped report producers, release the
  connect-lifetime field-ordering graph. Pinned Linux otherwise overwrites the
  retained allocation when Wacom receiver rebind performs the documented
  stop/start sequence. The next `hid_connect()` rebuilds the graph and keeps
  the existing nonfatal allocation fallback.
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
| direct HID++ response | one send-mutex-serialized waiter, durable response predicate, notification index 1, and absolute five-second attempt deadline | Register-before-test is preserved; report-task response and exact-interface disconnect are durable wake conditions. No callback waits or report-time allocation. This reduced wait contract is valid only for the linked single waiter. |
| post-abort HCD retirement | one global EP0 cancel transaction plus exact cancel-time FIFO prefix; one-shot non-control fence | No tick/SOF/stage-count polling. Current proof relies on PIO alarm IRQ and HCD abort sharing CORE1; ESP/DWC2 needs an explicit backend cancel-completion edge. |

WebHID's zero-time FAT mutex attempts intentionally return busy from a USB
callback and do not retry. Queue receives with zero timeout are bounded drains,
not mutex acquisition loops. No lifecycle/workqueue/timer compatibility lock
masks interrupts. The only new generated-host critical section is the bounded
ISR/task event handoff: zero-time queue operation, counters, or one event copy;
it contains no callback, logging, allocation, or wait.

## Conforming Areas

- CMake links 23 vendor driver descriptor translation units across 22 enabled
  vendor `CONFIG_HID_*` families (the compound Holtek config contributes
  keyboard and mouse fixup drivers). Generic `hid-multitouch` and `hid-haptic`
  are also linked. Stadia has no reduced config gate and is excluded simply by
  leaving its source out of CMake. The unlinked game-controller-only
  `hid-holtekff` ID is left generic instead of being marked as having an absent
  special driver. Active vendor/generic changes keep adjacent upstream lines
  and reasons; no unrelated vendor-flow rewrite was found.
- The imported `hid-magicmouse.c` matches only USB Magic Mouse 2 and Trackpad 2
  IDs. Its synchronous mode SET runs from lifecycle task context through the
  existing async EP0 owner. USB Mouse 2 returns before the upstream delayed
  retry site, while USB Trackpad 2 cannot satisfy that Mouse-2-only condition;
  the three delayed-work calls and every excluded Bluetooth/legacy ID remain
  visible beside the explained firmware boundary. Full 15-contact behavior is
  not claimed because the later evdev-to-KeyD queue remains a separate bounded
  consumer.
- The imported Wacom sources retain the complete pinned switch/parser flow and
  match only PTH-650 `056a:0027`, PTK-450 `056a:0029`, receiver `056a:0084`,
  CTH-470 `056a:00de`, CTL-472 `056a:037a`, CTL-672 `056a:037b`, and Yoga 260
  AES `056a:5048`. Their active paths retain upstream Pen, Pad, Touch,
  ExpressKey, Touch Ring, LED, arbitration, shared-data/devres, delayed
  initialization, ordinary/AES/receiver battery, timer, and receiver-rebind
  logic. All other Wacom IDs remain behind `CONFIG_HID_WACOM_ALL_DEVICES`.
  The separate wired and AES/receiver pairs completed their hardware matrices
  with balanced input lifetimes, stable removal plateaus, and `oom=0`. Exact
  queued power values, elapsed AES expiry, and consumer-start failure remain
  outside those runtime results. The Rapoo managed extra-input regression
  remains separate.
- The direct-HID++ path imports pinned `hid-logitech-hidpp.c` whole. Direct USB
  matching selects only IDs `046d:c08d` and `046d:c08a`; the current
  exact-class gate additionally selects DJ child IDs M560 `046d:402d`, T650
  `046d:4101`, K400 `046d:4024`, and K750 `046d:4002`.
  The direct IDs reach `.probe`, `.remove`, and `.raw_event`; the exact child
  classes also reach only their upstream `.input_configured` and
  `.input_mapping` paths. The
  active request path retains upstream protocol detection, RAP/FAP builders,
  answer/error matching, send mutex, BUSY retry, and work item. A port-only
  raw-event-only ingress reuses `hid-core` validation and `driver_input_lock`
  while stopping before field/input parsing, so `hid_device_io_start()` can
  receive probe replies without bypassing final evdev activation. The
  request/reply foundation has exact-artifact hardware coverage. The current
  host-only gate additionally reaches upstream HID++ 2.0 pre-connect name and
  unit-ID/serial discovery; its Linux-only `%4phD` and packed `u32` access have
  adjacent firmware-safe replacements. The evdev client retains the final
  bounded name across queued ADD/removal, and the KeyD task reports VID:PID and
  name. The unit ID remains in Linux HID/input state rather than crossing the
  firmware-only `port_input_dev` boundary. Direct battery discovery/event paths
  are active through a reduced managed `power_supply`:
  `power_supply_changed_work()` overwrites a detached full snapshot in one
  length-one queue per supply. Those queues belong to a separate devmon power
  QueueSet consumed by the UI task, not to the ordinary devmon/KeyD path. The
  UI currently discards `ADDED`/`CHANGED` values and
  removes the queue after terminal `REMOVED`; presentation is deliberately
  deferred. Generic HID battery strength is active through the same reduced
  power-supply boundary; HID++ sysfs, FF, broad vendor-key classes, Bluetooth,
  and legacy 27 MHz matching remain compiled out of reach. M705/M560 wheel
  handling, T650 WTP raw XY, K400, K750 solar,
  and M560/T650 delayed input initialization are now reachable only through
  the listed exact DJ IDs. The complete automatic sequence later passed with
  temporary `8/256` parser limits.
  Hardware also established the retained `64/675` capacity boundary recorded
  in `hid-emulator-coverage.md`: simultaneous M705 and ordinary keyboard
  children fit, while the complete HID++ eQuad keyboard child does not.
  The exact M560/T650/K400/K750 extension and its byte-exact emulator completed
  the full automatic hardware sequence on 2026-07-26, including both delayed
  class generations, K400/K750 input, expected retained-capacity behavior, and
  the final direct regression.
- The first reduced single-M705 DJ checkpoint was replaced by the full pinned
  upstream port. Runtime matching enables `046d:c52b`, while the other receiver
  IDs stay behind `CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS`. The upstream
  multi-slot child table, mouse/keyboard/Consumer/power/media/HID++ descriptors,
  pair/unpair/link-loss/connection handling, and virtual-child raw-request
  routing remain present. Firmware adaptations provide task-owned work,
  lifecycle destruction, sparse report lookup, and final evdev activation.
  Hardware comparison proved the complete HID++ eQuad keyboard lifecycle at
  temporary `8/256`. At retained `64/675`, standalone and simultaneous M705
  and ordinary keyboard children work; only the standalone complete eQuad
  keyboard profile reaches the RP2040 capacity boundary documented in
  `pio-usb-memory.md`. The earlier simultaneous-child limit was measured with
  the upstream-sized 256-field table.
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
- `ff-core` remains linked for HID Haptics; `ff-memless` and Stadia do not.
  Evdev clears `EV_REP` because KeyD owns held-key state and discards Linux
  repeat value `2`.
- AppleIR remains unlinked. The timer bridge now serves multitouch's 100-ms
  sticky-contact release. Standard HID Haptics waveform duration is
  device-managed and has no FF duration timer; the retained ff-memless timer
  path runs only when that deferred source is linked.
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
  port sends ordinary GET completion to the report task, but a GET queued while
  probe owns `driver_input_lock` publishes its request buffer directly to that
  interface's lifecycle owner. An inner `hid_hw_wait()` consumes it with the
  lock retained; the outer activation fence can consume it after probe releases
  the lock, and glue selects the matching parser entry. This avoids a
  firmware-only queue handoff without opening interrupt input on a half-built
  device. The existing per-interface wait head carries the corresponding
  upstream-style wake edge; the broader firmware `io_pending` lease remains
  the durable predicate, so `hid_hw_wait()` no longer needs one-tick polling.
  After producer stop, teardown cancels by exact HID owner and uses the same
  wait head for a composite `usb_kill_urb()` predicate: no aggregate I/O, async
  slot, interrupt-IN owner, deferred host pass, or physical-detach fence
  retains the interface. Every last-owner transition publishes the wake after
  unlocking; the three former teardown polling barriers are gone without
  another RTOS object.
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

## Force-Feedback Status

- `hid-multitouch`/`hid-haptic` and `ff-core` are active. Standard HID Haptics
  uses `input_ff_create()` with five firmware-owned effect slots and
  device-managed timing. Linux's 96-slot call and allocation remain commented
  beside that memory-bounded replacement.
- Stadia/`ff-memless` are retained but unlinked until a product `FF_RUMBLE`
  client exists. If both CMake entries are restored together, Stadia uses the
  upstream memless 16-slot policy and timer path.
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
- The final post-probe evdev snapshot publishes whether that input supports
  `FF_HAPTIC`. KeyD then owns a small per-device map of five standard HID
  Haptics effects, uploads them once on ADD, and discards the local IDs on
  REMOVE; reconnect therefore uploads a fresh set for the new evdev lifetime.
  The layout hook ignores the startup notification and queues a nonblocking
  virtual `DEVMON_HAPTIC` Press request through the same devmon path as virtual
  LEDs. It does not emit `FF_RUMBLE`, so it does not exercise Stadia. Preloading
  Press/Release retains HOST haptic mode for the attachment. The 2026-07-23
  lifecycle pass confirmed HOST/DEVICE transitions, replacement, erase, and
  fresh preload after reconnect; the ordinary layout-change trigger remains
  the product client rather than the removed test generator.
- ff-core retains an old definition throughout replacement upload and retains
  an erased definition while the driver callback runs. `hid-haptic` uses those
  upstream contracts to decide HOST/DEVICE ownership; it cancels a slot's
  pending PLAY before rewriting it and drains all effect/STOP work before
  teardown frees its dependencies.
- The combined haptic lifecycle fixture hardware-verified all five preloaded
  effects, replacement, erase/re-upload, HOST/DEVICE transitions, numbered and
  unnumbered output, queued-work unplug, remove, and reconnect. Runtime results
  are recorded in `hid-emulator-coverage.md`.
- Fixture `ErgoType-hid-devices:device/google-stadiaff` passed the retained
  mutex conversion before deferral with a temporary targeted `FF_RUMBLE`
  client, including timer stop, replay, running-work unplug, clean remove, and
  reconnect.

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
  A transient TinyUSB `BUSY` is instead physical endpoint ownership, not
  `usb_submit_urb()` failure. The accepted head waits for the exact idle or
  cancel edge without the former firmware-only one-second submit watchdog.
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
  byte before sending the complete payload. The 2026-07-23 lifecycle pass
  hardware-verified both ID-zero routes (`cafe:1007` interrupt OUT and
  `cafe:1008` EP0 fallback).
- The emulator confirms the active multitouch `FF_HAPTIC` output and teardown
  paths. A real physical touchpad, accepted mode-SET `-EIO`, unplug during that
  SET, and high-contact queue saturation are not hardware-verified.
- The compatibility delayed-work API is active for Wacom's one-second
  `init_work` and AES battery expiry. Deadlines, delayed entries, ordinary FIFO
  promotion, pending/running state, and synchronous-cancel gates share the
  workqueue mutex. The worker uses the nearest wrap-safe deadline as its
  notification timeout, so there is no timer-arm/cancel cross-lock race or
  polling task.
  `cancel_delayed_work_sync()` removes either a deadline-list or already
  promoted entry and waits for a running callback; `cancel_depth` keeps
  callback requeue closed until every simultaneous synchronous canceler has
  returned. A callback can otherwise requeue itself after the worker clears
  `pending`. Queue destruction immediately promotes firmware-owned delayed
  entries before draining so neither work nor queue storage is abandoned.
  This is a stronger port lifetime rule than Linux, where callers must cancel
  timer-side delayed work before destroying its queue.
  `flush_work()` still waits for full idle rather than Linux's generation
  boundary, and destruction rejects callback-chained enqueue once drain starts;
  no linked caller relies on either difference. `mod_delayed_work()` remains a
  compile-time boundary because it has no audited caller. Wacom's separate
  `TIMER_DEFERRABLE` idle-proximity timer retains not-before-deadline and
  cancellation semantics, but the always-running firmware timer task may run
  promptly at the deadline rather than waiting for a later non-idle wake.
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
- Reduced compatibility `wait_event_timeout()` implements only the linked
  HID++ single-waiter contract while retaining Linux's `0`/positive return
  values: `send_mutex` serializes the waiter, task registration precedes the
  durable predicate test, and notification index 1 is a wake edge. Exact-bound
  interface disconnect is a separate durable HID++ condition which the driver
  maps to `-ENODEV` after the standard wait returns. Spinlock operations and
  unsupported interruptible wait forms remain compile-gated. A second or
  concurrent waiter requires a real intrusive waiter list; it must not inherit
  this reduced contract silently.
- Compatibility `BUG_ON(expression)` evaluates its condition exactly once into
  a local boolean and passes only that identifier to `configASSERT()`. There is
  no extra silent busy-loop or release-build fail-stop policy. Its current
  repository caller is in the unlinked `ff-memless` path; linking that helper
  still evaluates the expression exactly once before the assertion.
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
- The complete pinned `hid-uclogic` core, parameter, and descriptor sources are
  linked for a narrow ID allowlist. Both descriptor headers and
  `hid-uclogic-rdesc.c` remain byte-identical to pinned upstream. Core differs
  only at the unavailable private-usbhid include, the locally explained active
  ID gates, and immutable driver registration; params differs only at that
  private include, the single allocate/copy/free replacement for upstream
  `krealloc()`, and a firmware optional-product-string guard. The original
  displaced lines remain adjacent.
- UC-Logic activates the existing task-side USB string and interrupt-OUT
  contracts. Reduced `list.h`, `ctype.h`, `string_choices.h`, and KUnit
  visibility headers expose only the reached upstream surface; `__force` is a
  no-runtime Sparse annotation and the little-endian conversions are valid for
  every supported firmware MCU target. The selected string consumers are
  ASCII; the reduced decoder supports BMP code points and substitutes `?` for
  UTF-16 surrogate units, so the ID gate must not widen to a non-BMP consumer
  without extending that contract. The selected wireless UGEE-v2 paths reuse
  the existing reduced power-supply snapshot through pinned generic HID
  battery code. No generic URB, HIDRAW consumer, VFS, KeyD tablet policy, UI,
  or LED contract was added.
- Enabling generic HID battery also reaches the pinned Magic Mouse/Trackpad
  battery path. Its upstream dense `report_id_hash` lookup is retained beside
  the sparse firmware lookup that preserves the full report-ID range. The
  immediate GET and timer flow remain upstream; firmware changes only the
  repeated USB battery interval from 60 to 90 seconds, with the displaced
  constant retained beside the documented policy.
- Reduced `ktime_t` remains a four-byte modulo-2^32 millisecond value so input
  object sizes do not grow. It is now unsigned, avoiding signed overflow; the
  accepted ordinary `a > b` comparison does not retain Linux ordering across
  the roughly 49.7-day FreeRTOS tick wrap. Its only linked ordering consumer is
  duplicate unchanged generic-battery notification: first reports, capacity
  changes, and charging changes bypass that comparison.
- FreeRTOS queues use the 8-byte `port_input_event`; evdev converts from the
  16-byte kernel `input_event` at the devmon boundary. `__KERNEL__` selects the
  kernel UAPI layout without exposing unavailable newlib ioctl headers.
- `hidraw.h` and `hidraw.c` now implement only connect, claim, report-event,
  disconnect, and a small per-device lifecycle object. With no subscriber,
  reports are not retained and report ingress allocates nothing. There is no
  descriptor/GET/SET consumer API, queue, VFS, device node, fd, ioctl, poll, or
  multi-open state. Hiddev remains disabled and active code uses stubs.
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
  Generic URBs, synchronous interrupt-IN, unaudited hooks, fuller HIDRAW
  consumer functionality, hiddev runtime, and PIDFF remain deferred. The
  linked direct-HID++ slice uses the already implemented synchronous raw
  SET_REPORT plus continuous interrupt-IN response path. The linked DJ slice
  uses sparse report lookup, receiver raw requests, and virtual-child report
  forwarding without generic USB URBs.
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
- exercised the haptic/Magic Trackpad 2 paths on 2026-07-23:
  three haptic transport profiles, five-slot lifecycle, queued-work unplug and
  reconnect, four Trackpad interfaces, native input, and reconnect all passed
  in repeated hot- and cold-start runs with `oom=0`
- `git diff --check`
- observed two-board haptic cursor feedback

Current checkpoint audit:

- retained the prior whole-file audit and diffed the newly linked
  `hid-logitech-hidpp.c`, full pinned `hid-logitech-dj.c`, and corresponding
  `hid-core.c` ingress/lifecycle changes against clean `83f14548`, then audited
  both linked Wacom translation units against the same pin. There are now 36
  linked Linux-derived C translation units; thirty-five have an upstream source
  counterpart and `hid-drivers.c` is the documented firmware-only
  linker registry. The raw-event-only signature and ordinary call sites retain
  their exact upstream forms beside the added argument. The active devres
  contract now follows pinned Linux and CMake follows the aggregate Wacom
  order. The reduced kfifo contract is documented beside the compatibility
  types. Remaining Wacom gate comments are the explicit porting-hygiene
  exceptions above; bounded P2 contracts are listed above
- audited the newly linked UC-Logic sources against pinned
  `83f1454877cc292b88baf13c829c16ce6937d120`: its complete upstream device
  table remains visible. Huion `256c:006d/006e` and Deco 01 V2 `28bd:0905`
  completed their automatic hardware matrix on 2026-07-26 with `oom=0` and a
  92-word minimum lifecycle watermark. The narrow expansion adds only Deco
  L/LW `28bd:0935` and Deco Pro S/SW/MW `28bd:0909/0933/0934`; its expanded
  battery/reconnect/input matrix passed on hardware on 2026-07-27 with every
  completion marker, no host `ERR`, `oom=0`, equivalent cleanup plateaus, and
  an 86-word minimum lifecycle watermark
- audited the complete pinned Wacom parser/system sources and the five active
  wired call graphs. Host
  `4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5`
  with emulator
  `037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b`
  completed
  `f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10`, 23 physical
  attachments, 46 balanced input-node add/remove lifetimes, nine expected
  ghost-interface warnings, nine bounded touch-batch warnings, nonzero task
  watermarks, and `oom=0` in all 115 heap snapshots. The CTH ordering pass
  reaches two zero-size allocations per attach; restoring Linux
  `ZERO_SIZE_PTR` semantics removed their false OOM accounting without changing
  upstream flow. Generic promotion-window, simultaneous-cancel,
  callback-requeue, destroy-with-delayed, and tick-wrap branches remain static
  audit results. Power-snapshot values and UI-consumer startup failure remain
  unverified
- extended that source/call-graph audit to exact AES `056a:5048` and receiver
  `056a:0084`, including explicit-feature compaction, timer and battery work,
  sibling initialization cancellation, PID snapshotting, reversible
  stop/start, and field-ordering release
- audited the active AES/receiver Wacom paths and their receiver-specific
  lifetime corrections. Host
  `8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8`
  with emulator
  `8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48`
  completed four AES and four receiver attachments, 20 balanced input
  lifetimes, pair/unpair/re-pair, pending sibling-initialization cancellation,
  held rebind/teardown controls, physical disconnect, recovery, nonzero task
  watermarks, and `oom=0` in all 47 heap snapshots. The fixture does not prove
  exact power values/order, elapsed AES expiry, or the short original pre-PID
  callback after promotion/while running
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
