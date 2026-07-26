# License inventory

| Component | Source and provenance | License |
|---|---|---|
| ErgoType shared code and nRF port | https://github.com/nazt/ErgoType, `main` source snapshot with local Zephyr changes | [MIT](licenses/LICENSE-ErgoType.txt), plus per-file notices |
| keyd-derived code | https://github.com/rvaiya/keyd, vendored through the ErgoType source snapshot | [MIT/X](keyd/LICENSE) |
| Matrix scanner | QMK-derived ErgoType `keyscan.c`, ported at the Zephyr boundary | [GPL-2.0](licenses/LICENSE-GPL-2.0.txt), with notices retained in `src/keyscan_zephyr.c` |
| coreJSON | https://github.com/FreeRTOS/coreJSON, commit `27edcd51569b1c05fef8abaf73ca06923390df17` | [MIT](coreJSON/LICENSE) |
| PMW3360 driver and SROM | https://github.com/mrjohnk/PMW3360DM-T2QU | Upstream/source distribution terms; derivation notice retained in `pointing/pmw3360/pmw3360.c` |
| PMW3389 driver and SROM | https://github.com/mrjohnk/PMW3389DM | Upstream/source distribution terms; derivation notice retained in `pointing/pmw3389/pmw3389.c` |

The project is provided without warranty; see each license text for details.
