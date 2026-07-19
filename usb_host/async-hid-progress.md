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
  A zero-data SET_IDLE uses the same transport and keeps the upstream
  synchronous ll-driver return contract by waiting only in the caller task.
- Normal GET_REPORT receive lengths follow upstream `hid_submit_ctrl()`: they
  are rounded to EP0 max-packet size and capped by the fixed transport buffer,
  while raw GET_REPORT keeps its caller-supplied length. Only the actual bytes
  reported by TinyUSB are handed to the Linux HID parser.
- The ll-driver output paths now follow upstream USB HID routing.
  `.request(HID_REQ_SET_REPORT)` sends an `HID_OUTPUT_REPORT` over interrupt
  OUT when the interface exposes that endpoint and otherwise uses EP0
  SET_REPORT. FEATURE reports and `.raw_request()` remain control-only, while
  `.output_report()` remains interrupt-only.
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
- Deferred input-report delivery, firmware workqueue, and firmware timer
  bridges are present for the currently linked driver set.
- The standard HID Haptics path is linked through `hid-haptic`,
  `hid-multitouch`, ff-core, evdev, and the firmware workqueue. Broader gaming
  FF drivers remain deferred. Hiddev remains in its bounded firmware-proxy
  form.

## Manual Test Notes

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
- ErgoType Pico host now logs the async Razer raw SET_REPORT path and then
  receives the same macro usage as unsupported evdev code `0x290`.
- The remaining `0x290` handling is consumer/keyd-side work, not an async HID
  transport failure.
- Callback-side mount and descriptor debug logs were removed. Sticky callback
  fault bits are drained and logged by the lifecycle task; async submit and raw
  request status logs remain task-side.

## Current Driver Boundary

- The linked driver set can use upstream `hid_hw_request()` followed by
  `hid_hw_wait()`, including returned feature data during probe. The bounded
  firmware queue still rejects overload instead of attempting Linux's much
  larger control/output FIFOs.
- Drivers that need generic USB URBs/control helpers, broad Linux subsystem
  state, or unaudited callback behavior stay out of `CMakeLists.txt`.
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
  endpoint. Keep these as separate observations.
- Verify the direct-control checkpoint with the haptic-touchpad or hi-res-wheel
  fixture: descriptor pre-probe, feature GET/SET, input events, and unplug/replug
  must all complete without `HID_SUBMIT_TO`, `HID_XFER_TO`, or parser-queue
  errors. Record post-attachment heap; the enlarged report queue costs 400 B of
  startup heap and its reconcile table adds 32 B of static RAM.
- Exercise SET_IDLE on a path that actually calls the ll-driver `.idle` hook;
  enumeration-time TinyUSB SET_IDLE happens before this transport is mounted and
  is not proof of the new request path.
- Extend the serialized transport explicitly before importing drivers that need
  generic `usb_control_msg()`, URBs, or larger request/response protocols.
