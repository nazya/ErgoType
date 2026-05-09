# Runtime architecture

This page describes the high-level runtime wiring (tasks + queues) as implemented in this repo.

## Main tasks (startup)

`main.c` starts an `app_task` on core 0. `app_task`:
- parses `config.json` (`parse()` in `jconfig.c`)
- resolves boot mode (MSC vs HID)
- starts the USB device task (`tusb_device_task`)
- in HID mode, starts keyboard + keyd tasks
- optionally starts pointing task and binds motion IRQs
- optionally starts LED tasks

## USB device loop

`tusb_device_task.c` runs:
- `tud_task()` (TinyUSB device processing)
- `stdio_tusb_cdc_poll()` (flushes buffered log output to CDC when host is connected)

## Keyboard input pipeline (matrix → keyd → HID)

In HID mode:

1. `keyscan_task` (`keyscan.c`) scans the matrix periodically and sends `struct device_event` into `keyscan_event_queue`.
2. `keyd` event loop (`keyd/port/evloop.c`) receives `device_event` entries and feeds them into the keyd engine (`kbd_process_events` in `keyd/port/keyboard.c`).
3. keyd outputs actions via the vkbd backend (`keyd/port/vkbd/vkbd.c`), which enqueues `key_event_t` into `vkbd_event_queue`.
4. `vkbd_hid_task` (`keyd/port/vkbd/tusb_hid.c`) consumes `vkbd_event_queue` and sends TinyUSB HID reports.

## Pointing pipeline (PMW33xx → vkbd → HID)

If `drivers.pmw3360` and/or `drivers.pmw3389` are configured:

- `pointing_device_task` (`pointing/pointer.c`) initializes SPI buses + sensors.
- Each sensor’s `irq` pin is wired to the shared GPIO IRQ callback (`pointing_motion_irq_init()`).
- On motion IRQ, the ISR notifies the pointing task; the task reads deltas and emits:
  - `vkbd_mouse_move(...)` for `"mousemove"` sensors
  - `vkbd_mouse_scroll(...)` for `"scroll"` sensors

These calls enqueue vkbd events into the same `vkbd_event_queue`, so the HID output path is shared.

## Logging / console output

See [`docs/logging.md`](logging.md).

