# Boot modes (MSC vs HID)

ErgoType supports two USB modes:
- **MSC**: Mass Storage Class (exposes the on-flash FAT volume so you can edit `config.json`, `default.conf`, and overlays)
- **HID**: Human Interface Device (keyboard + optional pointing)

Mode is resolved at boot by `main.c`.

## Base mode

- If `config.json` parses successfully: base mode is **HID**.
- If config parse fails: base mode is **MSC**.

Config parse failures include:
- file missing (`config.json` not found)
- invalid JSON
- “config has no input devices” (matrix has no rows and no cols, and encoders + pointing are absent)

## Forcing MSC mode

If base mode is HID, firmware can still choose MSC at boot:

- `msc_pin` (active-low GPIO button): if set and reads LOW at boot → MSC
- `nr_pressed_msc`: if `msc_pin` is unset/invalid and `nr_pressed >= nr_pressed_msc` → MSC

Note: `nr_pressed_*` counting depends on the matrix (`gpio_rows`/`gpio_cols`) being configured, because it scans the matrix once at boot.

## HID output profile

Default HID profile is **NKRO**.

- **NKRO**: full keyboard bitmap, mouse interface, Consumer Control report, WebHID config interface.
- **Boot/legacy**: boot keyboard interface + boot mouse interface, for hosts that need boot protocol compatibility.

Hold exactly one non-encoder key at boot:

- `l`: select Boot/legacy HID profile and clear the keyd overlay

WebHID is available only in the NKRO HID profile.

## Forcing filesystem re-init

If base mode is HID, firmware can also re-initialize the on-flash FAT filesystem:

- `erase_pin` (active-low GPIO button): if set and reads LOW at boot → filesystem re-init + MSC
- `nr_pressed_erase`: if `erase_pin` is unset/invalid and `nr_pressed >= nr_pressed_erase` → filesystem re-init + MSC

## Pin read semantics

`msc_pin` / `erase_pin` are read as:
- GPIO input with internal pull-up enabled
- active-low (pressed = LOW)

## Caution: mount failure recovery

During `config.json` load, if `f_mount()` fails, firmware calls `flash_fat_initialize()` and retries the mount.
This is a destructive recovery path (filesystem re-init).
