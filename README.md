# ErgoType
ErgoType is a project I'm developing to integrate the keyboard features I find
most valuable. It ports [`keyd`](https://github.com/rvaiya/keyd) to embedded
keyboard firmware and currently supports RP2040/RP2350 through the Pico SDK and
ESP32-S3/ESP32-P4 through ESP-IDF.

Documentation index: [`docs/README.md`](docs/README.md).

## Platforms

- `platform/include/platform`: platform-independent interfaces.
- `platform/rp2`: Pico SDK backend for RP2040/RP2350.
- `platform/esp-idf/common`: shared ESP-IDF backend for ESP32 targets.
- `platform/esp-idf/esp32s3`: Waveshare ESP32-S3-Pico project (16 MB flash,
  2 MB quad PSRAM, onboard WS2812 on GPIO21; build-tested, hardware validation
  pending).
- `platform/esp-idf/esp32p4`: FireBeetle 2 ESP32-P4 project (16 MB flash,
  32 MB hex PSRAM; USB, MSC, HID, matrix, and onboard LED smoke-tested).

Platform-independent firmware uses only the interfaces under
`platform/include/platform/`. Generated firmware lives under
`platform/esp-idf/<chip>/firmware/` and is ignored
by Git.

VS Code:

- Open the repository root for the Pico extension.
- Open `ESP-IDF.code-workspace`, run `ESP-IDF: Pick a Workspace Folder`, and
  select the S3 or P4 folder for the ESP-IDF extension.

Pico build:

```sh
cmake -S . -B build-pico
cmake --build build-pico -j4
```

ESP build after exporting the ESP-IDF environment:

```sh
platform/esp-idf/build-firmware.sh esp32s3
platform/esp-idf/build-firmware.sh esp32p4
```

GitHub Actions artifacts:

| Artifact | Files |
| --- | --- |
| `firmware-raspberry-pi-pico` | `ErgoType-raspberry-pi-pico.uf2` |
| `firmware-rp2350` | `ErgoType-rp2350.uf2` |
| `firmware-waveshare-esp32s3-pico` | `ErgoType-waveshare-esp32s3-pico-full-0x000000.bin`, `ErgoType-waveshare-esp32s3-pico-app-0x010000.bin` |
| `firmware-dfrobot-firebeetle2-esp32p4` | `ErgoType-dfrobot-firebeetle2-esp32p4-full-0x000000.bin`, `ErgoType-dfrobot-firebeetle2-esp32p4-app-0x010000.bin` |

Each ESP command produces two files:

| File | Flash address | Use | `config.json` |
| --- | --- | --- | --- |
| `ErgoType-<board>-full-0x000000.bin` | `0x000000` | First flash, recovery, or after `Erase Flash` | Replaced with the initial FAT image |
| `ErgoType-<board>-app-0x010000.bin` | `0x010000` | Normal firmware update | Preserved |

Do not erase the flash before a normal app-only update. The full image contains
the bootloader, partition table, application, and initial 64 KB FAT volume; it
does not write the unused remainder of the 16 MB flash chip.

Rebuilding the same chip replaces these two files instead of accumulating
versioned binaries.

## Storage

All platform backends expose the same 64 KB FAT12 volume over USB MSC. FatFs runs with its
tiny mode enabled (`FF_FS_TINY=1` on RP2 and
`CONFIG_FATFS_PER_FILE_CACHE=n` on ESP), so each open `FIL` is 40 bytes instead
of 552 bytes while the mounted filesystem keeps one shared 512-byte sector
cache. This reduces peak stack use by 512 bytes for each active file object;
fixed FreeRTOS task stack allocations remain unchanged until their configured
sizes are reduced separately.

LittleFS is not used because it is not a filesystem desktop operating systems
mount from a USB mass-storage block device. Keeping the current MSC workflow
with LittleFS would require retaining or emulating a separate FAT volume, which
would increase code and RAM use instead of reducing it.

## Goals
ErgoType aims to extend the functionality of keyd beyond Linux by creating a hardware solution for a range of platforms and devices. It also aims to combine a keyboard with a pointing device, focusing on ergonomic design.


## Features
- **Easy Setup**  
  Just define rows and columns pins and a corresponding keymap in the configuration file.

- **Easy Configuration**  
  Dynamic device configuration with 2 USB modes: mass storage device class (MSC) or human interface device class (HID).

- **Easy Firmware Access**  
  The latest firmware is automatically built and available via GitHub Actions! Simply download the firmware from the GitHub Actions page without any setup or additional packages.

- **overloadtm** (keyd related)  
  Identical to overloadt, but additionally executes a supplied macro on layer activation.

## Bugs and Feedback
If you encounter a bug, please feel free to file an issue.

I also welcome your feedback and suggestions! Check [`ErgoType Discord server`](https://discord.gg/BXfzjKU5).

## Acknowledgments
A big thank you to all the keyd developers and contributors, especially @rvaiya and @gkbd. Special thanks to @oyama and @hakanrw for their contributions to integrating MSC USB, FatFs operations on PICO flash memory, and FreeRTOS. I also appreciate the efforts of the FreeRTOS, TinyUSB, coreJSON, and QMK developers and contributors for enhancing this project's capabilities.
