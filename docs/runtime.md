# Runtime

`src/main.c` mounts LittleFS, parses `config.json`, starts BLE, then launches
the matrix scanner, keyd event loop, BLE HID vkbd consumer, optional pointing
thread and optional UI thread. Zephyr message queues connect matrix/pointing to
keyd and keyd to HID; no separate boot-mode task exists.

The HID service keeps readable canonical 32-byte keyboard and 12-byte consumer
states. Notifications use a serialized bounded queue: every keyboard/consumer
transition and relative mouse report remains ordered across temporary Bluetooth
buffer exhaustion. Queue capacity applies backpressure to the vkbd HID task.
Disconnect or CCC changes discard reports from the old subscription, and mouse
subscription never replays an earlier relative delta.

LittleFS is mounted before Bluetooth starts. Bluetooth Settings owns
`/lfs/settings`, while project files are independently rooted at `/lfs/config`.
The BLE file protocol cannot list or address the Settings file; uploads use a
hidden staging file and atomically replace the target only on commit.

Logical SPI buses use independent SPIM2/SPIM3 hardware while I2C uses
TWIM0/TWIM1. PMW motion uses falling-edge GPIO interrupts and the shared
acceleration pipeline.
