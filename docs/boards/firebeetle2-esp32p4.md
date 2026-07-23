# DFRobot FireBeetle 2 ESP32-P4

This target is configured for DFRobot DFR1172 V1.0 with ESP32-P4R32 rev1.0,
16 MB external flash, and 32 MB in-package PSRAM. The target enables PSRAM and
uses it for large keyd allocations so internal SRAM remains available for task
stacks, ESP-IDF, and DMA-capable buffers.

## USB ports

| Board connector | Firmware role |
| --- | --- |
| Top Type-C `USB CDC` | ROM flashing and ESP USB-Serial/JTAG debugging; left at its default routing |
| Side Type-C `HIGH-SPEED USB OTG 2.0` | ErgoType high-speed TinyUSB device connected to the computer |

ErgoType uses only the side connector for TinyUSB. The top connector keeps its
fixed USB-Serial/JTAG function for flashing and logs.

## Build

```sh
./platform/esp-idf/build-firmware.sh esp32p4
```

Output:

- `platform/esp-idf/esp32p4/firmware/ErgoType-dfrobot-firebeetle2-esp32p4-full-0x000000.bin`
- `platform/esp-idf/esp32p4/firmware/ErgoType-dfrobot-firebeetle2-esp32p4-app-0x010000.bin`

## Flash without ESP-IDF

| File | Flash address | Use | `config.json` |
| --- | --- | --- | --- |
| `ErgoType-dfrobot-firebeetle2-esp32p4-full-0x000000.bin` | `0x000000` | First flash, recovery, or after `Erase Flash` | Replaced |
| `ErgoType-dfrobot-firebeetle2-esp32p4-app-0x010000.bin` | `0x010000` | Normal firmware update | Preserved |

A standalone esptool installation or the official
[esptool-js Web Serial flasher](https://espressif.github.io/esptool-js/) is
sufficient; ESP-IDF is not needed on the flashing computer.

Enter ROM download mode before connecting the flasher:

1. Connect the top `USB CDC` Type-C port.
2. Hold `BOOT`.
3. Press and release `RST`.
4. Release `BOOT`.
5. Flash the selected image at the address included in its filename.
6. Press `RST` after flashing if the board does not restart automatically.

Do not run `Erase Flash` for a normal app-only update.

## Available GPIO

The FireBeetle target accepts the board LED and GPIO exposed on the side
headers:

```text
3, 4, 5, 7, 8, 20, 21, 22, 23, 28, 29, 30, 31, 32, 33, 34,
37, 38, 48, 51, 52
```

GPIO35 and GPIO36 participate in the board's download-mode strapping and are
not accepted for the keyboard configuration. GPIO used by native USB, the
ESP32-C6 link, TF card, microphone, flash, and MIPI interfaces is also excluded.
