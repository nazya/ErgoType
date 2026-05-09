# keyd.conf support

ErgoType embeds a port of [`keyd`](https://github.com/rvaiya/keyd) and reads a single `keyd.conf` from the device’s on-flash FAT filesystem.

This page documents what is **actually implemented** in `keyd/*` in this repo.

## File location + load timing

- File name: `keyd.conf`
- Location: root of the MSC drive (same volume that contains `config.json`)
- Loaded: once, when the `keyd` task starts in **HID** mode (no hot reload)

Practical workflow:
1. Boot into **MSC** mode, edit `keyd.conf` on the drive.
2. Reboot / power-cycle back into **HID** mode to apply.

See also: [`docs/modes.md`](modes.md).

## Supported config surface

### Sections

- `[global]` (timeouts / behavior)
- `[aliases]` (key aliases)
- Layer sections, e.g. `[main]`, `[nav]`, `[space]`
- Typed layers, e.g. `[control:C]`, `[alt:A]`, `[shift:S]`, `[meta:M]`, `[altgr:G]` (supports multiple modifiers like `C-S`)
- Layouts, e.g. `[colemak:layout]` (switch via `setlayout(...)`)
- Composite layers, e.g. `[nav+shift]` (**must** appear after its constituent layers)

### Actions (descriptor expressions)

Implemented actions are defined in `keyd/port/config.c` and executed in `keyd/port/keyboard.c`:

- Layers: `layer()`, `oneshot()`, `toggle()`, `swap()`, `clear()`, `setlayout()`
- Macro variants: `layerm()`, `oneshotm()`, `togglem()`, `swapm()`, `clearm()`
- Overloads/timeouts: `overload()`, `overloadt()`, `overloadt2()`, `overloadi()`, `lettermod()`, `timeout()`
- Macros: `macro(...)`, `macro2(...)` (same tokenization rules as upstream keyd)
- Experimental: `scroll()`, `scrollt()` (toggle)
- ErgoType extension: `overloadtm(layer, macro, action)` (see below)

Chords like `j+k = esc` are supported (up to 6 keys per chord).

### `[global]` options

Parsed options (all timeouts are interpreted as **milliseconds** in this port):

- `macro_timeout`
- `macro_repeat_timeout`
- `macro_sequence_timeout` (note: upstream keyd documents this as microseconds)
- `oneshot_timeout`
- `chord_timeout`
- `chord_hold_timeout`
- `overload_tap_timeout`
- `disable_modifier_guard`
- `layer_indicator` (**see “Known limitations”**)
- `default_layout` (name of a `[<name>:layout]` layer to activate at startup)
- `overloadtm_timeout` (hold timeout for `overloadtm(...)`)

## Output differences vs upstream keyd

ErgoType does not run a Linux daemon and does not emit `uinput` events. Instead, `keyd` output is translated into USB HID reports via the `vkbd` port layer (`keyd/port/vkbd/*`).

Practical implications:
- You don’t get `keyd monitor`, `keyd reload`, `keyd listen`, app-specific mapper, or any other IPC-driven features.
- Output is a standard 6KRO keyboard HID report (6 non-modifier keys + modifiers).

## Unsupported / intentionally skipped

These are parsed or recognized but not functional in this port:

- IPC (no runtime reconfiguration / reload)
- File inclusion (`include ...`)
- `[ids]` section (parsed, but skipped)
- `command(...)` (recognized by the parser, but not implemented in this port; treat as unsupported)
- Unicode output (upstream relies on compose sequences + host-side setup; this port does not currently provide a working path)

## Built-in defaults

Before `keyd.conf` is read, ErgoType seeds a small default config that defines:
- A default `[aliases]` section that aliases left/right modifiers to `shift`, `control`, `meta`, `alt`, `altgr`
- A default `[main]` that maps `shift`, `control`, `meta`, `alt`, `altgr` to corresponding `layer(...)` actions
- Empty modifier layers: `[control:C]`, `[shift:S]`, `[meta:M]`, `[alt:A]`, `[altgr:G]`

You can override any of these in `keyd.conf`.

## ErgoType extension: `overloadtm(...)`

`overloadtm(<layer>, <macro>, <action>)` behaves like `overloadt(<layer>, <action>, <timeout>)`,
but the hold path activates the layer **and** runs a macro via `layerm(<layer>, <macro>)`.

The hold timeout is controlled by `[global] overloadtm_timeout` (milliseconds).

Note: unlike `overloadt(...)`, the timeout is currently **global** (one value for all `overloadtm(...)` bindings).

## Known limitations (current code)

- `layer_indicator` is parsed from `[global]`, but does not currently drive any LED behavior in this port.

Upstream reference (syntax + semantics): https://github.com/rvaiya/keyd/tree/master/docs/keyd.scdoc
