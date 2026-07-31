# Async HID Progress

This is the chronological development and hardware-checkpoint ledger. Newest
entries come first in `Manual Test Notes`; an older hash keeps exactly the
verdict recorded for that image and is never promoted by a later build. The
maintained TinyUSB patch contract lives in
[`tinyusb-host-port.md`](tinyusb-host-port.md), current RP2040 memory accounting
in [`pio-usb-memory.md`](pio-usb-memory.md), and current audit findings in
[`upstream-porting-audit.md`](upstream-porting-audit.md).

## Implementation Milestones

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
  design rather than more heap-backed mutexes. TinyUSB host-event publication
  now uses its ordinary queue path; the removed spill adapter no longer adds a
  port-owned critical section at that boundary.
  The task-only workqueue bridge is now a separate execution domain with its
  own priority-inheritance mutex. Durable FIFO/flags are the condition, direct
  task notifications are wake edges, and stack-owned waiters preserve
  `flush_work()`, `cancel_work_sync()`, and `destroy_workqueue()` without the
  former 15 critical regions or one-tick polling. That same mutex now owns a
  wrap-safe delayed-work deadline list for Wacom initialization and AES battery
  expiry: due entries move into the ordinary FIFO, pending/promoted/running
  synchronous cancel shares one lifetime fence, and simultaneous cancelers keep
  callback requeue disabled until the last waiter returns. Destroying a
  firmware queue promotes its delayed entries before drain;
  `mod_delayed_work()` remains compile-gated without callers. Receiver rebind
  and receiver battery work run on the lifecycle owner's ordinary work list.
  Battery callbacks use nonblocking parser-lock retry, receiver work snapshots
  PID under the monitor lock, and both sibling initialization callbacks are
  synchronously cancelled before dynamic rebind releases their resources.
  The timer bridge is a second task-only execution domain with its own
  priority-inheritance mutex. Its active Wacom `TIMER_DEFERRABLE` caller keeps
  not-before-deadline and cancellation semantics, but the always-running timer
  task may execute promptly at the deadline.
  TinyUSB unmount publishes stopping and wakes the report task; that owner
  claims and synchronously cancels an I/O-retry timer under an `io_pending`
  lifetime lease. Direct notifications replace the timer's one-entry wake
  queue, and stack-owned waiters replace `timer_delete_sync()` polling. Neither
  domain is mixed into the transport mutex or masks scheduler-wide interrupts.
  The live workqueue parents are KeyD, the dedicated HID timer/lifecycle tasks,
  and the worker itself; TinyUSB callbacks only publish state to those owners.
  FreeRTOS calls and invariant predicates are evaluated before any
  `configASSERT()`, so a Release build cannot erase the mutex operation itself.
  Startup constructors return allocation/readiness errors normally. Impossible
  lock misuse remains a debug assertion over an already computed boolean; the
  lock helpers do not add a second diagnostic/fail-stop state machine or log
  from a TinyUSB callback.
  The same no-expression assertion rule is now enforced across the async
  executor, USB HID lifecycle, interrupt-report owner, and transport mutex:
  every active `configASSERT()` there receives one precomputed identifier.
  Interrupt-IN tuple failures no longer assert after publishing a bad
  completion. EP0/OUT and pinned hub-reset completion failures take their
  existing task-side `-EIO` retirement path and lifecycle emits the bounded
  diagnostic. A failed hub identity check cannot retain the global EP0 lane.
  Shared EP0/interrupt-OUT completion validates the retained serial,
  generation, address/endpoint, and bounded actual length. It deliberately
  trusts the SHA-pinned TinyUSB callback to return the request it was given
  instead of rebuilding and comparing the SETUP tuple a second time.
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
- A CTRL/interrupt-OUT head which fails before reaching the wire is retained in
  a logical `PARKED` state, matching upstream's stopped queue rather than
  dropping the report or retry-spinning. A later enqueue on that same interface
  and lane is the upstream-shaped restart edge and preserves FIFO order.
  TinyUSB reporting a currently owned EP0/endpoint is not such a submit
  failure: the accepted report remains queued until the exact owner-idle edge
  or cancellation, with no firmware-only one-second pre-wire timeout.
  Ordinary `hid_hw_wait()` excludes that stopped lane's logical lifetime leases,
  while disconnect still drains and frees them all. If a parked GET made an
  ordinary waiter return, its parser-owner task is cleared atomically with the
  idle decision; a later restart therefore cannot publish completion to an old
  stack/task owner and falls back to normal report-task parsing. Because this
  idle predicate can become true at a nonzero lease count, every lease release
  wakes linked waiters rather than only the final zero transition.
- Fixed-slot admission now belongs to the generic synchronous USB bridge rather
  than descriptor callers. Lifecycle, `hid_workqueue_task`, report requests,
  and CLEAR_HALT each link a bounded admission node into one FIFO. The oldest
  eligible endpoint-front may enter when a real normal slot is free; requests
  do not reserve placeholder slots while waiting. Normal-slot release, waiter
  unlink, or matching teardown notifies the affected task, which rechecks this
  durable predicate. HUB_RESET alone retains its separate physical recovery
  slot. Synchronous admission has its own one-second local deadline before a
  successfully queued request receives its complete transfer interval. The two
  descriptor helpers contain no local FreeRTOS retry loop, and
  `hid_get_class_descriptor()` is upstream-identical. CLEAR_HALT atomically
  claims Linux's reset-work-shaped state and remains in `WAIT_SLOT` after local
  `-EBUSY`; its linked admission node owns the next generic capacity wake. The
  former private capacity latch and 32-ms polling timer are gone. The report
  task sleeps until a generic admission notification or the preserved absolute
  eight-second local deadline, then rechecks the same state and predicate.
- The report executor reserves space for all four queued async requests plus
  the active ordinary control request. Probe-owned GETs do not occupy that
  queue; their current upstream caller issues one request and immediately waits
  for its direct per-interface completion. Interrupt-IN close is reconciled in
  the TinyUSB owner: an immediate one-shot continuation in TinyUSB's own FIFO
  fences a raced completion before reopen may arm a new receive. EP0 retirement
  now uses the same single host-owned transaction as enumeration. Raw abort
  success completes immediately; a lost race snapshots and drains only the
  already-queued TinyUSB FIFO prefix, consuming the matching old stage before
  it can chain DATA/ACK. Prefix exhaustion synthesizes the old owner's TIMEOUT
  through its exact daddr + callback + serial tuple. It cannot abort a
  replacement EP0 owner after address reuse.
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
  open revision. Close/reopen aborts an old `ARMED` transfer through the
  same-queue giveback fence as `usb_kill_urb()`; an already queued old completion is dropped
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
  first waits on a stack-owned predicate until TinyUSB's host owner has made
  Linux's equivalent first `usb_submit_urb()` attempt, then retains upstream's
  `HID_RESUME_RUNNING` 50-ms drain before changing to `OPEN`. This matches
  upstream's `HID_OPENED`/resume tests even when parsing is deferred across
  close/reopen, without reading generic HID's separately locked
  `ll_open_count`.
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
  state now queues the upstream device-reset fallback. Each report interface
  owns one bit in a generation-scoped reset claimant mask. Close cancels only
  its own still-queued claim; a sibling interface's claim survives, and a reset
  already claimed by lifecycle is deliberately uncancellable like running
  upstream `reset_work`. The report revision still prevents a close/reopen from
  parking the new open or reviving old report recovery.
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
  it never self-kicks. The one global enumeration owner is retained from every
  active publication through its terminal even when it began before reset took
  the gate; root and hub admission check it atomically, and time spent behind it
  cannot consume the reset phase timeout. Exact-topology enumeration state still
  decides reset success/failure. Root attach is serialized in the TinyUSB host owner;
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
  that pump's own queue. An idle same-topology duplicate first runs normal
  TinyUSB removal, but remains fenced until the firmware's asynchronous
  Linux/cache generation retires. REMOVE revokes only stale attach intent, not
  that lifetime fence; a genuinely later ATTACH can reassert intent. The next
  host-service iteration after terminal/mount/retirement unwind starts the
  oldest ready survivor directly. Fresh and duplicate-restart entry points now
  also require a reusable Linux device-cache epoch. If all fixed epochs are
  active or retiring, the ATTACH remains in that FIFO and final lifecycle
  release wakes TinyUSB to recheck capacity; no polling or additional cache
  slot is used. A parked restart releases the physical enumeration owner
  through an explicit state callback without falsely completing or failing a
  pending reset.
  The public host loop is again exactly `while (1) tuh_task();`. Inside the
  generated TinyUSB core, an armed continuation or no-progress watchdog
  shortens only the next private queue wait to its exact deadline; HCD/deferred
  events wake that wait earlier. Cancel retirement instead consumes the exact
  FIFO prefix already present when the PIO abort loses its race. With nothing
  armed the queue wait is indefinite, and there is no fixed periodic wake. Failed
  enumeration-control completion snapshots its callback-local setup tuple into
  the enum epoch, arms TinyUSB's original 100-ms retry deadline, and returns.
  Shallow host-owner service reconstructs and resubmits the transfer only after
  callback unwind and global EP0 idle. The existing retry count and transfer-
  buffer ownership are preserved without a blocking delay, periodic poll, Pico
  timer, FreeRTOS timer, or extra queue.
- TinyUSB host events use one ordinary 32-entry FreeRTOS queue. The generated
  dynamic queue definition avoids OSAL's otherwise-dead static backing array;
  no spill FIFO, overflow flag, or firmware-specific fail-stop remains.
- The same compatibility generation pins `hid_host.c` and preserves its full
  `hidh_open()` and `hidh_set_config()` blocks commented beside their two
  replacements. A bounded two-pass scanner accepts the
  HID descriptor in the current interface extras (including after endpoint[0]),
  ignores non-interrupt endpoints, opens only the first interrupt IN/OUT pair
  exactly as upstream `usbhid_start()` does, and publishes the class slot only
  after endpoint success. Enumeration performs neither SET_IDLE nor SET_PROTOCOL and
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
- The complete pinned Wacom sources use a narrow USB-only table containing the
  seven hardware-tested base IDs `056a:0027/0029/0084/00de/037a/037b/5048`
  plus external wired Intuos `0302/0303/030e/0323`, Intuos 2
  `033b/033c/033d/033e`, and Intuos Pro `0314/0315/0317`. The active upstream
  paths retain Pen/Pad/Touch parsing,
  ExpressKeys, Touch Ring, LED control, pen-touch arbitration, ordinary/AES
  battery, delayed initialization, idle proximity, and receiver
  pair/unpair/re-pair with dynamic sibling rebind. Rebind clears the
  connect-lifetime HID field-ordering graph after the low-level transport has
  stopped, so the next `hid_hw_start()` rebuilds it without accumulating stale
  allocations. The separate wired and AES/receiver automatic fixtures passed
  their complete hardware runs for the original seven IDs. The focused
  eleven-ID Intuos matrix also passed with 29 balanced input-node lifetimes,
  `oom=0`, and terminal `f15, f10`. Its family captures verify exact-ID
  selection and reused parser-family/mode/input smoke, but are not byte-exact
  retail descriptors for every model. Bluetooth,
  ExpressKey Remote, bootloader, I2C, PCI, and every other product ID remain
  gated. Receiver lookup can resolve any child PID already in the selected
  table; hardware receiver coverage is still limited to child `056a:0027`.
- CTH padding-only report IDs 2 and 3 make pinned
  `hid_report_process_ordering()` request zero bytes. Linux returns
  `ZERO_SIZE_PTR` and later accepts that sentinel in `kfree()`. The former
  FreeRTOS shim returned `NULL` and incremented the global OOM counter twice
  per CTH attach even though no allocation was required. The compatibility
  `kmalloc()`/`kzalloc()`/`kcalloc()` and `kfree()` contract now preserves the
  Linux sentinel behavior. The exact rerun kept every heap snapshot at
  `oom=0`.
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
  `hid-multitouch`, ff-core, evdev, and the firmware workqueue. The audited
  Stadia `FF_RUMBLE` driver and `ff-memless` helper are retained but unlinked
  until firmware has a product client for them; broader gaming FF drivers
  remain deferred. The bounded firmware hiddev proxy remains an unlinked
  source; no active HID claims it.
- Haptic effect storage is bounded to five firmware application slots rather
  than Linux's 96 userspace slots. Each slot still owns
  the upstream per-effect report snapshot, but unused slots no longer consume
  the shared probe heap.
- Post-probe evdev publication snapshots `FF_HAPTIC` support. KeyD preloads the
  five standard waveform IDs once per device, keeps their ff-core IDs local to
  that device lifetime, and receives layout feedback as a virtual
  `DEVMON_HAPTIC` request through the same devmon queue used for host LEDs.
  Disconnect drops the local map and reconnect uploads fresh IDs; no global
  effect ID crosses device lifetimes.
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

- 2026-07-30: the AES/receiver Wacom checkpoint used exact host UF2 SHA256
  `8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8`
  and emulator UF2 SHA256
  `8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48`.
  The terminal alert sequence completed the AES, receiver, and success phases
  with no failure marker or host `ERR`. Four `056a:5048` attachments produced
  eight balanced Pen/Finger lifetimes. Four physical `056a:0084` attachments
  produced four balanced dynamic `056a:0027 (WL)` Pen/Pad/Finger graphs across
  pair, unpair, re-pair, held teardown/rebind control, physical disconnect,
  and final recovery.
  All 47 heap snapshots reported `oom=0`; terminal physical removal returned
  to the established 60,496/60,752-byte plateaus. Minimum-ever free heap was
  9,464 B. Minimum task watermarks were TinyUSB 265, KeyD 658, async 389,
  work 202, timer 332, lifecycle 182, and report 854 words. Four
  `EVDEV_BATCH_CAP` warnings and the
  unsupported evdev-code/event messages are the existing Linux-to-KeyD
  boundary; no `EVDEV_INPUT_DROP` or host transport error appeared.
  Production logs do not expose exact power-snapshot values/order. The fixture
  does not wait for the real 30-minute AES battery expiry, and its receiver
  child `0027` is a selected active profile. Captured child `033b` is selected
  by the current Intuos table, but that older artifact did not exercise it.
- 2026-07-30: the five-profile wired Wacom checkpoint used exact host UF2
  SHA256
  `4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5`
  and emulator UF2 SHA256
  `037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b`.
  The host log completed
  `f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10` with no `f12`,
  host `ERR`, transfer failure, timeout, or `EVDEV_INPUT_DROP`. Across 23
  physical Wacom attachments, all 46 published Pen/Pad/Finger nodes had
  matching removals. Nine `HID_IGNORED` warnings corresponded to the six
  CTL-472 and three CTL-672 ghost interfaces; nine `EVDEV_BATCH_CAP` warnings
  corresponded to the four CTH and five PTH touch inputs and did not produce a
  recorded input drop.
  All 115 heap snapshots reported `oom=0`, confirming that Linux
  `ZERO_SIZE_PTR` handling removed the former two false OOM increments for
  each CTH attach. Minimum-ever free heap was 7,568 B. Minimum task watermarks
  were TinyUSB 265, KeyD 658, async 389, work 198, timer 348, lifecycle 218,
  and report 854 words.
  The successful run intentionally contained no value-level `POWER` logger.
  Five PTH generations did not accumulate queue storage: after one immediate
  disconnect temporarily left a 60,496-byte plateau, later cleanup returned to
  60,752 B. This is indirect evidence for the normal UI-consumed `REMOVED`
  path, not verification of `present`/`status`/`capacity` snapshot values or
  cleanup when the UI task/timer fails to start.
- 2026-07-30: the Linux-shaped input/devres and evdev identity correction
  reran against the complete 32-reconnect Wacom artifact. The host log showed
  36 `056a:037a` Pen adds and removes, 36 expected ghost-interface
  `HID_IGNORED` results, and `f1, f2, f3, f4, f10` in order with no `f12` or
  host `ERR`. All 83 heap snapshots reported `oom=0`; repeated Wacom removal
  snapshots stabilized at `free/largest/blocks=60736/36032/7`. Minimum-ever
  heap was 7,856 B and minimum task watermarks were TinyUSB 265, KeyD 658,
  async 389, work 205, timer 348, lifecycle 222, and report 870 words. The
  published VID:PID proves the opaque-driver-data identity correction was
  active; repeated teardown covers the Wacom-reachable two-resource
  managed-input lifetime.
- 2026-07-29: wired Wacom CTL-472 passed with exact no-PIO host UF2 SHA256
  `bdf6ab6ce3bfbe1ed81abb4dcfb9183030d597f92e4e9d301bae8f474a436737`
  and 32-reconnect emulator UF2 SHA256
  `614401701850d5bbea0dde53ce005de9d0a76aacddf3bda5112a08c4b2c9048e`.
  The host log completed `f1, f2, f3, f4, f10` with no `f12`, all 36 Pen
  add/remove generations, and 36 expected ghost-interface
  `WARN: HID_IGNORED` results. No `ERR` or other warning appeared. Repeated
  removals returned to `free/largest/blocks=60728/36192/7`, minimum-ever heap
  was 7,848 B, every snapshot had `oom=0`, and minimum stack watermarks were
  TinyUSB 265, KeyD 658, async 389, work 205, timer 348, lifecycle 222, and
  report 870 words. This covers Wacom-specific pre-deadline and held-callback
  disconnects; generic promotion-window, simultaneous-cancel,
  callback-requeue, destroy-with-delayed, and tick-wrap branches remain static
  audit results.
- 2026-07-24: the direct HID++ 1.0/2.0 diagnostic checkpoint used exact host
  UF2 SHA256
  `9577a3e2820e99615b62e6535a1c01fbd403546c3bad233d9834a42f0dc88902`
  and emulator UF2 SHA256
  `2a0ff72e47fb4a8bff7e46a2550b69b5f58f0be3bb8ee91eb2e4d6d837a8ca4b`.
  Two complete runs covered the ordinary RAP/FAP reply, BUSY retry, protocol
  status, three-attempt timeout, pending-response disconnect, fresh-generation
  reconnect, and generic input through terminal `f10`; neither emitted the
  `f12` fallback. The first run returned to
  `free/largest/blocks=60832/53384/4` after every removal. The second kept
  `free=60864` and `blocks=8`, with `largest=52960` stable after its first
  warm-up removal. Both had `oom=0`; the minimum host-work watermark was 191
  words. The reported duplicate-GPIO validation was intentional and outside
  this USB result.
- 2026-07-24: removing that checkpoint's fixed HID++ trace strings, BUSY trace
  state, and otherwise unused RAP/FAP probe produces production-clean host UF2
  SHA256
  `a79e4385cec2987571226273951b492276e0275f495ebdc598bce2d8bba8498b`
  (`text=517376`, `data=788`, `bss=245088`). Matching emulator UF2 SHA256
  `31aa4485df67432e0d146a2d8fda3b46e93d2d4b70daab7e42e8016408a40149`
  validates the remaining protocol ping, BUSY retry on that ping, status,
  timeout, cancel, reconnect, and input sequence internally. The host CDC
  criterion is now only `f1`, `f2`, `f3`, `f4`, `f5`, `f6`, then terminal
  `f10`. The user accepted this reproducibly built pair as post-test cleanup
  without another hardware run: the exact hardware evidence remains attached
  to the diagnostic pair above, while the wait/reply/cancel implementation,
  `.data`, `.bss`, structure sizes, and dynamic allocation are unchanged.
- 2026-07-23: repeated hot-plug and cold-start runs completed all three haptic
  transport profiles: numbered interrupt OUT,
  unnumbered report ID 0 interrupt OUT, and unnumbered report ID 0 EP0
  fallback. The run covered five-slot playback, same-ID replay, replacement,
  erase/re-upload, HOST/DEVICE mode changes, queued-work unplug, remove, and
  reconnect. The same image passed USB Magic Trackpad 2 mode SET, all four
  interfaces, native low-contact/release/button parsing, and reconnect.
  Minimum heap was 11,368 B on cold start and 11,376 B on hot plug; complete
  removal repeatedly returned to 60,848 B free with `oom=0`.
- 2026-07-22: before the audited Stadia/`ff-memless` pair was returned to the
  deferred CMake set, a targeted two-Pico `FF_RUMBLE` test validated it. The
  temporary linked/instrumented host image had SHA256
  `8b188a595689102872e65ff7529ad3bbef3cb21b1d026e3378498688ba04bfbf`
  (`text=520188`, `data=788`, `bss=245100`); emulator commit `f8f9a38` image
  SHA256 was
  `499f249c748e7f5eb82e791ac654c7a03e0c20dae1e96050476c568a8b9a1ac3`.
  Two complete cycles separated by one reconnect observed upload, start, the
  300-ms `ff-memless` timer stop, replay of the same effect ID, unplug while
  that exact Stadia work was running, and clean removal. The post-remove heap
  returned to 60,816 B in both cycles with `oom=0`.
- 2026-07-22: the teardown-result fix, exact UF2 SHA256
  `db3ecf506b16471149ba45c0b7ff8199983e0d03c05fa3de8d3cd8079659a242`,
  is also the current rebuilt image after returning Stadia/ff-memless to the
  deferred CMake set. It passed repeated automatic ELECOM, Kensington, Topre,
  EVision, and
  `cafe:1005` long-configuration cycles. Input continued and the former false
  `HID_TEARDOWN_WAIT` diagnostic disappeared; the log contained no `ERR` or
  `WARN`. Removal repeatedly returned to `free=60784..60800`, minimum observed
  heap was 39,872 B, and `oom=0`. Host watermarks remained at least 265 words
  for TinyUSB, 410 for async, 348 for work/timer, 226 for lifecycle, and 862
  for report.
- 2026-07-22: the combined simplification image, exact UF2 SHA256
  `eabe0f7d200b51ccd0272303565391395e397b0885653c6e96e23c3b6c82087d`,
  completed repeated automatic ELECOM, Kensington, Topre, EVision, and
  `cafe:1005` long-configuration cycles. Input continued, removals returned to
  a stable `free=60800..60816` plateau, the minimum observed heap was 39,888 B,
  and `oom=0`. Every interface removal emitted the misleading
  `ERR: HID_TEARDOWN_WAIT`: the fence had completed, but its common wait helper
  returned `-ENODEV` because `transport_stopping` was true. The following
  dirty candidate treats that required teardown state as success and is
  hardware-verified above.
- 2026-07-22: superseded, build-only combined dirty audit image, UF2 SHA256
  `0a7ff16341260c7d78300989142b30fecb30b8314c0b6316f2a7a353a3586ba7`,
  clean-builds with `text=519612`, `data=788`, and `bss=245320`. It combines
  the later lifecycle/reset/queue-lifetime and Linux-diff fixes, event-driven
  cache-epoch admission, first-interrupt endpoint selection, and the
  then-current compatibility lifetime changes, the USB-only upstream Magic
  Mouse 2 / Trackpad 2
  driver, and the already-tested long-configuration path. The Apple path is
  build-audited but still needs its dedicated hardware fixture.
  No hardware verdict was recorded for this exact image. It is retained only
  as an unverified historical build and must not be treated as the current
  artifact or promoted into a hardware checkpoint.
- 2026-07-22: the long-configuration/retry checkpoint replaces TinyUSB's last blocking
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
- 2026-07-31: the Microsoft wired-USB WIP links the complete pinned
  driver with exactly 14 non-gaming USB IDs. SideWinder, Bluetooth, and FF
  remain compile-gated with matching special-driver gates. Its first complete
  hardware run reached every profile but demonstrated cross-device ownership
  in upstream's function-static F14--F18 state. The current per-device
  candidate builds as `text/data/bss=596296/788/245312`, with
  `__bss_end__=0x2003fe88`, 376 bytes of main-bank headroom, and host UF2
  SHA-256
  `f809966886aa4bdaf42c694055ea08b9313d4dbd1120f1a96f0cebd8ba71fd48`.
  The exact fixed-host/emulator pair passed on hardware: the interleaved Office
  reports produced F14 down, F15 down, F14 up, F15 up before either interface
  removal; all representative profiles completed; removal free heap returned
  to 60752 bytes; and terminal `f10` arrived with `oom=0` and no host `ERR`.
- 2026-07-31: the hardware-verified Apple external-USB stage links the complete
  pinned driver with exactly 18 wired keyboard/Mighty Mouse IDs. Bluetooth,
  internal/legacy, trackpad-only, Touch Bar, and backlight-only rows remain
  gated with matching special-driver gates. The stage builds as
  `text/data/bss=600920/788/245360`, with `__bss_end__=0x2003feb8`, 328 bytes
  of main-bank headroom, and host UF2 SHA-256
  `e55bc54d78acc77e14548d6f7dfc6f816fe8d00f712803c7aef9fb977cc5f096`.
  The compact four-profile emulator builds as
  `text/data/bss=60296/0/254452`, with UF2 SHA-256
  `6e6ba43c7efad0677a372079f079701e5fa51f8cb65c30f4321a3fbaafe72255`.
  The exact pair passed on hardware. Aluminum Fn/F-key translation, Mighty
  Mouse button handling, both Magic Keyboard mapping tables, immediate
  and 60-second battery GET_REPORT, pre-deadline timer teardown, disconnect
  with the delayed GET queued, and a fresh GET after reconnect all completed
  before terminal `f10`. There was no `f12` or host `ERR`; `oom=0`, the Magic
  removal plateau remained 60496 bytes through reconnect, and every task
  watermark remained nonzero. The fixture also delivered positive and negative
  Mighty Mouse Z reports, but production CDC has no REL_HWHEEL marker, so the
  inversion remains source-audited rather than directly observed.
- 2026-07-31: the unverified ordinary-Logitech-receiver WIP expands the
  existing pinned DJ table from `046d:c52b` to `c52b/c532/c52f/c534`,
  preserving the pinned DJ, mouse-only, and HID++ receiver types. Gaming,
  Lightspeed/Powerplay, legacy 27 MHz, Bluetooth-proxy, and Dinovo rows remain
  gated. The cumulative candidate builds as
  `text/data/bss=600968/788/245360`, with `__bss_end__=0x2003feb8`, 328 bytes
  of main-bank headroom, and host UF2 SHA-256
  `c222b8b396e88aa402171cb9247728a1574477e673b986de0ec633c68bfa2d5a`.
  The 48-byte text increase is the three selected table rows; mutable static
  state is unchanged. These are build/audit facts only; focused emulator and
  hardware verdicts are pending.
- 2026-07-31: the same unverified hardware stage now also imports complete
  pinned `hid-lenovo.c` and selects only external USB
  `17ef:6009/6047/60ee`. Bluetooth, I2C, ScrollPoint, dock, tablet, and Linux
  audio LED-class paths remain gated; Legion is a separate unlinked family.
  The combined candidate builds
  as `text/data/bss=603504/788/245408`, with
  `__bss_end__=0x2003fee8`, 280 bytes of main-bank headroom, and host UF2
  SHA-256
  `269e20807a5dcac4acbe8a9bf6fcd782a5633e4a193f7373cd4cb136078d918f`.
  The active Lenovo state is 28 bytes per configured TrackPoint interface and
  the static increase includes one 48-byte builtin-driver runtime. The compact
  combined emulator builds as `text/data/bss=62244/0/254452`, with UF2
  SHA-256
  `1f8903038ff59a2f9f849b3ea85b8351fc1e98c631854674efd131bd274721f6`.
  The first combined run failed in the broad c532 phase before child
  publication. The rebuilt emulator emits a `1` through `7` after `f12,a` to
  identify the exact c532 predicate; that diagnostic artifact has no hardware
  verdict yet.
- 2026-07-31: the corrected combined fixture subsequently reached terminal
  `f10` at temporary `HID_MAX_FIELDS=64`, `HID_MAX_USAGES=256`, with exact
  `c532/c52f/c534` receiver exchanges, Lenovo `6009/6047/60ee`, balanced
  removal, `oom=0`, and nonzero watermarks. This proves the staged logic, not
  retained-policy capacity. At 675, each `c52f/c534` physical-plus-virtual
  wide-field pair needs 26,080 bytes more than at 256 and cannot coexist in the
  RP2040 heap. The full `60ee` Consumer field needs 13,408 bytes more than its
  256-usage run, which left 11,152 bytes free. Their upstream rows are retained
  but disabled for the RP2040 build.
- 2026-07-31: the focused external wired Wacom Intuos matrix used host UF2
  SHA256
  `1bd3124acf0d3058bf798df8b59cc796aebc43cca09a36cf39ea4a96b8f94fed`
  and emulator UF2 SHA256
  `93f034b64d9eec4537477acfb9763d3cbe0590017338f5a561619b74d5da2fac`.
  Eleven selected PID profiles `0302/0303/030e/0314/0315/0317/0323/033b/033c/
  033d/033e` published and removed 29 expected Pen/Pad/Finger nodes, produced
  eleven expected rejected compatibility-interface `HID_IGNORED` and seven
  bounded touch-batch warnings, and completed with `f15, f10`, no `f12`, no
  host `ERR`, and `oom=0` in all 63 heap snapshots. Exact-ID feature-usage
  compaction for the three Intuos Pro IDs preserved every report value while
  removing duplicate
  vendor-usage metadata from the captured `0317` family descriptor reused for
  `0314/0315`: the Pro Pen snapshot improved from 10,688 to 30,696 B free,
  allowing Finger probe to complete at 17,320 B free. Minimum-ever heap
  was 12,832 B; terminal profile removals returned to 60,736 B. Minimum task
  watermarks were TinyUSB 265, KeyD 658, async 384, work 207, timer 348,
  lifecycle 212, and report 859 words. Representative battery report IN
  transfers for `0302`, `0314`, and `033b` completed on the emulator side;
  host parser/work enqueue and detached power-snapshot values remain
  unobserved.
- 2026-07-31: the focused Deco 01 original / Parblo A610 Pro matrix used host
  UF2 SHA256
  `a9ba49cf53d0ce0ba44679d85b11bca301809094cb7705c1721f8e93a12cbd71`
  and emulator UF2 SHA256
  `373df86d428d9c528dbf642b9a2931d38b5c0f8e584f218695d7f14392b65eca`
  from `device/uclogic-deco-parblo` commit `c843295`. Deco `28bd:0042`
  completed its v1 raw string-100/Pen/eight-key Pad path without an OUT probe;
  Parblo `28bd:1903` completed the endpoint-`0x03` OUT probe before raw string
  100, then Mouse/Pen/nine-key Pad/Dial input. One full and one same-PID
  reconnect generation of each profile produced ten balanced target input
  lifetimes and final `f15, f10`, with no `f12`, `HID_REPORT_SKIP`, or host
  `ERR`. All 22 heap snapshots reported `oom=0`; minimum-ever free heap was
  46,688 B, and minimum task watermarks were TinyUSB 265, KeyD 658, async 389,
  work 346, timer 348, lifecycle 85, and report 854 words.

## Current Driver Boundary

- The linked driver set can use upstream `hid_hw_request()` followed by
  `hid_hw_wait()`, including returned feature data during probe. The bounded
  firmware queue still rejects overload instead of attempting Linux's much
  larger control/output FIFOs.
- The hardware-tested direct-HID++ request/reply foundation uses synchronous
  `hid_hw_raw_request()` plus `.raw_event` response matching. Its one
  send-mutex-serialized waiter has a durable predicate, timeout, report-task
  wake, and exact-generation disconnect cancellation. Probe replies take the
  port-only raw-event-only ingress while ordinary input stays behind final
  activation.
- The current host selects direct USB `046d:c08d` and
  `046d:c08a`. It reaches upstream pre-connect HID++ 2.0 name and
  unit-ID/serial discovery, then the direct battery paths. The final name
  reaches the KeyD device-add log with VID:PID; the unit ID remains in Linux
  HID/input state and is not copied into `port_input_dev`. Battery values cross
  a reduced `power_supply` boundary through one coalescing length-one value
  queue per supply. The queues belong to a separate devmon power QueueSet; the
  UI task reads and currently ignores detached `ADDED`/`CHANGED` snapshots,
  then removes the personal queue after terminal `REMOVED`. Power events do
  not enter KeyD or the ordinary devmon queue, and UI presentation is deferred.
- The Microsoft stage selects USB
  `045e:0048/009d/00b4/00db/00dc/00e3/00f9/0713/071d/0730/0732/0750/076c/
  07da`. It exercises pinned report-fixup, mapping/mapped, event, and
  parse/start/remove paths plus the narrow per-device fix for upstream's
  function-static F14--F18 release ownership. No Microsoft work, timer, FF, or
  request object is reachable. The first complete run demonstrated the
  cross-device bug; the exact per-device fixed-host pair passed on 2026-07-31.
- The Apple stage selects USB
  `05ac:0304/021d/021e/021f/0220/0221/0222/024f/0250/0251/0267/026c/029a/
  029c/029f/0320/0321/0322`. It reaches pinned Fn/media/navigation mapping,
  Mighty Mouse button/HWheel quirks, Magic Keyboard battery descriptor fixup,
  immediate GET_REPORT, and the 60-second battery timer. Removal keeps the
  pinned synchronous timer-delete-before-transport-stop lifetime. The
  Touch-ID-named IDs are keyboard-only in this scope.
- The selected UC-Logic table now also includes exact Deco 01 original
  `28bd:0042` and Parblo A610 Pro `28bd:1903`. They reuse the pinned v1 and
  UGEE-v2 string/request/input paths without a new transport primitive,
  callback, work item, or mutable static state. Their focused same-PID
  reconnect matrix passed at the retained `64/675` parser policy. Pad keys
  reach KeyD; pen absolute/tool values and the Parblo Dial remain downstream
  unsupported-event boundaries.
- HID core now restores the upstream-shaped HIDRAW
  connect/claim/report/disconnect lifecycle. The reduced object stores no
  reports and has no subscriber, VFS, file descriptor, ioctl, or device-node
  API.
- The first reduced single-M705 checkpoint was superseded by the
  pinned-upstream-shaped `hid-logitech-dj.c` port. Receivers `046d:c52b/c532`
  are active. The pinned `c52f/c534` mouse-only and HID++ rows remain adjacent
  but are disabled on RP2040 because their physical and virtual Consumer fields
  exceed the heap at 675 usages. Gaming, Lightspeed/Powerplay, 27 MHz,
  Bluetooth-proxy, and Dinovo receivers remain compile-gated. The driver
  retains multiple virtual-child slots,
  standard mouse/keyboard/Consumer/power/media descriptors, HID++ descriptors,
  and raw-request routing through the physical receiver. The selected `c532`
  exact retained-policy regression remains pending; `c52f/c534` passed only
  the temporary `64/256` logic check.
- The Lenovo stage selects external USB `17ef:6009/6047`; its full `60ee` row is
  retained but memory-gated on RP2040. It retains pinned
  report validation, Button-16/Fn/vendor mappings, middle-button wheel
  arbitration, async `6009` feature SET_REPORT, and the sequential synchronous
  `6047/60ee` configuration requests. Bluetooth, I2C, ScrollPoint, dock,
  tablet, and audio LED-class paths remain gated; Legion is a separate
  unlinked family. Sysfs publication is a nonfatal reduced no-op. `KEY_FN_ESC`
  currently stops at the downstream
  KeyD mapping boundary rather than being silently claimed as remappable.
- The full automatic sequence passed with temporary RP2040 parser limits
  `HID_MAX_FIELDS=8`, `HID_MAX_USAGES=256`. At the retained `64/675` policy,
  standalone M705 and ordinary DJ keyboard children work both separately and
  simultaneously; the combined graph leaves `free=5160`, `min=4008`, and no
  new OOM. The complete HID++ eQuad keyboard connection profile reaches the
  RP2040 heap boundary as a standalone child and adds the run's only OOM count.
  The earlier two-OOM result used 256 fields. Heap values are in
  `hid-emulator-coverage.md` and `pio-usb-memory.md`.
- The corrected Logitech/Lenovo fixture separately passed all six new profiles
  at temporary `64/256`. The selected `c532/6009/6047` exact retained-policy
  regression remains pending; `c52f/c534/60ee` are not selected in the RP2040
  build after the measured capacity result.
- FF, high-resolution wheel, vendor keys, direct-touchpad subclasses, and
  broader real-device DJ product coverage remain outside that DJ candidate.
  The original seven Wacom IDs collectively have parser, delayed-work, timer,
  LED, arbitration, battery, receiver-rebind, reconnect, and balanced-teardown
  coverage across their focused fixtures. The later eleven-ID Intuos fixture
  adds exact-ID selection, mode exchange, Pro LED initialization, Pen/Pad plus
  seven Finger smoke paths, ten different-PID detach/attach transitions,
  balanced teardown, and the measured memory result. It uses family captures
  and does not add pending/running-work cancellation, same-PID reconnect,
  Touch Ring semantics, explicit arbitration, receiver `0084 -> 033b`, or
  exact power-snapshot coverage.
- Drivers that need generic USB URBs, interrupt-IN synchronous messages,
  HID requests beyond 16 KiB, USB messages beyond the 16-bit wire length,
  broad Linux subsystem state, or unaudited callback behavior stay out of
  `CMakeLists.txt`.
- `usb_host/deferred-hid-drivers.md` records the current deferred boundary and
  examples.

## Next Checks

- Correct the reachable UC-Logic combined-descriptor ownership leak after a
  successful `uclogic_params_get_desc()` followed by failing `hid_parse()` or
  `hid_hw_start()`. Exercise it with a temporary deterministic host fault and
  repeated cleanup plateaus, then remove the fault hook before committing.
  Ordinary successful Deco/Parblo enumeration does not cover this branch.
- Recheck active `c532` and Lenovo `6009/6047` at retained `64/675`. The
  unchanged combined emulator still presents memory-gated `c52f/c534/60ee`, so
  its stop at the first gated profile is not a passing automatic sequence and
  must not be relabelled as one. Generic HID sends no Nano startup writes, so
  the fixture stops at `c52f` and does not reach `c534/60ee`. Do not change that
  emulator without explicit permission.
- Keep HIDRAW lifecycle-only until a concrete useful USB consumer defines the
  required bounded report/subscriber contract. HIDDEV/VFS is outside the
  current roadmap.
- Run the existing `device/rapoo-2_4g-receiver` fixture against the exact
  current host as the second managed extra-input regression. Record both extra
  inputs, ordinary input, disconnect/reconnect, removal plateaus, stack
  watermarks, and `oom`/`ERR` results.
- Keep the generic delayed-work promotion-window, simultaneous synchronous
  cancel, callback self-requeue, queue-destruction, and tick-wrap branches as
  static audit results until a deterministic fixture exercises each one.
- If exact power publication becomes part of the product claim, add a bounded
  task-context consumer oracle for `ADDED`/`CHANGED` values and a deterministic
  startup-failure case. The current Wacom run proves only ordinary queue cleanup
  indirectly through recovered heap plateaus; it does not prove
  `present`/`status`/`capacity` values or cleanup without the UI consumer.
- Measure broader real-device DJ combinations and the same complete HID++ eQuad
  keyboard profile on the selected larger-RAM target. Do not transfer the
  temporary `8/256` RP2040 verdict to the retained `64/675` configuration.
- Keep verifying the linked drivers with targeted emulators or matching
  hardware before claiming hardware coverage.
- The normal USB Magic Trackpad 2 path is hardware-verified on all four HID
  interfaces: one mouse and three non-mouse. The probe-time `{ 0x02, 0x01 }`
  mode SET, native report ID `0x02`, one/two-contact input, repeated reconnect,
  and stable removal heap plateau passed on 2026-07-23. Accepted `-EIO`, unplug
  during SET, contact ID 32, and multi-contact queue saturation still need
  deterministic fault injection.
  `CFG_TUH_HID=4` admits that standalone device exactly; Trackpad plus another
  HID interface is a separate capacity/RAM configuration, not part of this
  certification.
  Upstream returns early for USB Magic Mouse 2, so report `0x12` must not be
  described as proof of a wired mode-switch path that Linux itself does not run.
- Exercise cache backpressure with at least two fast remove/replug epochs before
  lifecycle has released the earlier objects. The later ATTACH must resume
  without `HID_USB_DEV_ALLOC_FAIL`, `HID_ATTACH_OVERFLOW`, or a stuck reset
  gate, and the heap must return to its prior plateau.
- Add one HID fixture with `bulk IN, interrupt IN, bulk OUT, interrupt OUT` and
  another with two interrupt endpoints per direction. Verify that only the
  first interrupt pair opens and both input/output still work; ordinary haptic
  coverage does not prove this endpoint-filter branch.
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
- The haptic lifecycle fixture hardware-verified interrupt OUT and EP0 fallback
  output, feature control traffic, input, and repeated unplug/replug without
  `HID_REPORT_OUT_FAIL`, `HID_XFER_TO`, or `HID_CTRL_DISPATCH_FAIL`. The
  independent boot-keyboard startup-NumLock observation remains: a keyboard
  without interrupt OUT must receive it over EP0, while one with interrupt OUT
  must use that endpoint.
  Historical queue/slot
  sizes belong to their dated checkpoints above; use
  [`pio-usb-memory.md`](pio-usb-memory.md) for current linked-image and
  allocation accounting.
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
