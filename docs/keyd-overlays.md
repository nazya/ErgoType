# Keyd OS overlays

Set `overlay_conf` in `config.json` to load a small OS-specific file after
`default.conf`:

```json
{"overlay_conf": "macos.conf"}
```

The overlay replaces repeated `layer.key` entries and leaves other base entries
unchanged. Startup keys can override the configured file: `a` selects
`android.conf`, `w` selects `windows.conf`, `m` selects `macos.conf`, and `d`
clears the overlay.
