# Linux HID Port Contract

Rules: `usb_host/upstream-porting-rules.md`.

This document defines the contract between the pinned Linux HID/input sources
and the firmware port: accepted differences, ownership rules, target-memory
policies, downstream fixes, and boundaries that must be revisited when the
port grows. It records the lasting result of the source audit, not its history.
Hardware chronology and remaining runtime qualification belong in
[`hid-emulator-coverage.md`](hid-emulator-coverage.md), current RP2 memory
accounting in [`rp2.md`](../docs/hid-host/rp2.md), and the SHA-pinned TinyUSB
maintenance contract in [`tinyusb-host-port.md`](tinyusb-host-port.md).

Linux baseline: `../linux-upstream-hid` at
`83f1454877cc292b88baf13c829c16ce6937d120`.
Post-baseline upstream fixes are the multitouch active-slot bitmap commit
`8813b0612275cc61fe9e6603d0ee019247ade6be`; the Wacom ToolSerial FIFO
hardening `6b3014ec0e9a390ca563030b2d7689921f0daef5`,
`55f1ad573e34abf9a0443c34bc5a63d74edba7d7` and
`cb605d48dac95b3b1258c2dcdd2d2c3617bee092`; the Wacom post-`hid_hw_start()`
failure routing `ec2612b8ad9e642596db011dd8b6568ef1edeaa1`; and the LetSketch
timer fix `46c8beeccd8ab2c863827254a85ea877654a3534`. Each is annotated in the
source as `Upstream <commit>` beside the retained baseline line.

The retained native HID-BPF/Rakk ports additionally use Frego commit
`857e71cb0a538b1660743a4267a1e789575f7966` and Rakk commit
`15a0449c955c862045c17f35d9479791981e7812`; the other 16 selected BPF
programs use the baseline above. Huion string-mode glue follows
`huion-switcher` commit `7f63cd48aed5362b073b3c876d98228b3e3a6e90`.

Product status is maintained in the public device tables under
[`docs/hid-host/vendors/`](../docs/hid-host/vendors/), which distinguish
working, partial and unsupported devices and memory limits. Which Linux
drivers are ported is listed in
[`docs/hid-host/linux-drivers.md`](../docs/hid-host/linux-drivers.md), and
device classes deliberately outside the wired-remapper scope are explained in
[`docs/hid-host/README.md`](../docs/hid-host/README.md#what-the-hid-host-supports). Those pages are
maintained for the current working tree; an older source-count or
family-level statement here must not override it.

## Audit Result

The audit classified every active Linux-derived HID and input change as a
missing-subsystem adapter, target memory policy, RTOS ownership
primitive, or explicitly named downstream fix. The inspected active call graph
had no unexplained porting-rule difference or known P0/P1 lifetime, lock-order,
or polling defect. This is a source-conformance and ownership audit, not a
runtime certification.

Current driver selection and device status are maintained in the public
device tables under [`docs/hid-host/vendors/`](../docs/hid-host/vendors/).
Emulator scope and runtime
evidence are maintained in [`hid-emulator-coverage.md`](hid-emulator-coverage.md).
Those files, rather than this audit, are authoritative for product status and
test coverage.

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
- Detached power-supply queues are deleted only when the UI task consumes
  their terminal `REMOVED` snapshot. The host startup path does not establish a
  separate power-consumer readiness contract. The explicit eight-member
  admission bound prevents FreeRTOS QueueSet over-reservation.
- Runtime dynamic quirks, public input grab, second input clients, generic
  Linux logging, `mod_delayed_work()`, and `INIT_DEFERRABLE_WORK()` remain
  compile-gated or dormant. Ordinary delayed work is active for Wacom
  initialization and AES battery expiry and is audited below.
- Terminal interrupt-I/O recovery currently performs full TinyUSB
  teardown/re-enumeration rather than Linux's in-place `usb_reset_device()`.
  A backend-neutral reset/cancel contract is required before a second HCD.
- The compact firmware `usb_host_interface` exposes a deliberately selected
  transport-only view: the first interrupt IN and first interrupt OUT from the
  complete interface stream. TinyUSB retains the physical altsetting, while no
  linked HID caller consumes bulk or duplicate interrupt endpoints through the
  shim. This removes the former first-four-descriptors truncation and reduces
  each snapshot to two endpoint records. A future caller that needs additional
  endpoints must extend this contract.

## Scope

The audit covers the imported Linux HID/input sources, compatibility headers,
host glue, root CMake selection, and generated TinyUSB/Pico-HCD transformations.
It covers the complete imported host-port tree rather than only a single Git
diff. `keyd/port/device.c` was inspected for reachability but is outside the
upstream-conformance result. Source counts are intentionally omitted because
they become stale whenever another driver is imported.

## Supported and Deferred Subsystem Surface

The task-owned port supports descriptor fixups, input mapping hooks, ordinary
`hid_parse()` / `hid_hw_start()` probes, continuous interrupt IN, task-context
raw and FEATURE GET/SET, `hid_hw_request()` / `hid_hw_wait()`, and interrupt
OUT with EP0 SET_REPORT fallback. Device-specific delayed work, timers,
managed inputs, and the selected reduced LED and power-supply adapters use the
ownership contracts documented below.

The following Linux surfaces are not provided as general firmware services:

- generic `usb_submit_urb()` plus request-specific kill/resubmit ownership;
- synchronous interrupt-IN messages separate from continuous HID polling;
- general USB-core sibling-interface coordination beyond the explicit Wacom
  wireless-receiver registry;
- procfs, sysfs, debugfs, uevents, VFS, or a HIDRAW/hiddev userspace ABI;
- the full Linux LED, backlight, power-supply, hwrng, framebuffer, ALSA, or
  raw-MIDI subsystems;
- universal PID force-feedback state.

HIDRAW currently provides only HID lifecycle claim/report/disconnect hooks and
retains no reports because no raw consumer is linked. Keyboard LEDs, the
selected Wacom LED adapter, detached power snapshots, standard HID Haptics,
and the retained Stadia `ff-memless` implementation are narrower explicit
contracts, not implementations of the corresponding full Linux subsystems.
Generic keyboard `EV_LED` remains active: `hidinput_led_worker()` submits the
complete output report through the HID request path, and boot-keyboard start
clears NumLock through that same route. This is independent of Linux's LED
class and does not provide `/sys/class/leds` brightness or vendor RGB panels.

Do not enable a driver which needs one of the absent surfaces merely because
its source compiles. Add the exact task ownership, cancellation, memory, and
consumer contract for a reachable caller first. Product selection and
device-specific exclusions are maintained in the public device tables and in
[`docs/hid-host/README.md`](../docs/hid-host/README.md#what-the-hid-host-supports).

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
this adds no reverse timer/event lock edge. Disabled-source selection and its
reasons are maintained in the public device tables, not duplicated here.

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

The private headers `hid-haptic.h`, `input-core-private.h`, and `wacom_wac.h`
are byte-for-byte identical to the pinned baseline. `hid-ids.h` additionally
carries the named post-baseline Telink/Rakk IDs. The remaining differences of
each Linux-derived source file are commented beside the retained upstream
lines, including the post-baseline fixes listed at the top and the downstream
patch series below. Current source selection is listed in
[`docs/hid-host/linux-drivers.md`](../docs/hid-host/linux-drivers.md).

Scalar time replacements retain the adjacent Linux `ktime_set()`,
`ktime_compare()`, and `ktime_get()` lines. Managed input retains the upstream
allocation, registration, error-label, and two-phase unregister shape; only
final device release connects directly to the reduced device core.

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
- `hid-core`: make field-ordering initialization idempotent by returning when
  `report->field_entries` already exists. The graph belongs to `hid_report` and
  is released by `hid_free_report()`; Wacom receiver rebind reuses the same
  reports and fields across stop/start. This prevents pointer overwrite without
  stop-time allocator churn or a report-producer lifetime dependency. A real
  allocation failure still leaves `NULL` and keeps the existing nonfatal retry
  behavior. The guard was also runtime-validated on Linux 7.2.3.
- `input.c`: pass the UAPI `*_CNT` bit counts to `bitmap_subset()` instead of
  the pinned baseline's last-valid-bit `*_MAX` values, so handler matching does
  not silently omit the final capability bit in each bitmap.

Every replaced baseline statement remains commented beside these fixes. The
local upstream checkout is shallow/grafted, so this audit claims only that the
fixes are absent from the pinned baseline/local refs; it does not invent later
upstream commit IDs.

## Host Task Startup

TUH starts after the async, workqueue, timer, lifecycle, and report runtime has
been created. There is no global READY/FAILED protocol or USB-owner startup
barrier; device ADD events keep the ordinary devmon/KeyD order.

## Selected Driver Contracts

### Vendor allowlists

Microsoft, Apple, and Lenovo keep their complete pinned device tables. The
active rows are the same in the driver table and in
`hid_have_special_driver[]`; excluded Bluetooth, legacy, internal,
force-feedback, and similar rows are compile-gated in both places, so
excluded USB hardware keeps generic fallback. Lenovo's
`hid_have_special_driver[]` keeps pinned Linux's narrower `6009/6047` pair, so
a gated `60ee` interface falls through to generic HID when its report graph
fits.

### Wacom

- 134 fixed USB rows of the pinned `wacom_ids[]` are active: 133 Wacom rows
  plus Lenovo `17ef:6004`, each with its original nonzero `wacom_features_*`.
  The other 17 pinned Wacom USB PIDs stay in their upstream positions behind
  `CONFIG_HID_WACOM_ALL_DEVICES`. Because the final wildcard would otherwise
  claim them, the same PIDs form a firmware-boundary block in `hid_quirks[]`
  with upstream `HID_QUIRK_IGNORE_SPECIAL_DRIVER`, so unchanged matching hands
  them to unchanged `hid-generic`. That block is the canonical
  unfinished-profile list: sixteen need cross-PID sibling topology, and `0094`
  is the hidraw-only bootloader. No Wacom-specific `.match()` callback or
  `hid-generic` control-flow change exists.
- One final wired USB `056a:*` wildcard selects `wacom_features_HID_ANY_ID`
  only for a PID absent from that list. After wildcard parsing, a retained
  INPUT usage equivalent to `WACOM_HID_WD_MODE_CHANGE` rejects the device
  before bind, reported as `HID_IGNORED`. The paired Pen/Touch mode-change
  runtime stays on `wip/wacom-wildcard-mode-change` until a common USB hub
  test exists. Receiver rebind performs exact lookup among active fixed rows
  and can select neither a gated profile nor the wildcard.
- `CONFIG_HID_WACOM_ALL_DEVICES` is not a USB-only completeness switch: it
  restores all upstream rows, compiles out the quirk block, and also enables
  the Bluetooth, I2C, PCI, and broader paths. Bluetooth, I2C, PCI, and the
  broad switch are off in production.
- Exact `056a:0084/0314/0315/0317/0374/5048` set
  `HID_QUIRK_EXPLICIT_FEATURE_USAGES`. Their large VARIABLE Feature reports
  declare one vendor usage that Linux repeats to the report count; the quirk
  materializes callbacks only for explicitly declared usages and keeps wire
  sizes complete. It is a firmware memory optimization, not a Linux fix; on
  the captured `0317` descriptor it removes 626 duplicate usage/priority
  pairs, about 20 KiB. The Wacom feature-mapping pass is bounded by
  `field->maxusage`, so the removed entries have no reachable consumer. The
  probe also recognizes captured descriptor signatures of
  `056a:03ce/03cb/03a6/03ec/03ed/03f5/03f7/03f9/03f0/03c0/03c4/03d0` by exact
  length, terminal `0xc0`, and trailing Feature bytes, and applies the same
  compaction independently of interface number. This is a memory quirk, not a
  binding allowlist; `HID_MAX_USAGES` remains the only retained-storage
  capacity knob.
- Receiver work snapshots PID under the monitor parser lock, releases that
  lock before rebuilding siblings, and lets a later monitor report queue the
  next pass. It synchronously cancels both original sibling `init_work` objects
  before either dynamic resource group is released, clears
  `stylus_in_proximity` and `touch_down` while holding both children's
  `driver_input_lock`, and stops an already started first child if the second
  fails. Ordinary and AES battery callbacks use nonblocking parser-lock retry,
  because firmware power-supply unregister frees directly while physical
  removal holds that lock and cancels work synchronously.
- If the initial receiver-monitor probe queued `init_work` and the later
  `hid_hw_open()` fails, the failure path cancels that callback before devres
  releases `struct wacom`. Reversible `usbhid_start()` checks physical
  disconnect before and after buffer allocation, opens the transport gate only
  for the same live interface generation, and preserves `-ENODEV`/`-ENOMEM`.
- Exact USB ExpressKey Remote `056a:0331` reaches the pinned Remote runtime
  without the broad gate; only the Linux mode/unpair sysfs declarations stay
  gated. Remote ingress and work are task callbacks, so the upstream spinlock
  regions use one heap-backed PI mutex, and Remote work runs on the lifecycle
  owner because it creates and releases input, devres, and power objects. Each
  late serial-keyed input is prepared, opened, and published as an evdev client
  after `input_register_device()`; serial and registered state are published
  under the Remote mutex first. The modulo-u32 millisecond clock uses the
  battery-connected state to distinguish a zero timestamp, and unsigned
  elapsed subtraction stays wrap-safe for the bounded Remote expiry.

### Logitech HID++ and DJ

- A port-only raw-event-only ingress reuses `hid-core` validation and
  `driver_input_lock` but stops before field/input parsing, so
  `hid_device_io_start()` can receive probe replies before final evdev
  activation.
- Battery publication uses the reduced managed `power_supply`:
  `power_supply_changed_work()` overwrites a detached full snapshot in one
  length-one queue per supply. These queues form a separate devmon power
  QueueSet consumed by the UI task, which currently discards `ADDED`/`CHANGED`
  values and deletes the queue after `REMOVED`. The QueueSet admits at most
  eight live or retiring members; a ninth registration returns `-ENOSPC` and
  emits `ERR: POWER_SUPPLY_LIMIT`. HID++ publishes `battery->ps` only after a
  successful registration, so concurrent battery users never observe an
  `ERR_PTR` and a later receiver-child event may retry. The HID++ unit ID stays
  in Linux HID/input state and does not cross `port_input_dev`.
- Generic fallback may publish the physical interfaces of the gated Nano
  receivers `046d:c52f/c534`, but runs no DJ/HID++ startup and creates no
  virtual children.

### Input, devres, and evdev identity

- `devm_release_action()` is `void` as in pinned Linux, and `devres_destroy()`
  returns `0/-ENOENT` and destroys without release. Managed input allocation
  and registration use two `struct input_devres` records in upstream order:
  unused inputs remove their allocation record before the final
  `input_put_device()`, and registered inputs release the unregister record
  first. The reduced device refcount and final `input_dev_release()` own the
  port mutex and embedded devres cleanup.
- The evdev identity path reads `input_dev->id.vendor/product`, as Linux evdev
  does, and treats input driver data as opaque.
- CMake follows the pinned aggregate order `wacom_wac.c`, then `wacom_sys.c`.

### Force feedback and HID Haptics

- Standard HID Haptics uses `input_ff_create()` with five firmware-owned
  effect slots and device-managed timing. Stadia and `ff-memless` stay unlinked
  until a product `FF_RUMBLE` client exists; restored together, Stadia uses the
  upstream 16-slot memless policy and timer path.
- Evdev publication and the first `hid_hw_open()` happen only after
  `hid_add_device()` returns from the complete multitouch/haptic probe, so the
  final `EV_FF`/`FF_HAPTIC` state is atomic at the firmware client boundary.
- The evdev client exports only a `has_haptic` snapshot
  (`ff && test_bit(FF_HAPTIC, ffbit)`) taken after the complete probe. A
  future generic force-feedback client must extend that snapshot to the Linux
  `ffbit` capabilities it needs instead of identifying devices by VID/PID;
  `FF_RUMBLE` and `FF_HAPTIC` are different effect types. An uploaded effect
  ID is valid only for one input-device lifetime and must be uploaded again
  with `id = -1` after reconnect. For `ff-memless`, a nonzero play value is the
  repeat count, and a successful writer call does not mean USB completion.
- ff-core retains an old definition during replacement upload and an erased
  definition while the driver callback runs. `hid-haptic` uses this to decide
  HOST/DEVICE ownership, cancels a slot's pending PLAY before rewriting it, and
  drains effect/STOP work before teardown.

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
| `down_trylock()` in `hid-core.c` | one nonblocking semaphore attempt | Exact upstream parser gate, with no retry loop. |
| direct HID++ response | one send-mutex-serialized waiter, durable response predicate, notification index 1, and absolute five-second attempt deadline | Register-before-test is preserved; report-task response and exact-interface disconnect are durable wake conditions. No callback waits or report-time allocation. This reduced wait contract is valid only for the linked single waiter. |
| post-abort HCD retirement | one global EP0 cancel transaction plus exact cancel-time FIFO prefix; one-shot non-control fence | No tick/SOF/stage-count polling. Current proof relies on PIO alarm IRQ and HCD abort sharing CORE1; ESP/DWC2 needs an explicit backend cancel-completion edge. |

Queue receives with zero timeout are bounded drains, not mutex acquisition
loops. No lifecycle/workqueue/timer compatibility lock masks interrupts. The
generated-host critical section is limited to the ISR/task event handoff: a
zero-time queue operation, counters, or one event copy, with no callback,
logging, allocation, or wait.

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
- Upstream `hid_ctrl()` parses every successful control GET in its callback;
  parsing does not depend on the caller invoking `hid_hw_wait()`. This port
  sends ordinary completion to the report task, including a GET queued by that
  task from `raw_event`. Only when the lifecycle task queues a GET while it
  owns `driver_input_lock` does the request record a `parser_owner` and publish
  its buffer directly to that lifecycle owner. An inner `hid_hw_wait()` can
  consume it with the lock retained; the outer activation fence can consume it
  after probe releases the lock, and glue selects the matching parser entry.
  This avoids a firmware-only queue handoff without opening interrupt input on
  a half-built device. The existing per-interface wait head carries the
  corresponding upstream-style wake edge; the broader firmware `io_pending`
  lease remains the durable predicate. `hid_hw_wait()` only observes parser and
  FIFO drain and no longer needs one-tick polling.
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
  HID EP0 GET/SET build the class SETUP themselves and call
  `tuh_control_xfer()` with the request serial as `user_data`. The pinned
  TinyUSB `tuh_hid_get_report()`/`tuh_hid_set_report()` helpers are not used:
  their completions report only `wLength` or zero, which would lose the real
  result, actual length, and serial.
- Runtime HID paths perform no flash writes, and every host queue has a fixed
  bound; no queue grows without limit.
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
  logger from the callback.
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

## Open Semantic Boundaries

- The parser deliberately differs from Linux when a field declares more than
  `HID_MAX_USAGES` values.
  The full declared count remains represented in `report->size` up to the fixed
  `HID_MAX_REPORT_COUNT=12288` wire ceiling, while `field->report_count` and
  its cached `value`/`new_value` arrays retain only the configured prefix. The
  production limit is `HID_MAX_USAGES=675`.
  Non-padding INPUT ARRAY fields above that retention limit are rejected
  because omitting physical slots would change key-state semantics; other
  fields retain a bounded prefix. A structured
  `hid_hw_request(..., HID_REQ_SET_REPORT)` would make `hid_output_report()`
  zero-fill the omitted Output/Feature tail, so every future driver or exact
  device with an oversized writable report must audit that call path. A raw
  request with a complete caller-owned buffer is unaffected. The current
  `0374` call graph never writes its oversized `0xd9` Feature report and sends
  its small mode report through the raw Wacom helper. Complete retained-value
  coverage for this descriptor requires `HID_MAX_USAGES >= 1280` and more heap
  than the RP2040 target provides. The production default remains 675.
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
  receive allocation per queued GET.
- Upstream `hid-haptic` built manual-trigger output at byte zero even though
  `hid_alloc_report_buf()` reserves that byte for an absent report ID when the
  report is unnumbered. The adjacent firmware replacement shifts only that
  payload and counts the reserved byte exactly as `__hid_request()` does.
  Numbered reports are unchanged; unnumbered EP0 fallback and interrupt OUT
  remove the zero transport byte before sending the complete payload.
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
- The kernel BPF VM/loader remains absent. The native adapter source and active
  declarations remain adjacent as comments; exact upstream `!CONFIG_HID_BPF`
  stubs preserve the ordinary HID call graph. The adapter is implemented but
  default-off; the enable steps are in the HID-BPF section of
  [`docs/hid-host/README.md`](../docs/hid-host/README.md#hid-bpf-programs).
  When the adapter and a retained program source are
  restored, descriptor/event and lifecycle hooks dispatch to that static native
  upstream program. The descriptor-rescan branch then
  becomes reachable and propagates
  `hid_set_group()`'s firmware `-ENOMEM` result.
  `hid_close_report()` is exposed to the adapter for ACK05's pre-input-connect
  fallback to the original descriptor. These two core adaptations retain their
  original lines and reasons. Raw-request and output-interception
  hooks remain no-ops because no retained program implements them; helpers for
  unsupported BPF operations are not exposed. Dynamic program loading or
  runtime driver mutation is not implemented.
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
- The timer bridge rejects a shutdown timer whose callback is `NULL` while
  holding the same mutex used by `timer_shutdown_sync()`. This mirrors Linux's
  permanent shutdown state and prevents a concurrent rearm from relinking the
  timer. A zero-delay self-rearming callback could still monopolize the single
  timer task; all active callbacks rearm in the future.
- Vendored Pico-PIO-USB still has its documented abort busy-wait. Its dormant
  `pio_usb_host_stop()`/`pio_usb_host_restart()` APIs also wait on flags with no
  active clearing owner, but no linked firmware path calls them. Both belong to
  a future backend cancel/quiesce-completion contract; the active TinyUSB/Linux
  glue must not compensate with polling.
- Linux runtime PM, autosuspend, remote-wakeup policy, and in-place usbcore
  reset are not present. The lifecycle coordinator deliberately performs a
  stronger teardown/re-enumeration because pinned TinyUSB cannot restore class
  state in place; this costs continuity but leaves no half-restored HID graph.
  This boundary does not remove HID SET_IDLE: probe-time SET_IDLE and the
  ll-driver `.idle` EP0 implementation are present. The only imported
  driver-level `hid_hw_idle()` caller is `hid-multitouch::mt_resume()`, which is
  unreachable without the missing PM dispatcher. Imported generic, ALPS, ASUS,
  Lenovo, Logitech DJ, multitouch, UC-Logic, and Wacom PM callbacks remain
  dormant for the same reason; the Sony controller block is separately gated.
  Any future sleep/resume stage must add host-backend quiesce, wake policy,
  report-producer fencing, and ordered driver callback dispatch together rather
  than defining `CONFIG_PM` in isolation.
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
  ID gates and failed-probe free, and immutable driver registration; params
  differs only at that private include, the single allocate/copy/free
  replacement for upstream `krealloc()`, and a firmware optional-product-string
  guard. The original
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
- Pinned Artist initialization returns `-EINVAL` on each non-Pen interface.
  The reduced synchronous device model retains only `-ENOMEM` while trying
  registered drivers and returns `-ENODEV` when none binds, so the reachable
  firmware result is the existing `HID_IGNORED` topology warning. This is a
  device-model boundary, not a driver-local rewrite of the Linux return.
- `uclogic_params_get_desc()` returns a separately allocated combined
  descriptor through `drvdata->desc_ptr`. `hid_open_report()` copies it before
  parsing, so HID core never owns that original allocation. Pinned Linux frees
  it from `uclogic_remove()` after successful probe but omitted it from the
  failed-probe label. The port retains the original cleanup beside a direct
  `kfree(drvdata->desc_ptr)` under the existing `params_initialized` branch;
  return values and successful remove order are unchanged. The reached
  `hid_hw_start()` and `hid_parse()` failures use the same ownership order.
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
  implementation is unchanged by this port.
- The KeyD queue adapter is firmware glue; no pinned upstream-KeyD comparison
  is claimed.
- `hid_register_field()` retains upstream's single-allocation layout while the
  adjacent port replacement sizes mapping/priority and cached-value regions
  independently. Both are bounded by `HID_MAX_USAGES`: INPUT ARRAY fields keep
  every accepted physical slot while their selector mappings may be longer;
  scoped compact Features may retain fewer mappings than cached values. The
  complete declared wire width remains in `report->size`. The original
  signature, allocation, pointer arithmetic, and call remain commented beside
  the RP2040 memory-bounded replacement.
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
  current object further to 616 B without truncating the valid 0..255 ID range.
- Plain `kobject_uevent()` calls retain lifecycle counters but do not build a
  2.3 KiB environment: firmware has no userspace/netlink sink for it.
  `kobject_uevent_env()` remains available if a real environment consumer is
  added later.
