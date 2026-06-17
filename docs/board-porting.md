# Porting HID Remapper To Another Board

This is the checklist for moving the HID remapper branch from Waveshare RP2040-PiZero to another RP2040 board.

## 1. Select the board

Use an existing Pico SDK board when possible:

```cmake
set(PICO_BOARD your_board CACHE STRING "Board type")
```

If the board is not in the SDK, add a board header or pass the required flash defines yourself. The important value for this firmware is `PICO_FLASH_SIZE_BYTES`, because the config FAT volume is placed at the end of flash.

Files to check:

- `CMakeLists.txt`
- the selected Pico SDK board header
- `pico-usb-flash-drive/flash.h`

## 2. Check flash

Verify the board has enough flash for:

- firmware image;
- 64KB FAT config volume;
- any future assets/tables.

If `PICO_FLASH_SIZE_BYTES` is correct, `FLASH_FAT_OFFSET` follows automatically.

## 3. Wire PIO-USB host

Find the host-port D+/D- pins.

For the default Pico-PIO-USB pinout:

```cmake
PICO_DEFAULT_PIO_USB_DP_PIN=<gpio>
```

`D-` must be adjacent to `D+` for the default pinout. If the board uses reversed adjacent pins, set the matching Pico-PIO-USB pinout in `pio_usb_configuration_t`. If the pins are not a supported PIO-USB pair, this code path is not enough.

Files to check:

- `CMakeLists.txt`
- `usb_host/task.c`
- `tusb/tusb_config.h`

## 4. Avoid accidental board init pin use

Do not assume TinyUSB `board_init()` is safe. On some board definitions it initializes LEDs, VBUS enable pins, or default peripherals.

For this branch the safer pattern is:

- initialize only the clock/runtime needed by the firmware;
- configure PIO-USB explicitly in `usb_host/task.c`;
- initialize application GPIOs from `config.json`.

## 5. Power the host port

The external keyboard/mouse needs VBUS power on the host connector.

Board options:

- VBUS is always wired/powered by hardware;
- firmware must drive a VBUS enable GPIO;
- external wiring must provide 5V.

If a VBUS enable GPIO exists, initialize that exact GPIO explicitly. Do not use a dummy pin.

Files to check:

- board schematic
- `main.c`
- `usb_host/task.c`

## 6. Update docs

Create or update a board note with:

- board name and schematic link;
- selected `PICO_BOARD`;
- flash size/source;
- PIO-USB D+/D- pins;
- VBUS power behavior;
- onboard peripherals that consume GPIOs.

Keep generic USB host logic in [USB host implementation](usb-host.md), not in the board note.
