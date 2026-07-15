# HID Host Bring-up and Limits

This is the final short record of the HID host regression investigation. It
replaces the old experiment logs and describes the current design, RAM limits,
diagnostics, and remaining risks.

## Current Status

The following paths have produced input events on hardware:

- a generic USB keyboard, including the keyboard interface of a composite
  ErgoType device;
- the Holtek keyboard emulator, including its report-descriptor fixup;
- the PMW pointing-device path, independently of USB host input.

Working firmware runs before attaching a heavy HID device have reported roughly
45-48 KiB of free FreeRTOS heap. That is an observed operating margin, not a
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
  -> device/string descriptor pre-probe
  -> Linux HID report parse
  -> synchronous driver match and probe
  -> hidinput open, then TinyUSB interrupt-IN receive starts
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

## What `HID_REPORT_SKIP` Meant

`ERR: HID_REPORT_SKIP` was a late receive-path symptom, not the original parser
error. It means either no active `hid_device` slot matched the TinyUSB interface
or `hid_safe_input_report()` rejected the report. Earlier code could hide a
probe failure, arm receive anyway, and turn every later key report into this
same message.

The bind/error propagation and RX lifecycle above remove that known failure
chain. If `HID_REPORT_SKIP` appears now, inspect the mount, parse, bind, and
unmount messages immediately before it instead of changing key mappings.

## Why Wide Usage Ranges Exhaust RAM

The large allocations are not a single global key-code lookup table. Linux HID
materializes usage metadata separately for every parsed report field, and that
field remains allocated for the lifetime of the connected interface.

On the current RP2040 ABI:

```text
sizeof(struct hid_field) = 100 B
each retained usage      = 40 B
field allocation         = 100 + usage_count * 40 B
```

Examples:

| Usages | Requested persistent allocation | Relevant descriptor |
| ---: | ---: | --- |
| 675 | 27,100 B (26.5 KiB) | Current bounded field |
| 1,024 | 41,060 B (40.1 KiB) | ErgoType Consumer range `0x000..0x3ff` before the cap |
| 12,288 | 491,620 B (480.1 KiB) | Holtek fixed range `0x000..0x2fff` before the cap |

The parser also keeps three temporary arrays costing 9 B per local usage. At
675 entries they require 6,075 B and are freed after parsing. With the former
12,288 limit a one-shot reserve would require 110,592 B before the persistent
field could even request 491,620 B. The older incremental 2,048-to-4,096 growth
temporarily needed both allocations at once, or 55,296 B.

These are allocation requests. If a persistent field allocation fails, the
current `hid_add_field()` path can omit that field without returning `-ENOMEM`.
Missing reports under heap pressure therefore do not necessarily come with a
direct allocation error.

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
build. The current 16 runtime records occupy 896 B of static `.bss`. Static
`.bss` still consumes physical RAM, but it no longer depletes or fragments the
runtime heap. Excluding a driver from CMake also excludes its runtime record
while leaving its source in the repository.

The current allowlist is `hid-generic` plus A4Tech, Apple IR, Chicony, Creative
SB0540, Cypress, Google Stadia FF, Holtek keyboard, ITE, Kye, Primax, PXRC,
Rapoo, Razer, Saitek, and Zydacron.

Current memory-related settings are:

```text
configTOTAL_HEAP_SIZE       217 KiB
configMINIMAL_STACK_SIZE    384 words
hid_async_task              512 words
keyd_task                   7,168 words
PIO_USB_EP_POOL_CNT         8
PIO_USB_DEVICE_CNT          4
PIO_USB_ROOT_PORT_CNT       1
```

The workqueue, timer, and disconnect split still costs three tasks. Their
384-word stacks plus task overhead consumed about 6.6 KiB in the measured
build. Consolidating them is a later architecture change, not part of this
bring-up fix.

## Log Reference

| Message | Meaning |
| --- | --- |
| `DBG: USB_MOUNT_CB` | TinyUSB saw a physical USB device. |
| `DBG: HID_MOUNT_CB` | TinyUSB exposed one HID interface. |
| `WARN: HID_IGNORED` | No linked driver accepted this HID interface; other composite interfaces are unaffected. |
| `ERR: HID_ADD_FAIL` | `hid_add_device()` failed for an error other than `-ENODEV`, such as parse, registration, or start failure. |
| `ERR: HID_REPORT_SKIP` | No live HID slot matched, or Linux input parsing rejected the received report. |
| `ERR: HID_RX_START_FAIL` | Initial interrupt-IN receive could not be armed after bind/open. |
| `ERR: HID_RX_REARM_FAIL` | Receive could not be armed again after a report callback. |
| `ERR: HID_ASYNC_CANCEL_FAIL` | Pending async HID requests could not be cancelled during detach. |
| `WARN: HID_USAGE_CAP_DROP` | A report used an array selector outside the retained 675-entry field lookup. |
| `DBG: EVDEV_KEY_Q` | A key event reached the evdev-to-KeyD queue. |

No `USB_MOUNT_CB` or `HID_MOUNT_CB` after insertion points first to power,
wiring, D+/D-, the breakout, or the connector. Parser and KeyD changes cannot
fix a connection that never reaches TinyUSB.

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

The Google Stadia FF driver is linked; generic PID, haptic, Holtek FF, and the
other optional FF families remain disabled. The workqueue cancellation UAF is
fixed, but the Linux synchronization and object-lifetime model is not complete:

- the current Linux mutex/spinlock compatibility operations do not provide the
  full Linux locking semantics;
- putting all HID tasks on one core would not remove task-preemption races;
- Stadia play can observe `removed == false`, race past remove-time
  `cancel_work_sync()`, and enqueue new work before the device is freed because
  the imported spinlock does not currently serialize those paths;
- `evdev_client` ownership across `DEVICE_INPUT_REMOVED` is still unresolved:
  KeyD can retain the pointer used for FF while disconnect teardown frees it.

Do not describe FF detach as safe or enable more FF drivers until
`evdev_client` removal ownership and the required targeted synchronization are
implemented. The intended ownership direction discussed during the audit is
for disconnect to enqueue removal and for the KeyD side to release the client
after it has processed that event; no such change is implemented yet.

For the current Stadia path, the minimum stress test is repeated rumble followed
by immediate unplug/replug, checking for stale work, hard faults, hangs, and
reports sent through a removed device. The cancellation patch passes the build;
this exact post-fix hardware stress test is still pending.

## Related Notes

- [`pio-usb-memory.md`](pio-usb-memory.md): static RAM and Pico-PIO-USB pools.
- [`async-hid-requests.md`](async-hid-requests.md): nonblocking TinyUSB request model.
- [`async-hid-progress.md`](async-hid-progress.md): implemented async-driver coverage.
- [`deferred-hid-drivers.md`](deferred-hid-drivers.md): drivers intentionally left out.
- [`hid-emulator-coverage.md`](hid-emulator-coverage.md): coverage history; use
  CMake, not this older matrix, as the current driver allowlist.
- [`upstream-porting-rules.md`](upstream-porting-rules.md): rules for Linux-port diffs.
- [`upstream-porting-audit.md`](upstream-porting-audit.md): remaining port deviations.
