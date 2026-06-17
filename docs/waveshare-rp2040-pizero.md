# Waveshare RP2040-PiZero Board Notes

This file describes what this branch changed for Waveshare RP2040-PiZero.

Status: work in progress. These changes build, but they have not been tested on the physical RP2040-PiZero board yet.

References:

- Wiki: <https://www.waveshare.com/wiki/RP2040-PiZero>
- Schematic: <https://files.waveshare.com/wiki/RP2040-PiZero/RP2040-PiZero.pdf>

## Build target

`CMakeLists.txt` selects the Pico SDK board:

```cmake
set(PICO_BOARD waveshare_rp2040_pizero CACHE STRING "Board type")
```

The SDK board header provides the 16MB flash size:

```text
~/.pico-sdk/sdk/2.1.1/src/boards/include/boards/waveshare_rp2040_pizero.h
```

## Flash

The onboard flash is `W25Q128JVSI`.

The firmware FAT volume stays 64KB and is placed at the end of board flash:

```c
#define FLASH_FAT_OFFSET (PICO_FLASH_SIZE_BYTES - FAT_BLOCK_NUM * FAT_BLOCK_SIZE)
```

This avoids hardcoding the old 2MB Pico flash address.

## PIO-USB host port

The second Type-C port is used as PIO-USB host:

| Signal | GPIO |
|--------|------|
| D+     | 6    |
| D-     | 7    |

Current build define:

```cmake
PICO_DEFAULT_PIO_USB_DP_PIN=6
```

`usb_host/task.c` writes this into `pio_usb_configuration_t.pin_dp` before `tusb_init()`.

TinyUSB `board_init()` is not used here. `main.c` initializes the clock directly, and `usb_host/task.c` configures PIO-USB explicitly.

## Onboard peripherals not used

MicroSD wiring:

| Signal   | GPIO |
|----------|------|
| SD_SCK   | 18   |
| SD_MOSI  | 19   |
| SD_MISO  | 20   |
| SD_CS    | 21   |

DVI over HDMI wiring:

| Purpose      | GPIO |
|--------------|------|
| DVI data/clk | 22..29 |
| DVI DDC SDA  | 2    |
| DVI DDC SCL  | 3    |
| DVI CEC      | 16   |

The current firmware does not use MicroSD or DVI.
