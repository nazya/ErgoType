# Runtime architecture

This page describes the high-level runtime wiring (tasks + queues) as implemented in this repo.

## Main tasks (startup)

`app_start()` performs platform initialization and starts `app_task` on core
0. The RP2 entrypoint then starts the FreeRTOS scheduler; ESP-IDF calls its
entrypoint with the scheduler already running. `app_task`:
- parses `config.json` (`parse()` in `jconfig.c`)
- resolves boot mode (MSC vs HID)
- starts the USB device task (`platform_usb_device_task`)
- in HID mode, starts keyboard + keyd tasks
- optionally starts pointing task and binds motion IRQs
- optionally starts LED tasks

## USB device loop

The RP2 backend in `platform/rp2/usb_tinyusb.c` and the ESP-IDF backend in
`platform/esp-idf/common/usb_tinyusb.c` initialize their respective USB PHY and
then run the same device loop:
- `tud_task()` (TinyUSB device processing)
- `hid_work()` (advances queued HID output without blocking)
- `platform_cdc_work()` (flushes buffered log output to CDC when host is connected)

`tusb/device.c` owns bus state; `tusb/device_callbacks.c` forwards the
delivered suspend/resume callbacks. The
USB device task retains pending reports and requests one remote wakeup per
suspend episode. The RP2 target alone links the SOF workaround.

## Keyboard input pipeline (matrix → keyd → HID)

In HID mode:

1. `keyscan_task` (`keyscan.c`) scans the matrix periodically and sends `struct device_event` into `input_event_queue`.
2. `keyd` event loop (`keyd/port/evloop.c`) receives `device_event` entries and feeds them into the keyd engine (`kbd_process_events` in `keyd/port/keyboard.c`).
3. The USB HID backend (`tusb/hid.c`) implements keyd's `vkbd.h` interface directly. Output calls queue events and wake the USB device task. The global queue is owned by `main.c` and created in `app_task()` in HID mode before keyd starts; capacity remains 256 events with a 3-tick enqueue timeout.
4. `hid_work()` consumes those events in the USB device task and retains a prepared report while suspended or while its endpoint is busy. There is no separate HID output task. A USB completion, lifecycle event, or input notification lets processing continue.

## Pointing pipeline (PMW33xx → keyd → HID)

If `drivers.pmw3360` and/or `drivers.pmw3389` are configured:

- `pointing_device_task` (`pointing/pointer.c`) initializes SPI buses + sensors.
- Each sensor’s `irq` pin is wired to the shared GPIO IRQ callback (`pointing_motion_irq_init()`).
- On motion IRQ, the ISR notifies the pointing task; the task reads deltas and sends `DEV_MOUSE_MOVE` or `DEV_MOUSE_SCROLL` into `input_event_queue`.

keyd consumes the same input queue as matrix events, so keyd scroll mode can affect pointing motion before events reach the HID backend.

## Logging / console output

See [`docs/logging.md`](logging.md).
