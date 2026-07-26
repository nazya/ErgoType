# Debugging

Build with `west build` and inspect the Zephyr console through the board's
debug UART. `CONFIG_LOG` is enabled; application startup, BLE failures and
configuration errors are reported there. Use `west build -t menuconfig` only
for diagnosis and keep committed settings in `prj.conf`.

For runtime failures, first confirm that LittleFS mounts, `config.json` parses,
the selected pins are in `0..47`, and the peer subscribes to the relevant BLE
CCC. PMW failures additionally require checking mode-3 SPI, CS, MOT wiring and
sensor power with a logic analyzer.
