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

These drivers can run from the current pre-probe plus lifecycle-task probe path
because USB descriptor/string metadata is available before `hid_ignore()` and
driver probe. TinyUSB completions only publish bounded completion records; the
serialized async task resumes the waiting task-side continuation.

Razer is in this bucket: the tested macro-enable SET_REPORT uses the serialized
HID request owner, and USB strings are available before probe.

## Unimplemented Transport Boundary

HID probe and remove no longer execute in TinyUSB callbacks, so waiting on an
implemented HID request does not create the original callback deadlock. Do not
link drivers whose probe/init path requires transport primitives or subsystem
ownership that the port still does not provide.

Current unsupported patterns include:

- `usb_control_msg()` / `usb_submit_urb()` when the response is required before
  continuing
- `usb_string()` or explicit string-descriptor reads when the string is required
  beyond the pre-probe product/manufacturer/serial snapshots
- multi-interface protocols that require a complete USB-core ownership model,
  sibling binding, or `usb_get_intfdata()` coordination
- protocols that need more reliable/larger request FIFOs than the bounded
  serialized HID queue currently provides

The generic helpers remain disabled instead of pretending success. Each new
primitive needs explicit submit, completion, cancellation, and device-generation
semantics in the TinyUSB transport owner.

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

## Future Transport Work

Drivers beyond the current boundary need extensions to the existing
sync-over-async transport:

- route HID OUTPUT to interrupt OUT when available and EP0 otherwise
- expose honest transfer result and short-transfer length for control requests
- add bounded per-request completion ownership instead of global special cases
- implement generic control/URB operations only for audited linked users
- retain generation-based cancellation on unmount and fast replug

The Linux-shaped caller may block only in a task; TinyUSB callbacks must remain
bounded publishers.

## Examples To Keep Deferred Until Their Dependencies Exist

These are examples from upstream classes that need callback-unsafe request or
string/control-response handling before they can be trusted:

- `hid-alps.c`: raw GET/SET feature report init paths use returned data.
- `hid-letsketch.c`: uses `usb_string()` for tablet string data.
- `hid-lg.c` / `hid-lg4ff.c`: feature/raw requests and wait-style init paths.
- `hid-logitech-hidpp.c`: request/response protocol with wait queues.
- `hid-multitouch.c`: feature GET_REPORT state must be read before setup.
- `hid-ntrig.c`: USB control-message firmware/query path.
- `hid-sony.c`, `hid-nintendo.c`, `hid-playstation.c`: controller init uses
  request/response and worker-style state.
- `hid-uclogic-params.c`: USB strings, interface counts, and returned feature
  data drive parameter setup.

This list is not exhaustive. Reimport one driver at a time and record the
specific upstream wait/request site and the hardware/emulator check that makes
it safe under the firmware transport layer.
