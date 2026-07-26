# Logging

ErgoType `msg`/`warn`/`err` output is written to the Zephyr console and, after
a client subscribes, to the custom BLE log notification service in 20-byte
chunks. `log_level` controls the shared logger: `0` is normal messages and
warnings/errors; `1..3` progressively enable debug output.

BLE log delivery is best-effort and is not the HID reliability path. Use the
Zephyr console for boot and disconnect diagnostics.
