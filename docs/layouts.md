# Layouts and Host Synchronization

This page covers the recommended ErgoType model for multilingual layouts and host synchronization.

## Model

For multilingual setups, the recommended ErgoType model is:

1. Keep the host OS layouts installed normally, for example `us` and `ru`.
2. Use ErgoType `[...:layout]` sections to remap keyboard keycodes to host keycodes.
3. Let the host OS layout interpret those keycodes into final characters.

In practice:
- `[en:layout]` is usually an identity mapping to the host's English layout.
- `[ru:layout]` emits the host keycodes that should produce Russian characters when the host is on its Russian layout.
- ErgoType is not generating Unicode or text directly. It is selecting which HID keycodes to send.

This keeps the split clean:
- the host owns language/layout state;
- ErgoType owns the physical-to-logical keyboard mapping.

## `setlayoutm(layout, macro)`

ErgoType provides:

```ini
setlayoutm(<layout>, <macro>)
```

This is intended for host-layout synchronization:
- first run a macro that switches the host layout;
- then switch the internal ErgoType layout.

Example:

```ini
[main]
f13 = setlayoutm(en, macro(f13))
f14 = setlayoutm(ru, macro(f14))
```

## Deterministic host switching

Prefer deterministic host layout switching over cyclic switching.

Recommended:
- one shortcut that always selects layout 0 / English
- one shortcut that always selects layout 1 / Russian

Not recommended:
- one shortcut that cycles through layouts in sequence

Cyclic switching is easy to desynchronize from ErgoType's internal layout state.
Deterministic switching works naturally with `setlayoutm(...)`.

## Linux / XKB

On Linux, host layout switching is usually provided by XKB.

XKB supports:
- cyclic switching options such as `grp:alt_shift_toggle`, `grp:win_space_toggle`
- direct first/second layout selection options such as:
  - `grp:shift_caps_switch`
  - `grp:win_menu_switch`
  - `grp:lctrl_rctrl_switch`
  - `grp:lctrl_lwin_rctrl_menu`

Example:

```sh
setxkbmap -layout us,ru -option grp:lctrl_lwin_rctrl_menu
```

If you already maintain your own XKB symbols file, a very explicit deterministic setup is:

```xkb
key <FK13> { [ ISO_First_Group ] };
key <FK14> { [ ISO_Last_Group ] };
```

Then in `default.conf`:

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
- use normal XKB options in the device section;
- or switch layouts explicitly with `hyprctl switchxkblayout`.

Example per-device config:

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

If you already have an older custom XKB setup, Wayland is not automatically a blocker.
In practice this depends on the compositor, but compositor-side XKB support does exist.
For example, Hyprland exposes normal XKB parameters (`kb_layout`, `kb_variant`, `kb_options`)
and also a `kb_file` option, so an existing custom XKB file can still be reused.

Example:

```ini
device {
    name = ergotype-ergotype-usb-device-keyboard
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

`Shift` is different and usually should not bypass the active layout.
For text entry, `Shift` should continue to work on top of the selected typing layout.

## Windows

Windows has built-in host shortcuts for cycling layouts, for example:
- `Win+Space`
- `Win+Shift+Space`
- `Ctrl+Shift`

For deterministic "always switch to layout N" behavior, Windows typically needs extra host-side automation.
The ErgoType-side recommendation is still the same:
- create one host shortcut for English;
- create one host shortcut for Russian;
- call them from `setlayoutm(...)`.

## macOS

macOS also provides built-in host switching, but the standard shortcuts are still relative / cycling:
- `Control-Space`
- `Control-Option-Space`
- Fn/Globe key switching

For deterministic "switch to this exact input source" behavior, macOS generally needs extra host-side automation.
The ErgoType-side recommendation is the same:
- define one host shortcut per target layout;
- use `setlayoutm(...)` to call that shortcut and switch the internal ErgoType layout at the same time.
