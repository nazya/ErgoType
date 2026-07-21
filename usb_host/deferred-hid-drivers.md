# Deferred HID Drivers

This note is about the current task-owned HID host architecture. It is not a
list of every "large" driver. The important boundary is whether the upstream
driver's transport and subsystem dependencies are implemented by the firmware
port.

## Current Safe Boundary

The current build may include drivers that only need one of these patterns:

- report descriptor fixup
- input mapping or input mapped hooks
- simple probe that does `hid_parse()` and `hid_hw_start()`
- `hid_hw_request()` followed by `hid_hw_wait()`, including returned feature
  data needed during probe
- synchronous-looking HID raw GET/SET and interrupt-output calls, because the
  wait happens in a firmware task while TinyUSB remains free to run
- upstream-shaped `.request()` OUTPUT routing: interrupt OUT when the
  interface exposes it, EP0 SET_REPORT otherwise

These drivers can run from the current pre-probe plus lifecycle-task probe path
because USB descriptor/string metadata is available before `hid_ignore()` and
driver probe. TinyUSB completions only publish bounded completion records; the
serialized async task resumes the waiting task-side continuation.

Razer is in this bucket: the tested macro-enable SET_REPORT uses upstream
`usbhid_set_raw_report()` over the generic async USB owner, and USB strings are
available before probe.

Multitouch and the standard HID Haptics Page helper are also in this bucket.
The lifecycle task completes the multitouch feature GET_REPORT before probe
continues, while haptic output uses the same asynchronous report owner. Both
paths have emulator/hardware coverage, including unplug and re-enumeration.

Regular FEATURE and raw requests stay on EP0; `.output_report()` remains the
interrupt-only entry point and returns `-ENOSYS` without an OUT endpoint.

## Unimplemented Transport Boundary

HID probe and remove no longer execute in TinyUSB callbacks, so waiting on an
implemented HID request does not create the original callback deadlock. Do not
link drivers whose probe/init path requires transport primitives or subsystem
ownership that the port still does not provide.

Current unsupported patterns include:

- `usb_submit_urb()` and request-specific kill/resubmit ownership
- synchronous interrupt-IN messages
- `usb_string()` or explicit string-descriptor reads when the string is required
  beyond the pre-probe product/manufacturer/serial snapshots
- multi-interface protocols that require a complete USB-core ownership model,
  sibling binding, or `usb_get_intfdata()` coordination
- protocols that need more reliable/larger request FIFOs than the bounded
  serialized HID queue currently provides

Task-context `usb_control_msg()` and interrupt-OUT `usb_interrupt_msg()` have
explicit submit, completion, timeout, per-interface cancellation, and
device-generation semantics in the TinyUSB transport owner. Their fixed pool
orders generic interrupt OUT per device endpoint, so one NAKing device does not
hold unrelated endpoints behind a global FIFO. The unsupported helpers remain
disabled instead of pretending success.

## Other Deferred Reasons

Some drivers should also stay out for reasons that are separate from callback
synchronous waits:

- sysfs
- hidraw fd/ioctl semantics
- LED class
- backlight
- power_supply
- hwrng
- force-feedback PID state
- framebuffer
- ALSA/rawmidi

`hid-cherry.c` is also deferred, but for the bounded parser rather than a
missing transport API. Real Cymotion descriptors expand an array to selector
`0x03ff`, and the driver's useful `0x301..0x303` mappings sit above this
firmware's `HID_MAX_USAGES=675` (`0x2a2`) cap. A synthetic descriptor with
three explicit usages would only test the hook while falsely claiming support
for the real hardware; increasing the cap would cost roughly 3 KiB per such
report and is not justified for this legacy keyboard.

Those are missing Linux subsystem ownership layers, not the same problem as
blocking inside TinyUSB callbacks.

`hid-samsung.c` is deferred for this reason: the Samsung IrDA 184-byte path
forces HIDDEV and disables normal hidinput. The current firmware has no useful
hiddev consumer/proxy for ErgoType input routing, so the imported source stays
in the tree but is not linked by the CMake HID allowlist. Its emulator fixture
is not part of the current devices repo branch set.

Normal HID keyboard LEDs are not part of the Linux LED class boundary above.
CapsLock/NumLock/ScrollLock output remains in the generic HID input path:
`EV_LED` updates schedule `hidinput_led_worker()`, which sends the keyboard
LED SET_REPORT through the HID ll_driver request path. The deferred LED class
means `/sys/class/leds`-style brightness devices and vendor LED/RGB panels,
which need a firmware proxy/API before they are useful in this embedded host.
On boot-keyboard start, `usbhid` also clears NumLock and submits the complete
output report through the same route.

## Future Transport Work

Drivers beyond the current boundary need extensions to the existing
sync-over-async transport:

- add request-specific URB submit/kill/resubmit ownership
- add synchronous interrupt-IN without stealing continuous HID polling
- add any request-specific transfer contract only for an audited linked user
- retain generation-based cancellation on unmount and fast replug

The Linux-shaped caller may block only in a task; TinyUSB callbacks must remain
bounded publishers.

## Examples To Keep Deferred Until Their Dependencies Exist

These are examples from upstream classes that still need a driver-by-driver
subsystem/lifecycle audit before they can be trusted. Synchronous raw GET/SET
by itself is no longer a transport blocker:

- `hid-alps.c`: raw GET/SET transport is present, but the surrounding init and
  device-specific state remain unaudited.
- `hid-letsketch.c`: uses `usb_string()` for tablet string data.
- `hid-lg.c` / `hid-lg4ff.c`: feature/raw transport is present, but FF and
  wait-style init dependencies remain unaudited.
- `hid-logitech-hidpp.c`: request/response protocol with wait queues.
- `hid-ntrig.c`: USB control-message firmware/query path.
- `hid-sony.c`, `hid-nintendo.c`, `hid-playstation.c`: controller init uses
  request/response and worker-style state.
- `hid-uclogic-params.c`: USB strings, interface counts, and returned feature
  data drive parameter setup.

This list is not exhaustive. Reimport one driver at a time and record the
specific upstream wait/request site and the hardware/emulator check that makes
it safe under the firmware transport layer.
