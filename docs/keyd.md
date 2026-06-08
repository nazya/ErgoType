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
- Macro variants: `layerm()`, `oneshotm()`, `togglem()`, `swapm()`, `clearm()`, `setlayoutm()`
- Overloads/timeouts: `overload()`, `overloadt()`, `overloadt2()`, `overloadi()`, `lettermod()`, `timeout()`
- Macros: `macro(...)`, `macro2(...)` (same tokenization rules as upstream keyd)
- Experimental: `scroll()`, `scrollt()` (toggle)
- ErgoType extensions: `overloadtm(layer, macro, action)` and `setlayoutm(layout, macro)` (see below)

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

See also: [`docs/layouts.md`](layouts.md) for the recommended ErgoType multilingual layout model, host synchronization, `setlayoutm(...)`, XKB/Hyprland examples, and shortcut-layer guidance.

## Recommended host-layout model

For multilingual setups, the recommended ErgoType model is:

1. Keep the host OS layouts installed normally (for example, `us` and `ru`).
2. Use ErgoType `[...:layout]` sections to remap **keyboard keycodes to host keycodes**.
3. Treat the host OS layout as the final character interpreter.

In practice:
- `[en:layout]` is usually an identity mapping to the host's English layout.
- `[ru:layout]` emits the host keycodes that should produce Russian characters when the host is on its Russian layout.
- ErgoType is therefore not generating Unicode or text directly. It is selecting which HID keycodes to send.

This keeps the model simple:
- the host still owns language/layout state;
- ErgoType owns the physical-to-logical keyboard mapping;
- both sides can be switched together when needed.

## `setlayoutm(layout, macro)`

ErgoType provides `setlayoutm(<layout>, <macro>)`.

This is intended for host-layout synchronization:
- first run a macro that switches the host layout;
- then switch the internal ErgoType layout.

Example:

```ini
[main]
0 = setlayoutm(en, macro(M-A-e))
1 = setlayoutm(ru, macro(M-A-r))
```

In this example:
- `M-A-e` is a host shortcut bound to "switch host to English"
- `M-A-r` is a host shortcut bound to "switch host to Russian"
- ErgoType switches its own `[en:layout]` / `[ru:layout]` immediately after sending that shortcut

This is usually the cleanest way to keep the laptop/desktop and the keyboard firmware in sync.

## Host switching recommendations

Prefer **deterministic** host layout switching over cyclic switching.

Recommended:
- one shortcut that always selects layout 0 / English
- one shortcut that always selects layout 1 / Russian

Not recommended:
- one shortcut that cycles through layouts in sequence

Why:
- cyclic switching is easy to desynchronize from ErgoType's internal layout state;
- deterministic switching works naturally with `setlayoutm(...)`.

## Linux / XKB

On Linux, host layout switching is usually provided by XKB.

XKB supports both:
- cyclic switching options such as `grp:alt_shift_toggle`, `grp:win_space_toggle`
- direct first/second layout selection options such as:
  - `grp:shift_caps_switch`
  - `grp:win_menu_switch`
  - `grp:lctrl_rctrl_switch`
  - `grp:lctrl_lwin_rctrl_menu`

Example XKB config:

```sh
setxkbmap -layout us,ru -option grp:lctrl_lwin_rctrl_menu
```

That example gives you a non-cyclic first/second layout selection path from XKB itself.

If you prefer a cyclic host switch instead, the usual form is:

```sh
setxkbmap -layout us,ru -option grp:alt_shift_toggle
```

But for ErgoType + `setlayoutm(...)`, direct selection is usually the better fit.

If you already maintain your own XKB symbols file, a very explicit deterministic setup is to bind
dedicated keycodes to fixed XKB groups:

```xkb
key <FK13> { [ ISO_First_Group ] };
key <FK14> { [ ISO_Last_Group ] };
```

Then in `keyd.conf`:

```ini
[main]
f13 = setlayoutm(en, macro(f13))
f14 = setlayoutm(ru, macro(f14))
```

With two host layouts, this gives:
- `f13`: always switch host to the first group and ErgoType to `[en:layout]`
- `f14`: always switch host to the last group and ErgoType to `[ru:layout]`

## Linux / Hyprland

Hyprland can either:
- use normal XKB options in the `input` section;
- or switch layouts explicitly with `hyprctl switchxkblayout`.

Example Hyprland per-device config:

```ini
device {
    name = ergotype-ergotype-usb-device-keyboard
    kb_layout = us,ru
}
```

Example deterministic Hyprland binds:

```ini
bind = SUPER_ALT, E, exec, hyprctl switchxkblayout current 0
bind = SUPER_ALT, R, exec, hyprctl switchxkblayout current 1
```

Then bind the same shortcuts on ErgoType:

```ini
[main]
0 = setlayoutm(en, macro(M-A-e))
1 = setlayoutm(ru, macro(M-A-r))
```

This is often the easiest Wayland/Hyprland setup because the host-side selection is explicit by layout index.

If you already have an older custom XKB setup, Wayland is not automatically a blocker.
In practice this depends on the compositor, but compositor-side XKB support does exist.
For example, Hyprland exposes normal XKB parameters (`kb_layout`, `kb_variant`, `kb_options`)
and also a `kb_file` option, so an existing custom XKB file can still be reused instead of
rewriting everything from scratch.

Example:

```ini
input {
    kb_file = /home/user/.config/xkb/custom.xkb
}
```

## Shortcut layers vs localized layouts

Some shortcut systems resolve bindings by symbol, while others effectively expect the
English/QWERTY keycode side. Hyprland exposes this distinction explicitly with
`resolve_binds_by_sym`, and it also supports direct keycode binds via `code:...`.

Because of that, it is often useful to keep normal typing localized, but force shortcut
layers to emit stable English-side keycodes.

Practical pattern:
- keep normal typing in `[en:layout]`, `[ru:layout]`, etc.;
- define explicit shortcut layers for `Ctrl`, `Alt`, and `Meta`;
- in those layers, write direct alphabetic bindings so shortcut output stays predictable.

Example:

```ini
[control:C]
a = C-a
b = C-b
c = C-c
...
z = C-z
```

The same idea can be applied to:
- `[alt:A]`
- `[meta:M]`
- other modifier-specific shortcut layers when needed

`Shift` is different and usually should **not** bypass the active layout.
For text entry, `Shift` should continue to work on top of the selected typing layout.

## Windows

Windows has built-in host shortcuts for **cycling** layouts, for example:
- `Win+Space`
- `Win+Shift+Space`
- `Ctrl+Shift`

For deterministic "always switch to layout N" behavior, Windows typically needs extra host-side automation.

This document does not currently prescribe one Windows-specific automation stack.
If you need deterministic Windows switching, the intended ErgoType pattern is still the same:
- create one host shortcut for English;
- create one host shortcut for Russian;
- call them from `setlayoutm(...)`.

## macOS

macOS also provides built-in host switching, but the standard shortcuts are still relative / cycling:
- `Control-Space`
- `Control-Option-Space`
- Fn/Globe key switching

For deterministic "switch to this exact input source" behavior, macOS generally needs extra host-side automation.

As with Windows, the ErgoType-side recommendation is unchanged:
- define one host shortcut per target layout;
- use `setlayoutm(...)` to call that shortcut and switch the internal ErgoType layout at the same time.

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
