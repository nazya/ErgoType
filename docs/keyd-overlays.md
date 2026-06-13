# Keyd OS Overlays

Motivation:

- Keep one default keyd config.
- Put OS-specific differences into small overlay files.
- Common differences are layout shortcuts, Command/Control ergonomics, and non-English symbol bindings.

`config.json` can set `os` to select a small keyd overlay. The value is a bare name; keyd appends `.conf`.

```json
{
  "os": "macos"
}
```

With this config, files on the Pico FAT volume are loaded in this order:

1. `default.conf`
2. `macos.conf`

If `os` is not set, only `default.conf` is loaded. If `os` is set, the overlay file must exist.

Overlay rules:

- The overlay is parsed on top of `default.conf`.
- Entries with the same `layer.key` replace default entries.
- Missing entries stay from `default.conf`.
- Repeating a section does not clear that layer.

Example `default.conf`:

```ini
[main]
leftmeta = overload(meta, setlayoutm(en, S-capslock))
rightmeta = overload(meta, setlayoutm(ru, S-capslock))

[en:layout]
leftmeta = timeout(layer(meta), 300, S-capslock)

[ru:layout]
rightmeta = timeout(layer(meta), 300, S-capslock)
```

A `macos.conf` overlay should contain only entries that actually differ from `default.conf`.

Example `macos.conf`:

```ini
[main]
leftmeta = overload(meta, setlayoutm(en, C-space))
rightmeta = overload(meta, setlayoutm(ru, C-space))

[en:layout]
leftmeta = timeout(layer(meta), 300, C-space)

[ru:layout]
rightmeta = timeout(layer(meta), 300, C-space)
```

Result: the base config stays shared, and the overlay only carries OS-specific differences.
