# Deferred HID Drivers

This note is about the current callback-driven HID host architecture. It is not
a list of every "large" driver. The important boundary is whether upstream code
would need to wait for a TinyUSB host operation while still running from a
TinyUSB callback path.

## Current Safe Boundary

The current build may include drivers that only need one of these patterns:

- report descriptor fixup
- input mapping or input mapped hooks
- simple probe that does `hid_parse()` and `hid_hw_start()`
- queue-only SET_REPORT where the driver does not need returned data before
  continuing probe

These drivers can run from the current pre-probe plus HID probe path because
USB descriptor/string metadata is available before `hid_ignore()` and driver
probe, and the driver itself does not need returned HID request data before
continuing.

Razer is in this bucket for now: the macro enable command is a SET_REPORT, and
the tested path only needs the request to be queued/sent. USB strings are now
available before probe, but this path still does not need returned GET_REPORT
data before `hid_hw_start()`.

## Callback-Unsafe Boundary

Do not link drivers whose probe/init path requires synchronous progress on a
TinyUSB transfer while still inside the current callback-driven path.

These upstream patterns are callback-unsafe here:

- `hid_hw_raw_request(... HID_REQ_GET_REPORT ...)` when the returned data is
  needed before probe can continue
- `hid_hw_request(... HID_REQ_GET_REPORT ...)` followed by `hid_hw_wait()` or
  equivalent returned-data use
- `usb_control_msg()` / `usb_submit_urb()` when the response is required before
  continuing
- `usb_string()` or explicit string-descriptor reads when the string is required
  before continuing
- multi-interface USB state that must be complete before probe, such as sibling
  interface lookups or `usb_get_intfdata()` dependencies

The reason is specific: TinyUSB completions are delivered by the TinyUSB host
task/callback machinery. If the current callback path waits for an operation
whose completion needs that same machinery to run, the port can deadlock or
leave the HID device stuck before `hid_add_device()`.

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

## Future Layer

Drivers from the callback-unsafe bucket need a worker-based sync-over-async
transport layer:

- TinyUSB callback saves mount/request state and returns
- a firmware HID worker task runs Linux-shaped probe/request code
- TinyUSB completions wake that worker
- blocking-looking upstream calls run only in the worker, not in callbacks
- unmount cancels pending contexts by `dev_addr`/`instance`

With that layer, some upstream blocking probe code can be kept much closer to
Linux because it will no longer run inside the TinyUSB callback path.

## Examples To Keep Deferred Until Worker Exists

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
