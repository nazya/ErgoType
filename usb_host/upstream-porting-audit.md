# Upstream Porting Audit

Porting rules source:
`./usb_host/upstream-porting-rules.md`.

Linux upstream source used for this audit:
`../linux-upstream-hid` at `83f14548`.

Important correction: `.` is treated only
as the previous porting worktree, not as Linux upstream.

## Scope Audited

- current TinyUSB HID host glue: `usb_host/task.c`, `usb_host/tuh_ll_driver.c`
- current HID/input boundary glue: `usb_host/hid_port.c`, `usb_host/evdev.c`,
  `usb_host/evdev_queue.c`
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
- `hid_slice_stubs.c`: added explicit local-shim comments for unwired hidraw,
  work cancellation, and timer boundaries.
- `evdev.c`: renamed the firmware input handler boundary to upstream-like
  `evdev_*` names. This is not a line-preserving upstream evdev port; it keeps
  the input_handler shape and replaces userspace fd delivery with KeyD queue
  delivery.
- `evdev_queue.c`: split the firmware queue boundary from the input_handler
  side. It replaces upstream evdev per-client fd buffers with a KeyD queue.
- `tuh_ll_driver.c`: added/expanded explicit upstream usbhid lifecycle anchors
  around parse/request/wait/raw/output/idle no-op boundaries.
- `hid-drivers.c`: clarified that driver safety selection is the CMake
  allowlist, while the registration loop stays Linux-shaped.
- `task.c`: added an `Upstream Linux: no equivalent` comment for the TinyUSB
  host callback/lifetime glue.
- `hid_port.c`: marked the debug capability trace helper as firmware boundary
  glue.
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

Included driver hooks are limited to:

- `report_fixup`
- `input_mapping`
- simple `probe`

Checked with grep: no included vendor driver contains active `raw_event`,
`.event`, `.report`, `input_configured`, `remove`, FF init, workqueue,
delayed work, wait, `hid_hw_request`, `hid_hw_raw_request`,
`hid_hw_wait`, or USB control-message calls.

## Core HID Status

Conforms to the current callback-driven slice intent:

- HID parser/report/input flow remains Linux-shaped.
- `report_fixup`, `input_mapping`, and `input_mapped` are active for the
  included nonblocking vendor drivers.
- `raw_event`, driver `report`, `input_configured`, FF, hiddev, and synchronous
  hardware request paths remain disabled with upstream call sites visible next
  to the no-op/disabled boundary.
- TinyUSB report callback still enters Linux report parsing directly.
- Unsupported final firmware events stop at the input/keyd boundary rather than
  being collapsed earlier in HID parsing.
- `hid-input.c` still contains the upstream LED worker body, including the raw
  SET_REPORT fallback. The active `schedule_work(&hid->led_work)` call is
  commented out, and the current TinyUSB ll_driver has a no-op `.request`, so
  the callback-driven report path does not enter synchronous LED/output I/O.
- Compat mutex/semaphore/spinlock APIs are no-op/commented where they used to
  allocate or block. Short critical sections remain only in local compat
  atomic/bitop helpers, not as Linux driver locks or wait points.

## Input Core Status

Compared against real Linux upstream files/blobs:

- `../linux-upstream-hid/drivers/input/input.c`
- `../linux-upstream-hid/drivers/input/evdev.c`
- `HEAD:drivers/input/input-mt.c`
- `HEAD:drivers/input/ff-core.c`

Result:

- `input-mt.c` is upstream-shaped. Active diffs are local include paths and
  commented lockdep/guard replacements at FreeRTOS event-lock boundary points.
- `ff-core.c` is upstream-shaped. Active diffs replace Linux `guard()` /
  `scoped_guard()` helpers with explicit mutex and `input_port_event_lock()`
  calls, with upstream lines left visible.
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
- `hid_hw_request()` / `hid_hw_raw_request()` / `hid_hw_wait()` semantics.
- `raw_event` and driver `report` callbacks.
- `input_configured` and `feature_mapping`; `hid-vivaldi` is excluded because
  its feature mapping performs `hid_hw_raw_request(GET_REPORT)`.
- FF/PIDFF and workqueue/delayed-work based drivers.
- HIDDEV/userspace compatibility.

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
