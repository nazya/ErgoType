# USB Debug Summary

Updated: 2026-04-26

This file contains only observed facts and changes that were tried without effect.

## Facts

1. `dbg("starting os")` is called in `main.c` before `vTaskStartScheduler()`.

2. In both `MSC` and `HID` modes, the mode-entry log is visible in CDC logs, but `dbg("starting os")` is not.

3. Before `vTaskStartScheduler()`, created FreeRTOS tasks are not running yet.

4. Therefore, the missing `dbg("starting os")` line is not explained by task execution after scheduler start.

5. In the current setup, `dbg("starting os")` has not been observed in CDC logs.

6. The next log line can appear only partially. Observed example:

```text
INFO: ErgoType/main.c:200: entered MSC mode
INFO:
```

7. The long config dump before this point can be visible in CDC logs.

8. Removing the config dump entirely does not make `dbg("starting os")` appear.

9. If the following loop is uncommented before `vTaskStartScheduler()`, behavior gets worse:

```c
// for (int i = 0; i < 1000; ++i) {
//     tud_task();
//     sleep_ms(2);
// }
```

With that loop enabled, logs from `keyd` are not observed after scheduler start. With that loop commented out, logs from `keyd` are observed after scheduler start.

10. After raising the `tusb_device_task` priority, a captured host log showed successful USB enumeration with `ttyACM0` created and all interfaces bound:

```text
KERNEL[1109369.529279] add      /devices/pci0000:00/0000:00:14.0/usb1/1-12 (usb)
KERNEL[1109369.533687] add      /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.0 (usb)
KERNEL[1109369.535935] add      /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.0/tty/ttyACM0 (tty)
KERNEL[1109369.536356] bind     /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.0 (usb)
KERNEL[1109369.536721] add      /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.1 (usb)
KERNEL[1109369.537077] bind     /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.1 (usb)
KERNEL[1109369.538794] add      /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.2 (usb)
KERNEL[1109369.623769] bind     /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.2 (usb)
KERNEL[1109369.624167] bind     /devices/pci0000:00/0000:00:14.0/usb1/1-12 (usb)
```

11. A captured kernel log also showed successful enumeration with CDC and HID up:

```text
[1107635.490924] usb 1-12: new full-speed USB device number 69 using xhci_hcd
[1107635.632908] usb 1-12: New USB device found, idVendor=cafe, idProduct=1001, bcdDevice= 1.00
[1107635.632925] usb 1-12: New USB device strings: Mfr=1, Product=2, SerialNumber=3
[1107635.632933] usb 1-12: Product: ErgoType USB Device
[1107635.632940] usb 1-12: Manufacturer: ErgoType
[1107635.632946] usb 1-12: SerialNumber: 1
[1107635.641467] cdc_acm 1-12:1.0: ttyACM0: USB ACM device
[1107635.652908] input: ErgoType ErgoType USB Device Keyboard as /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.2/0003:CAFE:1001.018C/input/input793
[1107635.736432] input: ErgoType ErgoType USB Device Mouse as /devices/pci0000:00/0000:00:14.0/usb1/1-12/1-12:1.2/0003:CAFE:1001.018C/input/input794
[1107635.737066] hid-generic 0003:CAFE:1001.018C: input,hidraw0: USB HID v1.11 Keyboard [ErgoType ErgoType USB Device] on usb-0000:00:14.0-12/input2
```

## Tried Without Effect

1. Added `stdio_flush()` after `dbg("starting os")`. This had no effect. In the current setup, this is consistent with the SDK already flushing inside the printf path.

2. Added a loop after `dbg("starting os")` with repeated `tud_task()` and `sleep_ms(...)`.

3. Increased that post-log loop to a much longer wait.

4. Changed the CDC wait before scheduler start.

5. Raised the `tusb_device_task` priority.

6. Removed the config dump entirely. This did not make `dbg("starting os")` appear.

7. Added a long pre-scheduler loop with repeated `tud_task()` and `sleep_ms(2)` before `vTaskStartScheduler()`. This made behavior worse.

## What Follows Directly From The Facts

1. Raising `tusb_device_task` priority does not explain the missing `dbg("starting os")` line, because that log call happens before scheduler start.

2. The missing `dbg("starting os")` line reproduces in both `MSC` and `HID` modes.

3. Adding more pre-scheduler `tud_task()` time before `vTaskStartScheduler()` does not fix the missing `dbg("starting os")` line.
