# Async HID Progress

## Current Scope

- `usb_host/usbhid.c` owns a bounded USB-device cache and report-descriptor
  ingress pool allocated before the TinyUSB host starts. TinyUSB callbacks do
  not allocate, wait, or log; they publish a snapshot and wake the lifecycle
  task.
- HID interface mount no longer has to guess whether USB strings or
  `bcdDevice` are ready. It copies TinyUSB's ephemeral report descriptor into
  bounded ingress storage, and `usbhid_probe()` runs in the lifecycle task only
  after the USB device pre-probe sequence has completed.
- The pre-probe sequence currently fetches the device descriptor, LANGID,
  product string, manufacturer string, and serial string. That makes
  `hid->version`, `hid->name`, and `hid->uniq` available before
  `hid_ignore()`, `hid_lookup_quirk()`, and driver probe.
- The raw interface snapshot is still captured from TinyUSB mount data before
  TinyUSB normalizes the HID boot/report protocol field. That is functional
  metadata: it feeds Linux-like `hid->type` decisions such as the Razer mouse
  interface path.
- `usb_host/hid_async.c` owns serialized TinyUSB host submits for HID control
  and interrupt-output requests. TinyUSB callbacks only enqueue completions and
  never run Linux driver continuations directly.
- HID EP0 GET_REPORT/SET_REPORT now uses direct asynchronous
  `tuh_control_xfer()` requests submitted from the TinyUSB host owner. Each
  transfer carries the request serial in `user_data`, and completion preserves
  TinyUSB's real result and actual data length. Successful short transfers are
  no longer confused with failures; STALL and timeout remain distinguishable.
  Task-context `usb_control_msg()` now uses a fixed metadata-slot scheduler and
  a physical-device epoch lease. Interface requests also carry their live HID
  owner, so interface stop cancels a blocked caller immediately instead of
  leaving it to a physical-device timeout. SET_IDLE and upstream-shaped raw
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
  the nine 257-byte inline payloads shrinks `hid_async_request` from 312 B to
  60 B and its slot from 340 B to 88 B; one 257-byte shared pre-probe scratch
  remains in the startup allocation. Generic control uses USB's native 16-bit
  length, while HID `.request()` remains bounded by `HID_MAX_BUFFER_SIZE`.
- `usbhid_start()` now clears the NumLock output field on boot keyboards and
  submits the whole output report through that same `.request()` route,
  matching upstream startup behavior.
- Physical detach now closes the async generation and publishes a bounded
  cache tombstone without waiting in the TinyUSB callback. The app-driver close
  path covers hubs, for which TinyUSB does not issue the common unmount callback.
  Parent cache entries remain retired until their child subtree and HID objects
  are gone; fast reuse of the same device address starts a distinct generation.
  Lifecycle flags and descriptor slots remain authoritative if the one-entry
  wake queue is already full.
- `hid_hw_request()` now matches the upstream queue-and-return contract.
  Successful GET_REPORT completion enters the report queue, and `hid_hw_wait()`
  drains through the end of parsing, so callers cannot observe
  a transport-complete/parser-pending false idle. During probe, the waiting
  lifecycle task consumes only its completed control GET under the lock it
  already owns. Interrupt-IN therefore stays gated until probe finishes, while
  returned feature fields are preserved instead of being lost to lock
  contention. Raw GET/SET and interrupt output keep their upstream synchronous
  return contract while using the same asynchronous TinyUSB owner underneath.
- The report executor reserves space for all four queued async requests plus
  the active request, so a second fast control GET is no longer discarded while
  an earlier result waits for parser ownership. Interrupt-IN close is reconciled
  in the TinyUSB owner: abort completion is drained for two SOFs before a raced
  reopen may arm a new receive.
- Interrupt IN now uses the first interrupt-IN endpoint directly, matching
  upstream endpoint selection and preserving TinyUSB's exact transfer result
  and actual length. The request length is the largest parsed INPUT report,
  including its report ID, capped at Linux's 16 KiB HID limit. Upstream's
  per-interface `inbuf` ownership is restored; task context allocates INPUT-only
  backing with one-packet minimum/tail padding required by the pinned PIO HCD.
  Four persistent transport slots retain only lifecycle/callback metadata;
  queued events borrow `inbuf` zero-copy, and the endpoint is rearmed only after
  that event is consumed. Physical unplug publishes a durable detach state;
  the report task queues one host-owner fence which completes only after
  TinyUSB's class/HCD close pass, so no callback allocates, waits, or frees the
  buffer. Successful payload is dropped while
  `ll_open_count` is zero, including `HID_QUIRK_ALWAYS_POLL`, as in upstream.
  Non-success payload never reaches the HID parser. STALL queues the standard
  endpoint `CLEAR_FEATURE(HALT)` request on the generic per-device EP0 lane;
  only successful completion lets the TinyUSB host owner reset the PIO endpoint
  toggle to DATA0 and rearm. FAILED/TIMEOUT follows upstream's
  13/26/52/104-ms delayed retry for about one second. Clear-halt failure or
  exhausted retry still parks the endpoint because this TinyUSB/PIO stack has
  no safe coordinated per-device reset/re-enumeration API.
- Deferred input-report delivery, firmware workqueue, and firmware timer
  bridges are present for the currently linked driver set.
- The standard HID Haptics path is linked through `hid-haptic`,
  `hid-multitouch`, ff-core, evdev, and the firmware workqueue. Broader gaming
  FF drivers remain deferred. Hiddev remains in its bounded firmware-proxy
  form.

## Manual Test Notes

- 2026-07-20: uncommitted dynamic interrupt-IN checkpoint, exact UF2 SHA256
  `13edb5b61ca9c57f8da59613eb65b05cb8944f78c4bfef74f6cc50357264bd37`,
  builds with `text=477972`, `data=660`, and `bss=243308`. It removes the
  fixed 64-byte RX ceiling and adds a post-HCD-close detach fence. It has not
  yet been tested on hardware; do not commit it before that pass.
- 2026-07-20: request buffer-ownership checkpoint `hid: give async requests explicit buffer ownership`, exact UF2 SHA256
  `290ed22778c8905c49d2006bcb719ada1c3e8147cca4e4c41c8744025bde14e8`,
  builds with `text=477444`, `data=916`, and `bss=243308`, and was reported
  working on hardware. It is the baseline for the uncommitted dynamic-IN step.
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
- Verify the bounded ingress/hub-retirement checkpoint with normal enumeration,
  haptic allocation, direct unplug/replug, and a hub subtree reconnect. Record
  free heap and the TinyUSB stack watermark; inspect the lifecycle watermark
  separately if the hardware pass exposes any stack symptom.
- Verify the output-routing/startup-LED checkpoint on hardware: a boot keyboard
  without interrupt OUT must receive the enumeration-time NumLock reset over
  EP0, and an OUTPUT request on an interface with interrupt OUT must use that
  endpoint. For the direct-OUT checkpoint also verify normal input after boot,
  unplug/replug, and haptic or LED output without `HID_REPORT_OUT_FAIL`,
  `HID_SUBMIT_TO`, or `HID_XFER_TO`. Keep these as separate observations.
- Verify the direct-control checkpoint with the haptic-touchpad or hi-res-wheel
  fixture: descriptor pre-probe, feature GET/SET, input events, and unplug/replug
  must all complete without `HID_SUBMIT_TO`, `HID_XFER_TO`, or parser-queue
  errors. Record post-attachment heap; the enlarged report queue costs 400 B of
  startup heap and its reconcile table adds 32 B of static RAM.
- Exercise SET_IDLE on a path that actually calls the ll-driver `.idle` hook;
  enumeration-time TinyUSB SET_IDLE happens before this transport is mounted and
  is not proof of the new request path.
- Verify direct interrupt IN with keyboard and pointer traffic, then repeated
  unplug/replug. The expected normal path has no `HID_RX_STALL`,
  `HID_RX_XFER_FAIL`, `HID_RX_REARM_FAIL`, or `HID_REPORT_SKIP`. Record heap,
  largest free block, block count, and the TinyUSB stack watermark.
- Exercise recovery with a one-shot interrupt-IN STALL fixture. Expect one
  `HID_RX_STALL`, then `HID_CLEAR_HALT_OK`, followed by resumed input. Replug
  repeatedly and unplug while clear-halt is active. A normal TinyUSB NAK does
  not prove FAILED/TIMEOUT retry; that path needs a lower-level no-response or
  bad-packet injector. The deliberate remaining deviation is endpoint parking
  after retry exhaustion instead of Linux `usb_queue_reset_device()`.
- Extend the serialized transport explicitly before importing drivers that need
  URBs, interrupt-IN synchronous messages, or request/response protocols beyond
  the native HID/USB length contracts.
