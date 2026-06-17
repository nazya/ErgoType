# USB Host Implementation

USB host support lives in `usb_host/`.

## Files

| File | Purpose |
|------|---------|
| `task.c` | TinyUSB host task; configures PIO-USB and runs `tuh_task()`. |
| `hid_callbacks.c` | TinyUSB HID callbacks: mount, unmount, report received. |
| `hid.h` | Runtime state for one HID interface. |
| `hid_parser.c/.h` | HID report descriptor parser for input fields. |
| `hid_report.c` | Converts incoming HID reports into key/mouse device events. |
| `hid_keyd.c/.h` | HID usage page/usage -> `KEYD_*` mapping. |
| `forward.c/.h` | Pushes converted events into the device event queue. |

`dev_addr + instance` identifies one TinyUSB HID interface. A composite USB device can expose multiple HID instances.

## Flow

`tuh_hid_mount_cb()` allocates parser/runtime state, parses the report descriptor, creates a device event queue, registers the device with the keyd device layer, and arms the first report receive.

`tuh_hid_report_received_cb()` parses each incoming report immediately, forwards generated events with queue timeout `0`, and rearms `tuh_hid_receive_report()`.

`tuh_hid_umount_cb()` sends `DEV_REMOVED`, frees HID parser/runtime allocations, and removes the HID interface from the local table.

## Implemented

- HID report protocol via `tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT)`.
- Boot keyboard and boot mouse fallback.
- Report IDs.
- Keyboard/Keypad Page -> existing `KEYD_*`.
- Modifier byte -> left/right ctrl/shift/alt/meta.
- Button Page -> mouse buttons.
- Generic Desktop Page -> mouse X/Y/wheel, absolute X/Y, and selected system controls.
- Consumer Page -> known media/browser/app usages when a `KEYD_*` code exists.
- Relative mouse move, wheel, horizontal pan, and absolute movement forwarding.
- Device add/remove integration with the keyd device layer.

## Limits

- HID output and feature reports to the external device are not implemented.
- External keyboard LEDs are not driven by this host path.
- Unsupported HID pages can be parsed, but they produce no keyd event until `hid_keyd.c` has a semantic mapping.
- Vendor, FIDO, digitizer, LED, telephony, game, and similar pages currently map to `0`.
- Device-side Consumer Control output is not implemented yet; the current USB device side is keyboard/mouse.
- `KEYD_*` event codes are `uint8_t`. Wider HID support will likely need `uint16_t` key/event codes or a typed event carrying `usage_page + usage`.

## Future work

- Add device-side Consumer Control report descriptor and backend path.
- Decide how to represent non-keyboard HID usages in keyd without squeezing everything into `uint8_t`.
- Add output report handling if external keyboard LEDs matter.
- Add more usage mappings only where there is a clear keyd semantic target.
