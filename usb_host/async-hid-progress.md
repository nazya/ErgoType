# Async HID Progress

## Current Scope

- `usb_host/usbhid.c` now keeps a small USB-device cache by `dev_addr`.
  `tuh_mount_cb()` records cheap topology and VID/PID metadata, then queues a
  pre-probe control sequence through the HID async task.
- HID interface mount no longer has to guess whether USB strings or
  `bcdDevice` are ready. It stores pending HID probes, and `usbhid_probe()`
  runs only after the USB device pre-probe sequence has completed.
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
- `hid_hw_request()`, raw SET_REPORT, raw GET_REPORT building blocks,
  output-report submits, deferred input-report delivery, firmware workqueue,
  and firmware timer bridges are present for the currently linked driver set.
- The generic ff-core/evdev boundary remains for future haptic support, but no
  FF driver is active. Hiddev remains in its bounded firmware-proxy form.

## Manual Test Notes

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
- Current debug strings such as `HID_MOUNT_CB`, `HID_PRE_SUB_OK`,
  `HID_DEV_DESC_CB`, `HID_STR_DESC_CB`, and raw request status logs are
  intentionally still present for the next hardware passes.

## Current Driver Boundary

- The linked driver set is restricted to drivers that fit the current
  architecture: descriptor fixups, input mapping, simple probe/start paths,
  queue-only SET_REPORT paths, and small callbacks that do not need returned
  request data before probe can continue.
- Drivers that need returned GET_REPORT/control data, broad Linux subsystem
  state, or larger sync-over-async probe state machines stay out of
  `CMakeLists.txt`.
- `usb_host/deferred-hid-drivers.md` records the current deferred boundary and
  examples.

## Next Checks

- Keep verifying the linked drivers with targeted emulators or matching
  hardware before claiming hardware coverage.
- Remove or lower the temporary async debug logs only after the next hardware
  pass is stable.
- If broader HID-proxy behavior becomes the target again, add a real worker
  state-machine layer before importing drivers whose upstream probe depends on
  blocking request/response semantics.
