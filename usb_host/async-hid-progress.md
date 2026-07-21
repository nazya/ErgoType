# Async HID Progress

## Current Scope

- `usb_host/usbhid.c` owns a bounded USB-device cache and fixed probe-identity
  slots allocated before the TinyUSB host starts. Its bounded
  application-driver ingest copies the ephemeral raw configuration stream;
  mount/unmount callbacks do not allocate, log, interpret that retained
  snapshot, or wait for USB/lifecycle progress. Their bounded publication may
  briefly take the shared transport mutex. HID mount captures only TinyUSB's
  instance-to-interface identity
  and wakes the lifecycle task. Global mount also consumes
  the already published cache epoch and leaves device/string descriptor policy
  to lifecycle. Lifecycle calls the ordinary async-backed `usb_control_msg()`
  and may sleep; the executor and TinyUSB callbacks only transport/complete the
  standard request. After shared device/string pre-probe, lifecycle constructs
  the retained USB-interface shim and Linux's `usbhid_parse()` validates the
  HID descriptor in that task context. Reset recovery consumes the same durable
  `mount_complete` predicate rather than advancing its state in the callback.
- HID interface mount no longer has to guess whether USB strings or
  `bcdDevice` are ready. After pre-probe, lifecycle enters `usbhid_probe()`;
  its synchronous Linux parse callback allocates the exact class-declared
  buffer, fetches it through the asynchronous EP0 owner, parses it, and frees
  it before returning. Report descriptors are therefore supported through
  Linux's 4 KiB `HID_MAX_DESCRIPTOR_SIZE`, independently of TinyUSB's 512-byte
  enumeration scratch buffer.
- TinyUSB full configuration descriptors keep the permanent 512-byte fast
  path, while a validated `wTotalLength` from 513 through 4096 bytes is fetched
  into one exact-size transient host-owner buffer. The short-descriptor
  completion records only durable pending state; allocation and the full GET
  happen at the next shallow host-task entry after the callback has unwound.
  The buffer survives enumeration-control retries and is freed after the
  synchronous class-open scan or after terminal EP0 drain on every failure,
  timeout, or remove path. Its companion emulator now provides a dedicated
  600-byte configuration whose useful HID interface begins at byte 575 and
  stalls the first full GET to force the same retry path.
- The pre-probe sequence currently fetches the device descriptor, LANGID,
  product string, manufacturer string, and serial string. That makes
  `hid->version`, `hid->name`, and `hid->uniq` available before
  `hid_ignore()`, `hid_lookup_quirk()`, and driver probe. Lifecycle performs at
  most one descriptor wire operation before returning through top-level
  disconnect/reset handling. Required device-descriptor admission retries
  without consuming a wire attempt; optional strings remain best-effort and a
  persistently full local slot pool cannot prevent an otherwise usable probe.
- The raw interface snapshot preserves USB's HID subclass and interface
  protocol (none/keyboard/mouse) before TinyUSB reconstructs its class-facing
  view. That is functional metadata: it feeds Linux-like `hid->type` decisions
  such as the Razer mouse interface path. The probe token now carries that
  snapshot's interface number through probe, so task-side `usbhid_probe()`
  builds its USB shims only from lifecycle-owned device/interface state instead
  of rereading live TinyUSB VID/PID and interface tables. Mount publishes an
  immutable identity record through a host-owned serial; lifecycle acknowledges
  that serial after consuming its probe attempt (or a terminal shared
  pre-probe failure). HID unmount revokes only the host serial before TinyUSB
  clears its class slot, so an old probe can neither validate nor acknowledge a
  fast-replug record. There is no callback/lifecycle `FREE/PENDING/ACTIVE` state
  enum, and probe liveness no longer polls the host-owned `tuh_hid_mounted()`
  table. Report completion publishes only transfer metadata; the report task
  applies open/stopping/recovery policy under the exact interface-generation
  fence instead of rereading TinyUSB's class slot in the callback. Linux relies
  on the USB reset-default Report protocol, so selected boot/report mode is not
  retained as port state and no generic SET_PROTOCOL request is emitted.
- `usb_host/hid_async.c` owns serialized TinyUSB host submits for HID control
  and interrupt-output requests. TinyUSB callbacks only publish completions and
  never run Linux driver continuations directly. Device/string pre-probe has no
  private request kind, FIFO, scratch, or completion continuation in this layer;
  it uses the same generic device-control path as task-side Linux USB calls.
- TinyUSB mount/unmount/transfer callbacks execute in the dedicated host task,
  not in hard IRQ context. The async, lifecycle/cache, and report modules now
  preserve their existing common state domain with one explicit FreeRTOS
  transport mutex instead of 163 application-level `taskENTER_CRITICAL()` entry
  sites. That stops fixed-pool scans and report-state transitions from holding
  FreeRTOS's scheduler-wide SMP lock with local IRQs masked. No mutex scope
  crosses a TinyUSB/HCD call, host-task handoff, parser, blocking wait, logging,
  or heap operation. The migrated scopes do not nest, so a normal
  priority-inheritance mutex exposes accidental recursion. Fixed callback
  slots and durable predicates already hand work to the async, report, and
  lifecycle tasks. The remaining common domain couples generation/lifetime
  with recovery admission; splitting it requires a complete owner and lock-order
  design rather than more heap-backed mutexes.
  The task-only workqueue bridge is now a separate execution domain with its
  own priority-inheritance mutex. Durable FIFO/flags are the condition, direct
  task notifications are wake edges, and stack-owned waiters preserve
  `flush_work()`, `cancel_work_sync()`, and `destroy_workqueue()` without the
  former 15 critical regions or one-tick polling. The timer bridge is now a
  second task-only execution domain with its own priority-inheritance mutex.
  TinyUSB unmount publishes stopping and wakes the report task; that owner
  claims and synchronously cancels an I/O-retry timer under an `io_pending`
  lifetime lease. Direct notifications replace the timer's one-entry wake
  queue, and stack-owned waiters replace `timer_delete_sync()` polling. Neither
  domain is mixed into the transport mutex or masks scheduler-wide interrupts.
  The live workqueue parents are KeyD, the dedicated HID timer/lifecycle tasks,
  and the worker itself; TinyUSB callbacks only publish state to those owners.
  FreeRTOS calls and invariant predicates are evaluated before any
  `configASSERT()`, so Release builds cannot erase a mutex operation with the
  assert expression. Rare task-side invariant failures use fixed-size async
  diagnostics before asserting an already computed boolean; diagnostics found
  under the workqueue/timer mutex are emitted only after it is released.
  Transport-mutex failure is special because the helper is also entered by
  TinyUSB host callbacks: it publishes one fixed bit through lifecycle's
  existing indexed task notification, and lifecycle alone calls the logger.
  The callback error path therefore does not recurse through the mutex, enter
  the device-side deferred-log queue, or invoke a C11 read-modify-write helper
  that would hide an RP2040 IRQ-masking spinlock.
  The same no-expression assertion rule is now enforced across the async
  executor, USB HID lifecycle, interrupt-report owner, and transport mutex:
  every active `configASSERT()` there receives one precomputed identifier.
  Interrupt-IN tuple failures no longer assert after publishing a bad
  completion. Host-owner, EP0-abort, and pinned hub-reset completion failures
  likewise take their existing task-side `-EIO` retirement path; lifecycle
  emits both diagnostics. A failed hub identity check cannot leave the reserved
  recovery slot in `HOST_COMPLETING` or retain the global EP0 lane.
  The shared EP0/interrupt-OUT completion also validates the unique serial's
  callback tuple before publishing it: EP0 checks its reconstructed setup,
  buffer, and bounded actual length, while non-control TinyUSB callbacks expose
  only address/endpoint and length. Invalid giveback metadata completes as
  task-side `-EIO` rather than reaching a parser or blocked synchronous caller.
- HID EP0 GET_REPORT/SET_REPORT now uses direct asynchronous
  `tuh_control_xfer()` requests submitted from the TinyUSB host owner. Each
  transfer carries the request serial in `user_data`, and completion preserves
  TinyUSB's real result and actual data length. Successful short transfers are
  no longer confused with failures; STALL and timeout remain distinguishable.
  Task-context `usb_control_msg()` now uses a fixed metadata-slot scheduler and
  a physical-device epoch lease. Interface requests also carry their live HID
  owner, so interface stop cancels the exact HID's blocked caller immediately
  instead of selecting a potentially reused address/instance or leaving it to
  a physical-device timeout. SET_IDLE and upstream-shaped raw
  GET/SET helpers reach that generic path while retaining the synchronous
  ll-driver return contract. HID report requests and generic control messages
  now share one same-device EP0 FIFO; a completed GET_REPORT retains its parser
  slot without continuing to occupy the physical wire queue.
- Normal GET_REPORT receive lengths follow upstream `hid_submit_ctrl()`: they
  are rounded to EP0 max-packet size and capped by the per-device
  `usbhid->bufsize` computed from all parsed INPUT, OUTPUT, and FEATURE reports
  (itself capped at Linux's 16 KiB HID limit). The completion-owned buffer is
  sized for that transfer and passed directly to the parser; raw GET_REPORT
  keeps its caller-supplied length. Raw report-ID zero
  offset/count behavior is again the upstream `usbhid_get_raw_report()` and
  `usbhid_set_raw_report()` code rather than a parallel request builder. Only
  the actual bytes reported by TinyUSB are handed to the Linux HID parser.
- The ll-driver output paths now follow upstream USB HID routing.
  `.request(HID_REQ_SET_REPORT)` sends an `HID_OUTPUT_REPORT` over interrupt
  OUT when the interface exposes that endpoint and otherwise uses EP0
  SET_REPORT. FEATURE reports and `.raw_request()` remain control-only, while
  `.output_report()` remains interrupt-only and now runs the upstream helper
  over generic `usb_interrupt_msg()`. Interrupt OUT uses a direct endpoint
  transfer with the complete wire image and request serial, so its real result
  and actual length are preserved instead of being synthesized by the TinyUSB
  HID callback facade. Generic OUT requests are ordered per device endpoint,
  together with `.request()` output on that endpoint, not behind unrelated
  devices in one global FIFO. As upstream does, the first interrupt OUT
  endpoint is selected and a successful short transfer remains successful.
- Async SET_REPORT retains one exact `hid_alloc_report_buf()` snapshot per
  queued request, matching upstream's enqueue-time value semantics. Generic
  synchronous control and interrupt-OUT requests instead borrow the blocked
  caller's buffer through physical completion or fenced cancellation. Removing
  the old 257-byte inline payloads and the private pre-probe metadata shrinks
  `hid_async_request` from 312 B to 60 B and its slot from 340 B to 88 B. The
  ten-slot executor allocation is now metadata-only. One aligned 256-byte
  lifecycle scratch lives in the existing transport-pool allocation, so moving
  descriptor policy out of the executor adds no heap block or fragmentation.
  The generic request carries TinyUSB's address epoch; lifecycle separately
  revalidates its physical-cache generation after every blocking call.
  Generic control uses USB's native 16-bit length, while HID `.request()`
  remains bounded by `HID_MAX_BUFFER_SIZE`.
- `usbhid_start()` now clears the NumLock output field on boot keyboards and
  submits the whole output report through that same `.request()` route,
  matching upstream startup behavior.
- Physical detach now closes the async generation and publishes a bounded
  cache tombstone without waiting in the TinyUSB callback. The app-driver close
  path covers hubs, for which TinyUSB does not issue the common unmount callback.
  That physical callback also publishes `disconnect_queued` and stops direct IN
  for every live interface before rotating the address-wide async epoch. The
  later HID class close is an idempotent exact-interface publisher. Neither path
  destroys a Linux HID object: lifecycle calls the upstream-shaped
  `usbhid_disconnect(struct usb_interface *)`, synchronizes report/async/I/O
  owners, and only then removes and frees it.
  Parent cache entries remain retired until their child subtree and HID objects
  are gone; fast reuse of the same device address starts a distinct generation.
  Cache fields are filled under the transport mutex and `valid` is published
  last. Callback paths which still enter through a cache lookup acquire an
  `io_pending` lease in that same scope. Direct interrupt-IN completion does no
  address/instance lookup: under the transport mutex it resolves the fixed
  slot's one live registry owner and validates that owner against the retained
  HID, `inbuf`, device generation, endpoint, size, and open revision before
  ownership moves atomically from `ARMED` to `QUEUED`. Pointer lifetime
  therefore no longer depends on current task affinity or priority ordering.
  Lifecycle flags and probe slots remain authoritative. A dedicated indexed
  task notification carries only the coalesced wake edge, so lifecycle needs
  no firmware event object or queue allocation.
- Report-descriptor fetch preserves upstream `hid_get_class_descriptor()`
  behavior: one zeroed exact-size buffer, up to four reads, and acceptance of a
  final successful short read. The lifecycle task serializes probe, so at most
  one such buffer exists across devices; it remains local to `usbhid_parse()`
  until async completion/cancel and is freed on every parser return path.
  Local `-EBUSY` admission waits yield without consuming one of the four real
  USB attempts, but the normal control timeout bounds a persistently full pool.
  The SHA-pinned build-local TinyUSB HID class no longer performs its earlier
  duplicate read into the 512-byte enumeration buffer: it completes class mount
  with `NULL`, which is the only value consumed by this callback facade, and
  task-side `usbhid_parse()` performs the single authoritative fetch through
  the generic asynchronous control broker. Full configuration descriptors are
  separate from that report path: they use TinyUSB's permanent 512-byte scratch
  when they fit, or one exact-size host-owner heap buffer up to 4 KiB when they
  do not. The firmware-only full device-descriptor refetch now retains pending
  HID metadata across
  three transient failures, with four accepted attempts and 100-ms lifecycle
  deadlines. `usb_control_msg()` pins the exact physical cache entry and
  TinyUSB address epoch while blocked; lifecycle checks the captured cache
  generation again before publishing any descriptor or string field, so
  detach/reuse cannot retarget a retry.
- `hid_hw_request()` now matches the upstream queue-and-return contract.
  Ordinary GET_REPORT completion enters the report queue. A probe-owned GET
  instead publishes its existing request buffer through the interface-local
  `owned_control_input` slot, and the waiting lifecycle task consumes it under
  the `driver_input_lock` it already owns. In both cases `hid_hw_wait()` drains
  through the end of parsing, so callers cannot observe a transport-complete/
  parser-pending false idle. It now registers the existing per-interface wait
  head before testing those durable predicates; owner publication or the final
  I/O release supplies a task wake instead of one-tick polling. After producer
  stop and exact-HID async cancel, teardown reuses that wait head for one
  `usb_kill_urb()`-shaped predicate: aggregate I/O is idle, no async slot retains
  the HID, and direct interrupt-IN has no owner, deferred host pass, or pending
  physical-detach fence. Each final release publishes a wake after dropping the
  transport mutex. This removes all three former teardown polling loops without
  adding a queue, semaphore, or heap allocation. Interrupt-IN stays gated until
  probe finishes, while returned feature fields are preserved instead of being
  lost to lock contention. Raw GET/SET and interrupt output keep their upstream
  synchronous return contract while using the same asynchronous TinyUSB owner
  underneath.
- Fixed-slot admission now belongs to the generic synchronous USB bridge rather
  than descriptor callers. Lifecycle and `hid_workqueue_task` register bounded
  stack-owned FIFO waiters; normal-slot release or matching teardown wakes them,
  and enqueue retry remains the durable predicate. Existing nonblocking report
  and CLEAR_HALT producers may use only capacity not logically reserved by a
  waiter, while HUB_RESET retains its dedicated recovery slot. Admission has
  its own one-second local deadline before a successfully queued request
  receives its complete transfer interval. The two descriptor helpers contain
  no local FreeRTOS retry loop, and `hid_get_class_descriptor()` is
  upstream-identical. CLEAR_HALT saturation is also condition-driven: the
  report task atomically claims Linux's reset-work-shaped state, parks it on
  local `-EBUSY`, and retries only after normal-slot release or a reservation
  is removed without consuming a slot. A one-byte capacity latch closes the
  release-during-enqueue race. The former 32-ms admission polling timer is
  gone; the report task sleeps until a capacity edge or the preserved absolute
  eight-second local saturation deadline. Unrelated wakes only recheck that
  same deadline, while the report timer now represents only upstream's
  protocol-error retry.
- The report executor reserves space for all four queued async requests plus
  the active ordinary control request. Probe-owned GETs do not occupy that
  queue; their current upstream caller issues one request and immediately waits
  for its direct per-interface completion. Interrupt-IN close is reconciled in
  the TinyUSB owner: abort completion is drained for two SOFs before a raced
  reopen may arm a new receive. EP0 retirement is also bounded: three two-SOF
  host-owner fences cover SETUP/DATA/ACK, after which a SHA-pinned TinyUSB helper
  matches daddr + callback + serial and synthesizes the old owner's TIMEOUT
  giveback if the PIO completion event was lost. It cannot abort a replacement
  EP0 owner after address reuse.
- Interrupt IN now uses the first interrupt-IN endpoint directly, matching
  upstream endpoint selection and preserving TinyUSB's exact transfer result
  and actual length. The request length is the largest parsed INPUT report,
  including its report ID, capped at Linux's 16 KiB HID limit. Upstream's
  per-interface `inbuf` ownership is restored; task context allocates INPUT-only
  backing with one-packet minimum/tail padding required by the pinned PIO HCD.
  Four fixed transport slots retain `inbuf` plus raw completion metadata. The
  endpoint callback first resolves the slot through the live HID registry and
  validates owner, address/endpoint, generation, buffer, and size. Only that
  accepted tuple publishes result/length and `ARMED -> QUEUED`; the report task
  claims `QUEUED -> ACTIVE` round-robin and parses zero-copy. A failed invariant
  clears the physical serial, parks the interface, wakes its existing waiters,
  and publishes one lifecycle fault without parsing or logging in the callback.
  There is no interrupt-input queue or address-based HID lookup; the bounded
  registry scan validates only the fixed slot owner. The endpoint is rearmed
  only after the slot returns to `STOPPED`. Every arm snapshots the
  open revision. Close/reopen aborts an old `ARMED` transfer through the same
  two-SOF fence as `usb_kill_urb()`; an already queued old completion is dropped
  and the current open is freshly armed. Non-`ALWAYS_POLL` close waits for that
  report-only owner/parser fence before returning; it does not wait unrelated
  control I/O or physical-detach state. Physical unplug publishes a durable
  detach state. The report task queues one host-owner fence which completes only
  after TinyUSB's class/HCD close pass. No callback allocates or frees the
  buffer, or waits for report/USB progress; bounded publication may briefly
  take the shared transport mutex. Completion snapshots the byte-sized
  transport open state;
  successful payload is dropped for both `CLOSED` and `RESUMING`, including
  `HID_QUIRK_ALWAYS_POLL` while its client is closed. Non-`ALWAYS_POLL` open
  retains upstream's `HID_RESUME_RUNNING` 50-ms drain before changing to
  `OPEN`. This matches upstream's `HID_OPENED`/resume tests even when parsing
  is deferred across close/reopen, without reading generic HID's separately
  locked `ll_open_count`.
  Non-success payload never reaches the HID parser. There is no port-only
  BOOT-mode gate; Linux usbhid relies on the USB reset-default Report protocol.
  STALL queues the standard
  endpoint `CLEAR_FEATURE(HALT)` request on the generic per-device EP0 lane;
  only successful completion lets the TinyUSB host owner reset the PIO endpoint
  toggle to DATA0 and rearm. Like an already running upstream `reset_work`, an
  active clear-halt finishes across HID close: success still resets the host
  toggle and a wire failure still publishes device reset; `report_wanted`
  gates only the later interrupt-IN rearm. Stop/unplug still cancels the old
  transport epoch. A failed TinyUSB interrupt-IN arm now publishes one durable
  `IO_ERROR_PENDING` state; the report task, not the host owner, validates its
  device/open epoch and starts Linux's retry/reset policy. A close/reopen drops
  the old failure and reconciles only the new open. FAILED/TIMEOUT follows upstream's
  13/26/52/104-ms delayed retry for about one second. Clear-halt transfer
  failure, exhausted protocol retry, or failure to reset the local PIO DATA0
  state now queues the upstream device-reset fallback. Reset publication is
  versioned with `report_revision`, matching close's upstream
  `cancel_work_sync()` boundary: a close/reopen which wins the publication race
  rearms only the new open instead of parking it or reviving the old reset.
  The firmware lifecycle
  retains no HID pointer: it snapshots the physical topology, closes the exact
  old cache epoch, waits for HID/descriptor/async retirement, and performs full
  TinyUSB re-enumeration behind a global EP0 gate. Logical control work remains
  accepted in bounded parked slots and has its timeout shifted when the gate
  opens; one additional async slot is reserved for hub-port recovery so normal
  traffic cannot starve it. Lifecycle no longer polls reset state every 10 ms:
  retirement, mount, enumeration terminal, async-slot release, and TinyUSB's
  global control-IDLE transition publish wake edges, while phases without an
  external owner sleep to their exact deadline. The async edge is published
  after the completed slot is released, so a hub retry sees the reserved
  recovery slot as reusable. The host-core edge also covers native hub
  GET_STATUS/CLEAR_FEATURE chains, abort, remove, and watchdog release. A
  rejected root attach parks until that edge or the one global enum terminal;
  it never self-kicks. A matching enumeration is checked atomically at root
  admission. Root attach is serialized in the TinyUSB host owner;
  a hub child makes up to three pinned `hub_port_reset()` attempts. An exact
  fully mounted native replacement may win only before the first wire reset is
  published. Once reset outcome can be ambiguous, raced mounts are exclusions:
  failure/timeout retries without enumeration, while known wire success closes
  the raced epoch and enters `enum_new_device()` directly in the host owner.
  Persistent local async-pool exhaustion is a firmware scheduling failure and
  parks the endpoint instead of falsely resetting a healthy USB device.
- Pico SDK 2.1.1's pinned TinyUSB does not issue `tuh_mount_cb()` for hubs and
  exposes no exact post-`enum_full_complete()` fence or non-recursive
  host-owner enumeration entry. CMake verifies the pinned `usbh.c` SHA and
  exact unique anchors, then generates a build-local copy with the host-owner,
  enumeration-terminal, bounded-recovery, and global-control publications
  required by the lifecycle glue. The complete active source inventory,
  semantic patch groups, and SDK upgrade procedure are recorded in
  [`tinyusb-host-port.md`](tinyusb-host-port.md). The pinned TinyUSB input is
  never modified and drift in that selected source fails configuration for an
  explicit re-audit. Its former 50-ms root-reset, 450-ms root/hub debounce, and
  2-ms post-address waits are now host-owned deadlines and continuations, so the
  sole TinyUSB event pump keeps processing REMOVE and duplicate ATTACH while an
  enumeration delay is active. A foreign ATTACH is deduplicated in a compact
  host-owned topology FIFO instead of being sent with an infinite wait back into
  that pump's own queue; REMOVE prunes stale entries and the next host-service
  iteration after terminal/mount unwind starts the oldest survivor directly.
  The public host loop is again exactly `while (1) tuh_task();`. Inside the
  generated TinyUSB core, an armed continuation, no-progress watchdog, or
  physical-drain fence shortens only the next private queue wait to its exact
  deadline; HCD/deferred events wake that wait earlier. With nothing armed the
  queue wait is indefinite, and there is no fixed periodic wake. Failed
  enumeration-control completion snapshots its callback-local setup tuple into
  the enum epoch, arms TinyUSB's original 100-ms retry deadline, and returns.
  Shallow host-owner service reconstructs and resubmits the transfer only after
  callback unwind and global EP0 idle. The existing retry count and transfer-
  buffer ownership are preserved without a blocking delay, periodic poll, Pico
  timer, FreeRTOS timer, or extra queue.
- The same compatibility generation pins `hid_host.c` and preserves its full
  `hidh_open()` and `hidh_set_config()` blocks commented beside their two
  replacements. A bounded two-pass scanner accepts the
  HID descriptor in the current interface extras (including after endpoint[0]),
  opens no more than `bNumEndpoints`, and publishes the class slot only after
  endpoint success. Enumeration performs neither SET_IDLE nor SET_PROTOCOL and
  mounts without borrowing the shared enumeration buffer; mount publishes only
  its ephemeral class identity. Lifecycle builds the retained interface, then
  upstream-shaped `usbhid_parse()` sends Linux's SET_IDLE and owns the one
  exact-size descriptor request plus its retry/error semantics. TinyUSB retains
  Report-protocol metadata only to match the reset default. Its upstream
  report-descriptor prefetch branch remains unchanged but unreachable because
  the replacement set-config path directly completes the class mount.
- A third, single-anchor compatibility source pins TinyUSB's PIO HCD. Its
  upstream `hcd_edpt_clear_stall()` is a no-op; the generated implementation
  maps `rhport` through the HCD's own `RHPORT_PIO()` macro and resets the local
  endpoint toggle through the existing Pico-PIO-USB helper. Report recovery now
  calls only the generic HCD API after remote `CLEAR_FEATURE(HALT)` succeeds,
  so Linux-shaped transport no longer owns PIO root numbering. The vendored
  Pico-PIO-USB sources are not modified by this step.
- Fixed-slot task-side input-report delivery, the firmware workqueue, and the
  firmware timer bridges are present for the currently linked driver set.
- Input registration no longer opens the firmware's always-on evdev client
  inside an unfinished HID driver probe. `evdev_connect()` retains an inactive
  Linux input handle. After successful `hid_add_device()`, lifecycle first
  drains finite probe-originated control/OUT requests. Activation then prepares
  every sibling client/queue privately, opens every matching handle, and only
  after every open succeeds publishes their final devmon snapshots and writers.
  This prevents a mode-setting probe SET from being overtaken by the separately
  scheduled direct IN path, prevents cross-core output/FF calls from observing
  an unopened client, and ensures the first open cannot expose only part of a
  sibling set. Events synchronously generated by an open hook are suppressed
  while the prepared queue is not yet a QueueSet member. Like upstream,
  `mt_on_hid_hw_open()` may enqueue a mode SET and return without an extra
  wait; the async FIFO retains that ordering. The report task consumes/rearms
  completions during activation but does not parse them until final
  `driver_ready`; that final readiness publication is rejected if detach won
  the race, and already published ADDs receive ordinary removal during unwind.
- Host evdev queues have 64 entries: this covers hid-input's standard 60-value
  multitouch hint, two input-core framing slots, and two firmware
  lifecycle/release slots. An unusual input whose computed `max_vals` exceeds
  62 raises `WARN: EVDEV_BATCH_CAP` and remains best-effort instead of growing
  the bounded shared heap. Other firmware input queues stay at 16 entries. A
  full producer drops only the incoming record; it never dequeues a QueueSet
  member, so notification tokens remain paired. Lifecycle removal runs in task
  context and waits for KeyD to make its reserved queue slot available.
- The standard HID Haptics path is linked through `hid-haptic`,
  `hid-multitouch`, ff-core, evdev, and the firmware workqueue. Broader gaming
  FF drivers remain deferred. Hiddev remains in its bounded firmware-proxy
  form.
- Haptic effect storage is bounded to five firmware application slots rather
  than Linux's 96 userspace slots. Each slot still owns
  the upstream per-effect report snapshot, but unused slots no longer consume
  the shared probe heap.
- Haptic upload/erase/destroy now preserve the upstream ff-core ordering while
  closing three lifecycle holes: replacing or erasing the last Press/Release
  effect restores DEVICE auto-trigger mode, an erased slot cannot race a queued
  PLAY that still owns its report buffer, and teardown cancels all haptic work
  before releasing either the HID device or those buffers.
- Haptic and ff-core probe now check the heap-backed FreeRTOS semaphore created
  by each Linux-shaped `mutex_init()`. Allocation failure unwinds as `-ENOMEM`
  before any lock use; failed ff publication also removes the temporary
  `EV_FF`/`FF_HAPTIC` capability, because this port keeps the otherwise usable
  multitouch input after optional haptic initialization fails.
- Multitouch active contacts now follow upstream fix `8813b061`: a separately
  allocated bitmap is sized from the finalized `maxcontacts`, while
  `mt_io_flags` contains only RUNNING at bit zero. The old code wrote bit 32
  past a 32-bit RP2040 `unsigned long`, could corrupt adjacent driver state for
  larger slot IDs, and checked only slots 0 through 7 for sticky release.

## Manual Test Notes

- 2026-07-22: the current dirty host-core step replaces TinyUSB's last blocking
  100-ms enumeration retry with a host-owned deadline/continuation and adds
  exact transient full configuration-descriptor storage for 513..4096 bytes.
  Host UF2 SHA256
  `63e935043eb04b95c8fe5c377024f6d7b7b9897b82ed04626a502bf9f98fca2f`
  and emulator UF2 SHA256
  `58d309156aa7fb4a8152f6623ff2a4a4e1e2c7c0fcebe44f90afcdc307eef496`
  passed three repeated hardware cycles. The `cafe:1005` fixture deliberately
  stalls its first full configuration GET, then reaches a working HID interface
  at byte 575 of a 600-byte configuration after the one-shot retry. Removal
  returns to stable `free=60936/60944` plateaus with `oom=0`. The ordinary
  haptic/multitouch hot-plug regression also passed on the same host image.
- 2026-07-21: TinyUSB PIO-HCD clear-stall adapter checkpoint, exact host UF2
  SHA256 `7f1d77e9f2d589db50a1e1a863fe12a0bb4126456ab9707200bd51eab25647ef`,
  clean-builds with `text=501948`, `data=708`, and `bss=245376`. The dirty
  `device/haptic-touchpad` emulator UF2 SHA256
  `370afde1483ca15c346c3ec25726f5b48b2d2ad3db245d567d8dcf476f42b0b5`
  forced `2 -> HID_RX_STALL -> HID_CLEAR_HALT_OK -> s`; input continued after
  recovery and after programmed removal/re-enumeration of all three interfaces.
  The active heap returned to the same `free=10264`, `largest=8800`,
  `blocks=3`, `oom=0` plateau.
- 2026-07-20: edge-driven CLEAR_HALT checkpoint `usb: make clear-halt admission edge driven`, exact UF2 SHA256
  `312a456a981ac2dcbe057ce8fe247673952106f6dcbb520b26e32f0c7a92beaa`,
  clean-builds with `text=492388`, `data=708`, and `bss=243312`. The requested
  normal boot/enumeration and emulator input regression pass was reported
  working on hardware. This does not force CLEAR_HALT pool saturation or the
  terminal device-reset path.
- 2026-07-20: generic synchronous-request admission checkpoint `usb: make synchronous request admission waitable`,
  exact UF2 SHA256
  `b58404971ae20b6de376f8dc22ae9912a429a1d8b56086dfd852c8555cf87a64`,
  clean-builds with `text=491156`, `data=708`, and `bss=243296`. Normal boot,
  enumeration, emulator input, pointer movement, and wheel events were reported
  working on hardware. The subsequent edge-driven CLEAR_HALT-admission change
  is not part of that verified image.
- 2026-07-20: SHA-pinned HID-open/report-ownership image, exact UF2 SHA256
  `c0a58d7adf6d1bb8fac1d94d713f53443271235254dbe4cdd7ed5c8e923e6df2`,
  was reported working for normal boot, enumeration, emulator input, and mouse
  movement. This does not cover malformed interface extras, transient device-
  descriptor reads, or dropped EP0 completion injection.
- 2026-07-20: direct host-owner re-enumeration checkpoint, exact UF2 SHA256
  `bea0614236dd6de396f1a74678243f7f3b056d46e1261b58198fd1f06a01e94b`,
  builds with `text=492676`, `data=660`, and `bss=243308`. Normal boot,
  enumeration, and input were reported working on hardware. This does not
  exercise injected endpoint failure or root/hub reset recovery.
- 2026-07-20: asynchronous device-reset checkpoint `hid: recover failed devices asynchronously`, exact UF2
  SHA256 `fe3d0c717171499c1ad40a69d5d30cb1d5d6a8cc1525435fb1aa7566ceb362fb`,
  was reported working on hardware for normal boot, enumeration, and input.
  This is not fault-injection coverage of terminal retry, root reset, hub-port
  reset, or native re-enumeration races; those remain explicit checks below.
- 2026-07-20: task-side exact report-descriptor fetch checkpoint, exact UF2
  SHA256 `eaa95df9bfe2b39d84f9d6048c8c49a84df32cbc49073f39dc83b240a131572a`,
  clean-builds with `text=482212`, `data=660`, and `bss=243308`. It removes
  the TinyUSB enumeration-buffer limit from HID report descriptors and recovers
  2,008 B of persistent heap_4 allocation. This exact image was reported
  working on hardware. The larger-than-512-byte descriptor and hub-subtree
  cases remain separate coverage items.
- 2026-07-20: dynamic interrupt-IN checkpoint `hid: allocate interrupt input buffers per device`, exact UF2 SHA256
  `13edb5b61ca9c57f8da59613eb65b05cb8944f78c4bfef74f6cc50357264bd37`,
  builds with `text=477972`, `data=660`, and `bss=243308`. It removes the
  fixed 64-byte RX ceiling and adds a post-HCD-close detach fence. Normal input
  and reconnect behavior were reported working on hardware.
- 2026-07-20: request buffer-ownership checkpoint `hid: give async requests explicit buffer ownership`, exact UF2 SHA256
  `290ed22778c8905c49d2006bcb719ada1c3e8147cca4e4c41c8744025bde14e8`,
  builds with `text=477444`, `data=916`, and `bss=243308`, and was reported
  working on hardware. It is the baseline for the dynamic-IN step above.
- 2026-07-20: physical-endpoint scheduling / generic clear-halt checkpoint
  `hid: serialize requests by physical USB lane`, exact UF2 SHA256
  `8c21c80630bcc857e381fff4d466eb9defc392f7c9e67a58e8743401986ff235`,
  was reported working on hardware.
- 2026-07-20: upstream raw/output helper checkpoint `hid: route synchronous usbhid helpers through generic bridge`, exact UF2
  SHA256 `8e8402604678195e07980c211f541d87157a64a57d5e16f323d9d749c2560e8c`,
  was reported working on hardware and is the baseline immediately before the
  now-tested physical-endpoint lane / generic clear-halt consolidation.
- 2026-07-20: generic USB-message checkpoint `hid: add generic asynchronous USB message bridge`, exact UF2 SHA256
  `739e0d119cd2e5dd7e64051b75181dca5d7c90c411c7cc924c64e3ceac791e6d`,
  was reported working on hardware and is the baseline immediately before the
  now-tested upstream raw/output helper consolidation.
- 2026-07-19: host checkpoint `hid: preserve exact interrupt output completion` booted with 43,008 B minimum free
  heap, a 43,160 B largest free block, and 938 words free at the TinyUSB task
  watermark. Repeated `c` press/release reports reached the input boundary.
  This verifies exact interrupt OUT and is the baseline immediately before the
  direct interrupt-IN checkpoint; direct IN still requires its own hardware
  pass.
- 2026-07-19: host checkpoint `hid: retire queued cancels outside callbacks` passed the strict hi-res wheel
  emulator again after queued cancel retirement moved out of TinyUSB unmount
  callbacks. Pointer events resumed normally after reconnect.
- 2026-07-19: host checkpoint `hid: harden callback ingress and hub retirement` enumerated the current haptic-touchpad
  emulator, delivered keyboard/pointer events, and retained at least 43,672 B
  of FreeRTOS heap before the HID device was attached. This verifies the
  callback-ingress checkpoint, not the output-routing change below.
- 2026-07-19: host checkpoint `hid: route output reports like upstream usbhid` booted with 43,712 B minimum free heap
  before HID attachment and 938 words free at the TinyUSB task watermark.
  Repeated `c` press/release reports reached the input boundary. This is a
  no-regression observation for the output-routing checkpoint; it does not yet
  verify the direct-control/SET_IDLE checkpoint described above.
- 2026-07-18: the strict hi-res wheel fixture verified a probe-time FEATURE
  `GET_REPORT` returning `0xA0`, locked parser state preservation, and the
  resulting raw `SET_REPORT` payload `0xA5` on hardware. Pointer events remain
  gated unless that complete round trip succeeds.
- Fixture: `../ErgoType-hid-devices`, branch
  `device/hires-wheel`, commit `184429e`; host checkpoint `hid: preserve probe GET report state`.
- 2026-07-10: Razer BlackWidow async raw SET_REPORT path was exercised with
  the emulator and Pico host logs. This is an emulator/transport verification,
  not a matching Razer hardware claim.
- Fixture: `../ErgoType-hid-devices`, branch
  `device/razer-blackwidow`, commit `4b2fc8f`, VID:PID `1532:010e`,
  product `Razer BlackWidow Test`.
- Laptop Linux sees the emulated Razer device under the Linux `razer` HID
  driver and reports macro usage `0x68` as unsupported evdev code `0x290`.
- ErgoType Pico host received the async Razer raw SET_REPORT path and then the
  same macro usage as unsupported evdev code `0x290`.
- The remaining `0x290` handling is consumer/keyd-side work, not an async HID
  transport failure.
- Callback-side mount and descriptor debug logs were removed. Sticky callback
  fault bits are drained and logged by the lifecycle task; async timeout and
  report-request status logs remain task-side.

## Current Driver Boundary

- The linked driver set can use upstream `hid_hw_request()` followed by
  `hid_hw_wait()`, including returned feature data during probe. The bounded
  firmware queue still rejects overload instead of attempting Linux's much
  larger control/output FIFOs.
- Drivers that need generic USB URBs, interrupt-IN synchronous messages,
  HID requests beyond 16 KiB, USB messages beyond the 16-bit wire length,
  broad Linux subsystem state, or unaudited callback behavior stay out of
  `CMakeLists.txt`.
- `usb_host/deferred-hid-drivers.md` records the current deferred boundary and
  examples.

## Next Checks

- Keep verifying the linked drivers with targeted emulators or matching
  hardware before claiming hardware coverage.
- Verify task-side `usbhid_parse()` report-descriptor fetch with normal enumeration,
  haptic allocation, direct unplug/replug, and a hub subtree reconnect. Also
  exercise a report descriptor larger than 512 bytes. Record free heap and the
  TinyUSB stack watermark; inspect the lifecycle watermark separately if the
  hardware pass exposes any stack symptom.
- The 600-byte configuration, useful HID interface beyond byte 512, injected
  failed full GET, and repeated stable add/remove plateau are hardware-verified.
  Remaining boundary/fault coverage is exact lengths 512, 513, and 4096,
  invalid/short full reads, and a declared length above 4096.
- Exercise REMOVE and a foreign ATTACH while the 100-ms retry deadline is
  armed. The successful error-only continuation itself is hardware-verified;
  these remaining cases target cancellation and competing topology only.
- Verify the output-routing/startup-LED checkpoint on hardware: a boot keyboard
  without interrupt OUT must receive the enumeration-time NumLock reset over
  EP0, and an OUTPUT request on an interface with interrupt OUT must use that
  endpoint. For the direct-OUT checkpoint also verify normal input after boot,
  unplug/replug, and haptic or LED output without `HID_REPORT_OUT_FAIL`,
  `HID_SUBMIT_TO`, or `HID_XFER_TO`. Keep these as separate observations.
- Verify the direct-control checkpoint with the haptic-touchpad or hi-res-wheel
  fixture: descriptor pre-probe, feature GET/SET, input events, and unplug/replug
  must all complete without `HID_SUBMIT_TO`, `HID_XFER_TO`, or
  `HID_CTRL_DISPATCH_FAIL`. Record post-attachment heap. Only the ordinary-
  control report queue remains (232 B of startup heap); removing the input queue
  returns 176 B and one allocation block. The reconcile table adds 32 B of
  static RAM, while four 32-byte direct-IN slots occupy 128 B in scratch X.
- Verify task-side probe SET_IDLE with a fixture that records class requests:
  exactly one SET_IDLE must precede the report-descriptor GET, a SET_IDLE STALL
  must not abort probe, and no generic SET_PROTOCOL may appear. Exercise the
  later ll-driver `.idle` hook separately through the same async-backed path.
  On a composite with three or four HID interfaces, also record the TinyUSB
  task watermark: set-config now follows TinyUSB's supported synchronous
  interface-completion pattern rather than yielding through a class request.
- Verify direct interrupt IN with keyboard and pointer traffic, then repeated
  unplug/replug. The expected normal path has no `HID_RX_STALL`,
  `HID_RX_XFER_FAIL`, `HID_RX_REARM_FAIL`, or `HID_REPORT_SKIP`. Record heap,
  largest free block, block count, and the TinyUSB stack watermark.
- The dirty `device/haptic-touchpad` fixture completed the basic recovery
  baseline on hardware on 2026-07-21, before the controller-specific DATA0
  helper moved behind TinyUSB's HCD API: marker `2`, one `HID_RX_STALL`,
  `HID_CLEAR_HALT_OK`, then marker `s` and continued input. Its programmed
  reconnect repeated the exact active heap plateau with `oom=0`. This proves
  the underlying remote clear, local DATA0 reset, and interrupt-IN rearm for
  the root-device success path. The generated PIO-HCD adapter was then verified
  with host UF2 SHA256
  `7f1d77e9f2d589db50a1e1a863fe12a0bb4126456ab9707200bd51eab25647ef`:
  it repeated `2 -> HID_RX_STALL -> HID_CLEAR_HALT_OK -> s`, continued input,
  removed all three interfaces, and re-enumerated. The active heap returned to
  `free=10264`, `largest=8800`, `blocks=3`, with `oom=0`, matching its first
  enumeration. The isolated `device/rx-stall` fixture remains
  available for a smaller recovery-only reproduction. A separate deterministic
  fixture is
  still needed to unplug while clear-halt is active. A normal TinyUSB NAK does
  not prove FAILED/TIMEOUT retry; that path needs a lower-level no-response or
  bad-packet injector. Also force terminal clear-halt/protocol failure for both
  a root device and a hub child: expect `HID_RESET_Q`, complete old-epoch
  teardown/re-enumeration, then `HID_RESET_OK` and resumed input. Exercise
  unplug, parent-hub removal, address reuse, and a native same-port replug in
  each reset phase. The reset paths themselves are not yet hardware-verified.
- Extend the serialized transport explicitly before importing drivers that need
  URBs, interrupt-IN synchronous messages, or request/response protocols beyond
  the native HID/USB length contracts.
