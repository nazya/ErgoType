# Upstream Porting Audit

Porting rules source:
`./usb_host/upstream-porting-rules.md`.

Linux upstream source used for this audit:
`../linux-upstream-hid` at `83f14548`.

Important correction: `.` is treated only
as the previous porting worktree, not as Linux upstream.

## Scope Audited

- current TinyUSB HID host glue: `usb_host/task.c`, `usb_host/usbhid.c`,
  `usb_host/hid_async.c`, `usb_host/hid_workqueue.c`, `usb_host/hid_timer.c`
- current HID/input boundary glue: `usb_host/evdev.c`,
  `keyd/port/device.c`
- active Linux HID/input core files in CMake
- active lightweight vendor HID drivers in CMake
- active Linux compatibility headers used by the HID slice

The local Linux upstream working tree is sparse but now materializes the input
files needed for ordinary file-based comparison:

- `../linux-upstream-hid/drivers/input/input.c`
- `../linux-upstream-hid/drivers/input/evdev.c`

## Fixed During Audit

- `hid-core.c`: documented the port-only `hid_match_one_id()` export.
- `hid-input.c`: removed non-port diffs against Linux upstream: duplicate
  comments around active `input_mapping`/`input_mapped`, an extra context
  comment, an indentation-only comment change, and unnecessary braces.
- `hid-generic.c`: documented the const driver pointer conversion and unused
  `id` in generic probe.
- `hid-quirks.c`: restored the full upstream include block as commented
  context before the local compatibility include.
- `hid.h`: marked the file as a reduced compatibility contract, not a
  line-preserving upstream header.
- `hid_slice_stubs.c`: removed. Deferred hidraw calls are documented inline in
  `hid-core.c`; workqueue and timer boundaries are now owned by
  `hid_workqueue.c` and `hid_timer.c`.
- `evdev.c`: renamed the firmware input handler boundary to upstream-like
  `evdev_*` names. This is not a line-preserving upstream evdev port; it keeps
  the input_handler shape and replaces userspace fd delivery with KeyD queue
  delivery.
- `keyd/port/device.c`: owns the firmware queue replacement for upstream
  keyd's evdev fd path while still using the existing `devmon_queue`.
- `usbhid.c`: added/expanded explicit upstream usbhid lifecycle anchors around
  parse/request/wait/raw/output/idle boundaries and TinyUSB pre-probe glue.
- `hid-drivers.c`: clarified that driver safety selection is the CMake
  allowlist, while the registration loop stays Linux-shaped.
- `task.c`: documented the explicit firmware call to collected Linux-style
  module/initcall registrations before HID drivers bind.
- `hid_port.c`: removed after evdev became the always-open firmware event
  producer.
- `input.c`: removed stale wording that still referenced the old HID driver
  task/workqueue pump, documented the removed Work3 executor wrappers, and
  added a top-level note that this is a reduced input core slice.
- `input.h`: documented that the Work3 proxy ABI declarations are deferred and
  are not upstream Linux input definitions.
- `workqueue.h`: documented that Work3 workqueue entry points are not exposed
  in this callback-driven slice.

## Vendor Drivers

All included lightweight vendor drivers were compared against real Linux
upstream under `../linux-upstream-hid/drivers/hid`.

The intended diff for each included vendor driver is:

- upstream `static struct hid_driver ...` kept as a commented line
- port-specific `static const struct hid_driver ...`
- short reason: imported HID driver descriptors live in flash/rodata

`hid-lcpower.c` has one additional whitespace-only diff: upstream has trailing
space in the copyright line, while the port keeps `git diff --check` clean.

The current CMake allowlist is the source of truth for linked vendor drivers.
The allowlist now includes simple mapping/fixup/probe drivers plus a small set
of audited nonblocking hooks such as `input_configured` users and queue-only
SET_REPORT users. Drivers that need returned request data before probe can
continue stay out of CMake until a larger worker/state-machine layer exists.

## Core HID Status

Conforms to the current callback-driven slice intent:

- HID parser/report/input flow remains Linux-shaped.
- `report_fixup`, `input_mapping`, and `input_mapped` are active for the
  included nonblocking vendor drivers.
- Some `raw_event`, FF, and hiddev paths are now enabled only through audited
  nonblocking slices. Synchronous hardware request paths remain disabled or
  converted to explicit async boundaries with upstream call sites visible next
  to the port replacement.
- TinyUSB report callbacks either enter the Linux report parser directly for
  callback-safe paths or enqueue report delivery to the HID async task.
- Unsupported final firmware events stop at the input/keyd boundary rather than
  being collapsed earlier in HID parsing.
- `hid-input.c` keeps upstream `EV_LED -> schedule_work(&hid->led_work)`.
  The firmware workqueue bridge runs the worker outside TinyUSB callbacks, and
  the TinyUSB ll_driver queues output through the HID async request path.
- Compat mutex/semaphore/spinlock APIs are no-op/commented where they used to
  allocate or block. Short critical sections remain only in local compat
  atomic/bitop helpers, not as Linux driver locks or wait points.

## Input Core Status

Compared against real Linux upstream files/blobs:

- `../linux-upstream-hid/drivers/input/input.c`
- `../linux-upstream-hid/drivers/input/evdev.c`
- `HEAD:drivers/input/input-mt.c`
- `HEAD:drivers/input/ff-core.c`
- `../linux-upstream-hid/drivers/input/ff-memless.c`
- `../linux-upstream-hid/drivers/hid/hid-google-stadiaff.c`

Result:

- `input-mt.c` is upstream-shaped. Active diffs are local include paths and
  commented lockdep/guard replacements at FreeRTOS event-lock boundary points.
- `ff-core.c` is upstream-shaped. Active diffs replace Linux `guard()` /
  `scoped_guard()` helpers with direct statements, with upstream lines left
  visible.
- `ff-memless.c` is upstream-shaped. Active diffs replace Linux `guard()`
  event-lock helpers with direct statements, with upstream lines left visible;
  the file itself does not submit HID requests and calls the driver
  `play_effect()` callback.
- `hid-google-stadiaff.c` is upstream-shaped. Active diff is the same imported
  driver descriptor conversion used by the other linked vendor drivers:
  upstream `static struct hid_driver` remains visible next to the port
  `static const struct hid_driver`.
- `input.c` is not line-preserving upstream. It is a reduced in-memory input
  core slice: Linux char device/procfs/sysfs/IDA/RCU/poller/userspace
  machinery is not present, while the HID input event batching/handler path
  needed for KeyD is active. Its function order was moved closer to upstream so
  future review noise is lower, but the file remains a porting-rule exception:
  the omitted Linux subsystem blocks are not preserved as full commented
  upstream bodies.
- `evdev.c` is not line-preserving upstream. It preserves upstream names for
  `evdev_handler`, `evdev_connect`, `evdev_events`, `evdev_disconnect`, and
  `evdev_init`, but the userspace fd/client code is replaced by the firmware
  KeyD queue boundary.

## Header Status

`usb_host/linux/include/linux/hid.h`, `input.h`, `workqueue.h`, `usb.h`,
`hidraw.h`, and `timer.h` were compared against real Linux upstream headers.

These headers are reduced compatibility contracts, not line-preserving upstream
ports. That is now explicit in `hid.h`, `input.h`, and `workqueue.h`. The active
C files remain the main upstream-preservation target.

## Worktree-Only Deferred Areas

These existed in Work3 but are not real Linux upstream paths and are not active
in this branch:

- `input_port_proxy_hid_usage_event()` and related proxy/debug taps before the
  final input boundary
- Work3 `hid_compat_exec_*` executor
- Work3 firmware workqueue bridge
- Work3 raw/proxy device-report ABI

## KeyD Port Boundary Notes

- `pointing/pointer.c:send_pointing_event()` currently emits Linux-shaped
  `EV_REL` events plus `EV_SYN` directly for local PMW devices. This works as a
  temporary bridge into the new KeyD device queue format, but it needs
  architectural cleanup later. This is already the KeyD firmware port boundary,
  not Linux HID upstream code, so the future fix should be guided by the KeyD
  port architecture rather than by preserving Linux HID diffs.

## Explicit Deferred Linux Areas

These are not enabled by the current slice and should not be silently enabled:

- `hidraw` active buffering/proxy APIs.
- synchronous `hid_hw_request()` / `hid_hw_raw_request()` / `hid_hw_wait()`
  semantics that do not have an explicit async continuation.
- unaudited `raw_event` and driver `report` callbacks.
- `input_configured` or `feature_mapping` paths that need returned request data
  before continuing probe.
- FF/PIDFF drivers that need PID state or synchronous waits.
- Linux hiddev fd/ioctl/poll compatibility beyond the bounded firmware proxy.

## Checks Run

- `cmake --build build -j4`
- `git diff --check`
- comparison of active HID core/vendor files against
  `../linux-upstream-hid/drivers/hid`
- comparison of active input files against real upstream files under
  `../linux-upstream-hid/drivers/input`
- comparison of active Linux headers against
  `../linux-upstream-hid/include`
- comparison of current `usb_host/linux` against previous worktree
  `./usb_host/linux`
- grep over included vendor drivers for blocked async/request/work hooks
