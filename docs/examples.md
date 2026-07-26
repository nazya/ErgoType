# Examples

Minimal matrix:

```json
{
  "gpio_rows": [2, 3, 4],
  "gpio_cols": [5, 6],
  "keymap": [
    ["q", "w"],
    ["a", "s"],
    ["leftshift", "space"]
  ],
  "scan_period": 5,
  "debounce": 9
}
```

Add an overlay with `"overlay_conf": "macos.conf"`; see
[keyd overlays](keyd-overlays.md). A PMW and acceleration example is in
[the configuration reference](config-json.md).
