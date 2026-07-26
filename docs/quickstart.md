# Quick start

Use NCS v3.4.0 and an activated Nordic toolchain. For a new west workspace,
clone the repository as `app`, then run from the workspace root:

```sh
west init -l app
west update
west zephyr-export
```

Build from the application clone and flash a connected nRF52840 DK:

```sh
west build -b nrf52840dk/nrf52840 . \
  -d build --pristine always
west flash -d build
```

Provision `config.json` in the LittleFS storage partition using the BLE config
service. Until a valid configuration is present, BLE services start but the
matrix/keyd/pointing tasks do not.
