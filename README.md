# ErgoTypeNRF

ErgoTypeNRF is an nRF52840 Bluetooth LE HID keyboard/pointing firmware built
with Nordic Connect SDK (NCS) v3.4.0 and Zephyr. It keeps the ErgoType keyd,
matrix, PMW33xx and acceleration flow while implementing the hardware boundary
with Zephyr GPIO, BLE, LittleFS and nrfx SPIM/TWIM/PWM.

The common behavior is ported from ErgoType `main`.

## Build and flash

Activate an NCS v3.4.0 toolchain using the nRF Connect SDK installation or its
Toolchain Manager terminal. In a fresh west workspace, clone this repository as
`app`, initialize from its manifest, and fetch the pinned SDK:

```sh
west init -l app
west update
west zephyr-export
```

From the clone itself, an already activated NCS environment builds with:

```sh
west build -b nrf52840dk/nrf52840 . \
  -d build --pristine always
west flash -d build
```

The verified local stack is NCS v3.4.0, Zephyr 4.4.0, west 1.5.0, Zephyr SDK
1.0.1, and GCC 14.3.0. The `build/` directory is ignored by Git.

The application expects `config.json` in its LittleFS storage partition. See
[the documentation index](docs/README.md) and [configuration reference](docs/config-json.md).

## Status

Implemented: matrix/encoder input, keyd mappings and overlays, BLE HID NKRO
keyboard (report 1), separate consumer control (report 2), 16-bit relative
mouse/scroll (report 3), PMW3360/PMW3389 with `none`/`flat`/`adaptive`/`custom`
acceleration, BLE configuration/log services, LittleFS, plain LEDs, WS2812 and
SSD1306.

LittleFS is mounted at `/lfs` before Bluetooth starts. Bluetooth Settings uses
the file backend at `/lfs/settings`; the project storage API exposes only
`/lfs/config`, so BLE file listing/read/write/delete operations cannot access
bonding data. Uploads are staged outside that directory and atomically renamed
on commit. NVS is disabled. The linker limits the application to
`0x00000..0xF7FFF`; the non-overlapping 32 KiB `storage_partition` occupies
`0xF8000..0xFFFFF`.

The BLE configuration service preserves the shared 63-byte protocol. Local
ATT MTU is 66 and ACL RX/TX buffers are 70 bytes; the peer must negotiate ATT
MTU 66 or greater before sending 63-byte requests or receiving notifications.
Notification failures are logged and reported to acknowledged GATT writes.

Intentionally excluded: RP2/Pico SDK and PIO, FreeRTOS, TinyUSB, USB device and
host support, mass storage, serial-over-USB, WebHID, UF2, and boot-protocol HID.

Parity with the source snapshot includes keyd/keyscan, startup `a`/`w`/`m`/`d`
overlays, PMW motion bursts/SROM/CPI, the fixed X×1.25/Y-negated transform,
all four acceleration profiles and UI/log/CapsLock semantics. Zephyr backends
live in `src/` and `platform/zephyr/`; small boundary adaptations also remain in
shared files such as `jconfig.c`, the PMW drivers, acceleration timing and the
keyd vkbd port. They select Zephyr storage, allocation, timing and HID APIs
without changing the common behavior.

Hardware-unverified: matrix/encoders, BLE interoperability, MTU negotiation
and bonding, LittleFS/settings persistence, PMW SROM/timing/IRQ/CPI, CapsLock
indication, SSD1306, plain LEDs and WS2812.
