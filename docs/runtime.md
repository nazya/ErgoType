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
- `tusb_hid_handle()` (advances queued HID output without blocking)
- `stdio_tusb_cdc_poll()` (flushes buffered log output to CDC when host is connected)

`tusb/usb_device_callbacks.c` owns suspend state and remote-wakeup attempts,
while retaining the existing UI and WebHID actions. Lifecycle state and wakeup
requests are processed only in the USB device task.

## Keyboard input pipeline (matrix → keyd → HID)

In HID mode:

1. `keyscan_task` (`keyscan.c`) registers the onboard keyboard as a `struct device` and sends `struct device_event` entries into that device queue.
2. `keyd` event loop (`keyd/port/evloop.c`) waits on the device table, receives `device_event` entries, and feeds them into the keyd engine (`kbd_process_events` in `keyd/port/keyboard.c`).
3. keyd outputs actions via the vkbd backend (`keyd/port/vkbd/vkbd.c`), which enqueues `vkbd_event_t` into `vkbd_event_queue` and wakes the USB device task. The queue is initialized once in HID mode before keyd starts; capacity remains 256 events with a 3-tick enqueue timeout.
4. `tusb_hid_handle()` (`keyd/port/vkbd/tusb_hid.c`) consumes those events in the USB device task and retains a prepared report while suspended or while its endpoint is busy. There is no separate HID output task. A USB completion, lifecycle event, or input notification lets processing continue.

## Pointing pipeline (PMW33xx → keyd → HID)

If `drivers.pmw3360` and/or `drivers.pmw3389` are configured:

- `pointing_device_task` (`pointing/pointer.c`) initializes SPI buses + sensors.
- Each sensor’s `irq` pin is wired to the shared GPIO IRQ callback (`pointing_motion_irq_init()`).
- On motion IRQ, the ISR notifies the pointing task; the task reads deltas and sends `DEV_MOUSE_MOVE` or `DEV_MOUSE_SCROLL` into the matching sensor device queue.

keyd consumes the same device table as matrix events, so keyd scroll mode can affect pointing motion before events reach the HID backend.

## Logging / console output

See [`docs/logging.md`](logging.md).
