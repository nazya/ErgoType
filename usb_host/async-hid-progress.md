# Async HID Progress

## Current Scope

- `usb_host/usbhid.c` owns a bounded USB-device cache and fixed probe-identity
  slots allocated before the TinyUSB host starts. Its bounded
  application-driver ingest copies the ephemeral raw configuration stream;
  mount/unmount callbacks do not allocate, wait, log, or interpret that retained
  snapshot. HID mount captures only TinyUSB's instance-to-interface identity
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
  and interrupt-output requests. TinyUSB callbacks only enqueue completions and
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
  priority-inheritance mutex exposes accidental recursion. The next ownership
  steps move callback publications into the async, report, and lifecycle task
  queues so this shared lock can be partitioned rather than becoming permanent
  design.
  The separate task-only timer/workqueue bridges still contain 19 old critical
  regions and remain a later synchronization domain; they are not mixed into
  this transport mutex.
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
  second lookup: its fixed arm slot retains the exact HID, `inbuf`, device
  generation, and open revision while ownership moves atomically from `ARMED`
  to `QUEUED`. Pointer lifetime therefore no longer depends on current task
  affinity or priority ordering.
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
  the generic asynchronous control broker. Configuration descriptors remain
  separately limited by the enumeration scratch buffer. The firmware-
  only full device-descriptor refetch now retains pending HID metadata across
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
  endpoint callback atomically publishes result/length and `ARMED -> QUEUED`;
  the report task claims `QUEUED -> ACTIVE` round-robin and parses zero-copy.
  There is no interrupt-input queue or second HID lookup, and the endpoint is
  rearmed only after the slot returns to `STOPPED`. Every arm snapshots the
  open revision. Close/reopen aborts an old `ARMED` transfer through the same
  two-SOF fence as `usb_kill_urb()`; an already queued old completion is dropped
  and the current open is freshly armed. Physical unplug publishes a durable
  detach state. The report task queues one host-owner fence which completes only
  after TinyUSB's class/HCD close pass. No callback allocates, waits, or frees
  the buffer. Successful payload is dropped while
  `ll_open_count` is zero, including `HID_QUIRK_ALWAYS_POLL`, as in upstream.
  Non-success payload never reaches the HID parser. There is no port-only
  BOOT-mode gate; Linux usbhid relies on the USB reset-default Report protocol.
  STALL queues the standard
  endpoint `CLEAR_FEATURE(HALT)` request on the generic per-device EP0 lane;
  only successful completion lets the TinyUSB host owner reset the PIO endpoint
  toggle to DATA0 and rearm. Like an already running upstream `reset_work`, an
  active clear-halt finishes across HID close: success still resets the host
  toggle and a wire failure still publishes device reset; `report_wanted`
  gates only the later interrupt-IN rearm. Stop/unplug still cancels the old
  transport epoch. FAILED/TIMEOUT follows upstream's
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
  exact unique anchors, then generates a build-local copy with five audited
  deltas: the hub mount fence, a weak generation hook, direct root/hub
  enumeration helpers, exact-owner recovery for a lost EP0 completion, and the
  host-global control-IDLE publication.
  The installed SDK is never modified and any upstream source drift fails
  configuration for an explicit re-audit.
- The same compatibility generation pins `hid_host.c` and preserves its full
  `hidh_open()`, `hidh_set_config()`, and report-descriptor prefetch blocks
  commented beside their replacements. A bounded two-pass scanner accepts the
  HID descriptor in the current interface extras (including after endpoint[0]),
  opens no more than `bNumEndpoints`, and publishes the class slot only after
  endpoint success. Enumeration performs neither SET_IDLE nor SET_PROTOCOL and
  mounts without borrowing the shared enumeration buffer; mount publishes only
  its ephemeral class identity. Lifecycle builds the retained interface, then
  upstream-shaped `usbhid_parse()` sends Linux's SET_IDLE and owns the one
  exact-size descriptor request plus its retry/error semantics. TinyUSB retains
  Report-protocol metadata only to match the reset default.
- Fixed-slot task-side input-report delivery, the firmware workqueue, and the
  firmware timer bridges are present for the currently linked driver set.
- The standard HID Haptics path is linked through `hid-haptic`,
  `hid-multitouch`, ff-core, evdev, and the firmware workqueue. Broader gaming
  FF drivers remain deferred. Hiddev remains in its bounded firmware-proxy
  form.

## Manual Test Notes

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
- Exercise recovery with a one-shot interrupt-IN STALL fixture. Expect one
  `HID_RX_STALL`, then `HID_CLEAR_HALT_OK`, followed by resumed input. Replug
  repeatedly and unplug while clear-halt is active. A normal TinyUSB NAK does
  not prove FAILED/TIMEOUT retry; that path needs a lower-level no-response or
  bad-packet injector. Also force terminal clear-halt/protocol failure for both
  a root device and a hub child: expect `HID_RESET_Q`, complete old-epoch
  teardown/re-enumeration, then `HID_RESET_OK` and resumed input. Exercise
  unplug, parent-hub removal, address reuse, and a native same-port replug in
  each reset phase. The reset paths themselves are not yet hardware-verified.
- Extend the serialized transport explicitly before importing drivers that need
  URBs, interrupt-IN synchronous messages, or request/response protocols beyond
  the native HID/USB length contracts.
