# Waveshare ESP32-S3-Pico

This target is configured for the Waveshare ESP32-S3-Pico with ESP32-S3R2,
16 MB external flash, 2 MB quad PSRAM, and an onboard WS2812 on GPIO21. The
target enables PSRAM and uses it for large keyd allocations.

Build-tested; hardware validation is pending.

## USB

The board exposes one USB Type-C connector through its onboard USB hub. ErgoType
uses the ESP32-S3 native full-speed USB OTG controller for TinyUSB; the onboard
CH343 UART remains a separate function for flashing and console output.

## Build

After exporting the ESP-IDF environment, run:

```sh
./platform/esp-idf/build-firmware.sh esp32s3
```

Output:

- `platform/esp-idf/esp32s3/firmware/ErgoType-waveshare-esp32s3-pico-full-0x000000.bin`
- `platform/esp-idf/esp32s3/firmware/ErgoType-waveshare-esp32s3-pico-app-0x010000.bin`

| File | Flash address | Use | `config.json` |
| --- | --- | --- | --- |
| `ErgoType-waveshare-esp32s3-pico-full-0x000000.bin` | `0x000000` | First flash, recovery, or after `Erase Flash` | Replaced |
| `ErgoType-waveshare-esp32s3-pico-app-0x010000.bin` | `0x010000` | Normal firmware update | Preserved |

Do not run `Erase Flash` for a normal app-only update.

## Available GPIO

The target accepts the exposed general-purpose pins and the onboard WS2812 pin:

```text
1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
21, 38, 39, 40, 41, 42
```

GPIO19/GPIO20 are reserved for native USB, GPIO33..GPIO37 are reserved by the
board's memory wiring, and GPIO0 is the BOOT strap. Other pins omitted by the target are either
not exposed as general-purpose board pins or are intentionally reserved.
