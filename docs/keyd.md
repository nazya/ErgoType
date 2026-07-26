# Embedded keyd

The firmware loads `default.conf` from LittleFS once at startup and optionally
loads `overlay_conf` over it. It supports the embedded keyd layers, layouts,
aliases, chords, macros, timeouts, overloads, `setlayout()`/`setlayoutm()`, and
the ErgoType extensions present in `keyd/port/config.c`.

This is not a Linux keyd daemon: there is no IPC, monitor, reload, listener or
uinput path. Output is translated by `keyd/port/vkbd/tusb_hid.c` into BLE HID NKRO,
Consumer Control, mouse and scroll events
through the Zephyr backend. Layout changes update the local UI; the initial
layout is retained even when keyd starts before the UI thread.

Startup keys `a`, `w`, `m`, and `d` select `android.conf`, `windows.conf`,
`macos.conf`, or no overlay when exactly one non-encoder key is held. The
legacy boot-profile key `l` has no nRF behavior.
