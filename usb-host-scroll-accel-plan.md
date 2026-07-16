# Plan: USB Host Input Frames, High-Resolution Scroll, and Acceleration Profiles

Date: 2026-07-08  
Branch when written: `hid-host-pio-bringup`

Update: the current architectural choice is not to embed this pipeline into
`keyd/port`. The new layer should live separately at the USB Host / local
pointing input boundary and have a replaceable output: no-op, direct HID output,
or a future libinput/libinput-like backend. `keyd` remains a separate remapper
path with its own scroll semantics.

This document describes a work plan. It is not, by itself, permission to change
logic. Every stage that changes firmware behavior must be confirmed separately
before implementation.

## Goal

Build one clear input pipeline for:

- USB Host HID devices that go through the ported Linux HID/input layer.
- Local PMW pointing sensors that already send Linux-shaped `EV_REL` +
  `EV_SYN` events into the keyd queue.
- Legacy wheel input (`REL_WHEEL`, `REL_HWHEEL`).
- High-resolution wheel input (`REL_WHEEL_HI_RES`, `REL_HWHEEL_HI_RES`).
- Pointer acceleration profiles from the `accel_profiles` branch.
- Future scroll acceleration compatible with the libinput custom-profile model.

Main idea: the USB Host/Linux layer should remain the source of evdev events,
`keyd/port/device.c` should become the place where frames are normalized around
`EV_SYN/SYN_REPORT`, and acceleration should be applied after that
normalization but before output to `vkbd`.

## What Must Not Be Confused Right Now

### Upstream keyd

The local upstream keyd tree is:

- `~/keyd/src/device.c`

In upstream keyd:

- `REL_X` and `REL_Y` accumulate in `dev->_pending_rel_x/y`.
- `DEV_MOUSE_MOVE(x, y)` is emitted only on `EV_SYN`.
- `REL_WHEEL` and `REL_HWHEEL` are emitted immediately.
- `REL_WHEEL_HI_RES` and `REL_HWHEEL_HI_RES` are left as TODO.

So upstream keyd already groups relative motion correctly, but it is not a
complete example for high-resolution scroll. For scroll we do not just need to
"return to upstream"; we need to finish what upstream keyd did not implement.

### Linux HID/input

The current repository already has a ported Linux HID/input layer:

- `usb_host/linux/drivers/hid/hid-input.c`
- `usb_host/linux/drivers/input/input.c`
- `usb_host/evdev.c`
- `usb_host/evdev_queue.c`

Linux HID already understands high-resolution wheel input:

- `REL_WHEEL_HI_RES`
- `REL_HWHEEL_HI_RES`
- `v120` units, where `120` means one legacy wheel detent.

Key code:

- `usb_host/linux/drivers/hid/hid-input.c:1502` `hidinput_handle_scroll(...)`
- `usb_host/linux/include/uapi/linux/input-event-codes.h:862`
- `usb_host/linux/include/uapi/linux/input-event-codes.h:863`

`hidinput_handle_scroll(...)` does the important work: it converts HID wheel
values and resolution multiplier into high-resolution v120 values, keeps the
remainder, and also emits legacy low-resolution `REL_WHEEL` / `REL_HWHEEL`
when a full detent has accumulated.

Implication: do not write a custom wheel mini-parser in the USB Host parser. If
the Linux slice already emitted `REL_WHEEL_HI_RES`, preserve it higher in the
pipeline.

### libinput

The local libinput tree is:

- `~/libinput`

Useful locations:

- `~/libinput/src/evdev-fallback.c`
- `~/libinput/src/libinput-plugin-mouse-wheel.c`
- `~/libinput/src/libinput-plugin-mouse-wheel-lowres.c`
- `~/libinput/src/filter-custom.c`
- `~/libinput/src/filter-mouse.c`
- `~/libinput/src/filter-flat.c`

libinput model:

- `SYN_REPORT` is the frame boundary.
- Low-resolution and high-resolution wheel events may appear at the same time.
- They must not simply be added as two scroll movements.
- If a high-resolution stream exists, it is the primary precise scroll stream.
- The low-resolution stream remains a legacy/fallback stream.
- If no high-resolution stream exists, low-resolution wheel input can be
  normalized as `value * 120`.
- Adaptive/flat pointer acceleration in libinput does not accelerate physical
  wheel units like normal motion.
- The custom acceleration profile supports a separate movement type,
  `LIBINPUT_ACCEL_TYPE_SCROLL`.

Documentation:

- Linux input event model:
  <https://docs.kernel.org/input/event-codes.html>
- libinput wheel API:
  <https://wayland.freedesktop.org/libinput/doc/latest/wheel-api.html>
- libinput pointer acceleration:
  <https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html>

### Current Output Backend

The firmware currently sends scroll to the host as legacy HID mouse wheel/pan:

- `keyd/port/daemon.c`
- `keyd/port/vkbd/vkbd.c`
- `keyd/port/vkbd/tusb_hid.c`
- `tusb/usb_descriptors.c`

`keyd/port/vkbd/tusb_hid.c` sends scroll through:

```c
tud_hid_n_mouse_report(..., wheel, pan)
```

This is legacy wheel/pan output. It does not send a high-resolution v120 stream
to the host.

Implication: the near-term implementation can preserve high-resolution data
inside the firmware, apply scroll acceleration, and accumulate remainders, but
the USB-device output still has to emit whole detents. True high-resolution
output to the host needs a separate HID descriptor/report-format decision.
Under current project rules, that is a separate stage and `tusb/usb_descriptors.*`
must not be touched without explicit confirmation.

## Target Architecture

The pipeline should look like this:

```text
USB mouse/trackball/etc.
  -> usb_host/linux/drivers/hid/*
  -> usb_host/linux/drivers/input/*
  -> usb_host/evdev.c / usb_host/evdev_queue.c
  -> keyd/port/device.c
  -> frame-normalized device events
  -> acceleration/filter layer
  -> keyd/port/daemon.c
  -> keyd keyboard actions / scroll actions / vkbd
  -> keyd/port/vkbd/*
  -> TinyUSB HID device reports
```

Local PMW sensors should enter the same pipeline:

```text
PMW3360/PMW3389
  -> pointing/pointer.c
  -> EV_REL REL_X/Y or REL_WHEEL/HWHEEL
  -> EV_SYN SYN_REPORT
  -> keyd/port/device.c
  -> the same normalization and acceleration
```

Important: `pointing/pointer.c` must not be the final place for acceleration
profiles, because then USB Host mice and local PMW sensors would have different
processing paths.

## Main Invariants

1. `usb_host/linux/*` remains a Linux-shaped layer.
2. Do not move libinput-style acceleration into `usb_host/linux/*`.
3. Do not write a custom HID wheel mini-parser next to TinyUSB host callbacks.
4. `EV_SYN/SYN_REPORT` is the only frame boundary for mouse/scroll events.
5. Keep `REL_WHEEL_HI_RES` and `REL_HWHEEL_HI_RES` inside the firmware as v120.
6. If high-resolution and low-resolution events arrive on the same axis in one
   frame, do not add them together.
7. If high-resolution is absent, the low-resolution fallback is `low_res * 120`.
8. Acceleration must be per-device because filter state depends on previous
   events, timing, and residues.
9. Scroll residue must also be per-device.
10. Until a USB descriptor output decision is made, output only legacy detents
    to the host, but do not discard v120 inside the pipeline too early.

## Stage 0. Record the Baseline

Goal: before changing behavior, know the exact current state and avoid mixing
other dirty changes with this work.

Actions:

1. Check the branch:

   ```sh
   git branch --show-current
   ```

2. Check dirty state:

   ```sh
   git status --short
   ```

3. Inspect the current diff in `keyd/port/device.c`, because the working tree
   may already contain a change that restores upstream-style accumulation of
   `REL_X/Y` until `EV_SYN`:

   ```sh
   git diff -- keyd/port/device.c
   ```

4. Compare with upstream keyd:

   ```sh
   sed -n '420,520p' ~/keyd/src/device.c
   ```

5. Compare with current `HEAD` to understand which changes are already present
   in the working tree:

   ```sh
   git show HEAD:keyd/port/device.c
   ```

6. Build the current firmware before new changes:

   ```sh
   cmake --build build -j4
   ```

Readiness criteria:

- It is clear which files are already dirty.
- The next stage starts from the current working tree without reverting other
  people's changes.
- There is a build baseline.

## Stage 1. Restore Frame Semantics for Relative Motion

Goal: `REL_X` and `REL_Y` should accumulate until `EV_SYN/SYN_REPORT`, then one
`DEV_MOUSE_MOVE(x, y)` should be emitted for the frame.

Current state:

- Upstream keyd does exactly this.
- The current working tree's `keyd/port/device.c` already looks partially
  restored.
- Do not rewrite it from scratch; confirm and finish the coherent shape.

Actions:

1. In `keyd/port/device.c`, make sure that:

   - `REL_X` does `dev->_pending_rel_x += ev.value` and returns `NULL`.
   - `REL_Y` does `dev->_pending_rel_y += ev.value` and returns `NULL`.
   - `EV_SYN` returns one `DEV_MOUSE_MOVE` if pending values are nonzero.
   - Pending values are reset after emit.

2. In `keyd/port/device.h`, make sure the pending motion fields exist:

   - `_pending_rel_x`
   - `_pending_rel_y`

3. Check that `pointing/pointer.c` already sends `EV_SYN/SYN_REPORT` after a
   local PMW motion frame.

4. Check that the USB Host path actually delivers `EV_SYN` to the queue:

   - `usb_host/evdev.c`
   - `usb_host/evdev_queue.c`
   - `usb_host/linux/drivers/input/input.c`

5. Do not change ABS or wheel behavior in this stage.

Readiness criteria:

- One physical frame with `REL_X=5`, `REL_Y=-3`, `SYN_REPORT` produces one
  `DEV_MOUSE_MOVE(5, -3)`.
- Two separate frames produce two separate `DEV_MOUSE_MOVE` events.
- No acceleration, no scroll changes, no descriptor changes.

## Stage 2. Design Framed Output from `device.c`

Goal: prepare `keyd/port/device.c` for a single `EV_SYN` producing more than
one high-level event.

Why this is needed:

- Right now `device_read_event()` returns at most one `struct device_event`.
- While wheel is emitted immediately, this limitation is mostly hidden.
- If scroll is also accumulated until `EV_SYN`, one frame may contain:

  - motion,
  - vertical scroll,
  - horizontal scroll,
  - absolute coordinate update.

- If `EV_SYN` returns only motion, scroll may remain pending without another
  raw event in the FreeRTOS queue. It will either hang until the next input
  event or be lost in a later rewrite.

Recommended option:

- Add a small pending output buffer for already-built `struct device_event`
  values inside `struct device`.
- `device_read_event()` first returns pending output events.
- If pending output is empty, read raw `struct input_event` from `ev_queue`.
- On `EV_SYN/SYN_REPORT`, the raw pending frame becomes one or more pending
  output events.
- `evloop.c` must drain pending device output before blocking on
  `xQueueSelectFromSet`.

Why leaving it as-is is not enough:

- The queue set wakes from raw `ev_queue`.
- Pending high-level output inside `struct device` will not wake the queue set
  on its own.
- Therefore `evloop.c` must explicitly check pending device output before
  waiting for a new raw queue event.

Minimal API shape:

- Add a helper in `device.h`, conceptually:

  ```text
  device_has_pending_event(dev)
  ```

- In `evloop.c`, before `xQueueSelectFromSet(...)`, walk `device_table` and
  emit pending output events if any exist.

Pending output buffer size:

- At least three events per frame:

  - `DEV_MOUSE_MOVE`
  - `DEV_MOUSE_SCROLL` or future `DEV_MOUSE_SCROLL_V120`
  - `DEV_MOUSE_MOVE_ABS`

- If horizontal and vertical scroll are not split into separate events, one
  scroll event is enough because `x` and `y` are already present in
  `struct device_event`.

Readiness criteria:

- A frame with motion and scroll delivers both high-level events without
  waiting for the next raw input event.
- Ordering is stable and documented:

  1. motion,
  2. absolute motion,
  3. scroll.

The order can be changed, but it must be consistent in all cases.

## Stage 3. Introduce Canonical v120 Scroll Representation

Goal: preserve high-resolution wheel data and avoid double-scroll for devices
that emit low-resolution and high-resolution streams at the same time.

New per-device pending fields:

- pending low-resolution vertical wheel detents,
- pending low-resolution horizontal wheel detents,
- pending high-resolution vertical wheel v120,
- pending high-resolution horizontal wheel v120,
- flag: high-resolution vertical was seen in this frame,
- flag: high-resolution horizontal was seen in this frame.

Logic for raw `EV_REL`:

```text
REL_WHEEL:
  pending_scroll_lo_y += ev.value

REL_HWHEEL:
  pending_scroll_lo_x += ev.value

REL_WHEEL_HI_RES:
  pending_scroll_hi_y += ev.value
  saw_hi_res_y = true

REL_HWHEEL_HI_RES:
  pending_scroll_hi_x += ev.value
  saw_hi_res_x = true
```

Logic on `EV_SYN/SYN_REPORT`:

```text
if saw_hi_res_y:
  frame_scroll_v120_y = pending_scroll_hi_y
else:
  frame_scroll_v120_y = pending_scroll_lo_y * 120

if saw_hi_res_x:
  frame_scroll_v120_x = pending_scroll_hi_x
else:
  frame_scroll_v120_x = pending_scroll_lo_x * 120
```

Important:

- Do not add low-resolution and high-resolution values on the same axis.
- Decide independently for vertical and horizontal axes.
- If vertical has high-resolution input and horizontal only has low-resolution
  input, use high-resolution for vertical and `low_res * 120` for horizontal.
- After frame flush, reset all per-frame pending fields and flags.

High-level event API options:

### Option A: add `DEV_MOUSE_SCROLL_V120`

Recommended.

Meaning:

- `DEV_MOUSE_SCROLL` remains legacy detents.
- `DEV_MOUSE_SCROLL_V120` means canonical v120 units.
- Acceleration receives v120 and may return v120.
- Legacy output conversion happens later, closer to `vkbd`.

Pros:

- Does not break old `DEV_MOUSE_SCROLL` semantics.
- Does not lose precision before acceleration.
- Allows the high-resolution pipeline to be tested separately.

Cons:

- Requires extending `enum device_event.type`.
- Requires handling the new type in `daemon.c`.

### Option B: redefine `DEV_MOUSE_SCROLL` as v120

Not recommended.

Problem:

- Today `DEV_MOUSE_SCROLL.x/y` means whole detents.
- `daemon.c` loops over each detent and calls `KEYD_SCROLL_*`.
- If `40` is suddenly put there, firmware will send 40 scroll actions instead
  of one third of a detent.

### Option C: collapse v120 into detents inside `device.c`

Not recommended for acceleration.

Problem:

- High-resolution precision is lost before the filter layer.
- Scroll acceleration cannot work with fractional wheel movements.
- Behavior becomes almost the same as ordinary low-resolution wheel input.

Readiness criteria:

- `REL_WHEEL=1`, `SYN_REPORT` becomes `scroll_v120_y=120`.
- `REL_WHEEL_HI_RES=40`, `SYN_REPORT` becomes `scroll_v120_y=40`.
- `REL_WHEEL=1` + `REL_WHEEL_HI_RES=120` in the same frame becomes
  `scroll_v120_y=120`, not `240`.
- The same works for the horizontal axis.

## Stage 4. Legacy Output Conversion with Residue

Goal: while USB-device output remains legacy wheel/pan, high-resolution scroll
should accumulate cleanly into whole detents.

Where to store residue:

- Per-device.
- After acceleration.
- Before `vkbd_mouse_scroll(...)`.

Why after acceleration:

- If `40 + 40 + 40` is collapsed into one detent first and accelerated later,
  the filter no longer sees the actual frequency and size of high-resolution
  movement.
- If each `40` is accelerated in v120 units and residue is accumulated after
  that, the profile gets proper continuous input.

Conversion logic:

```text
scroll_residue_x_v120 += accelerated_scroll_x_v120
scroll_residue_y_v120 += accelerated_scroll_y_v120

detents_x = scroll_residue_x_v120 / 120
detents_y = scroll_residue_y_v120 / 120

scroll_residue_x_v120 -= detents_x * 120
scroll_residue_y_v120 -= detents_y * 120

if detents_x || detents_y:
  emit legacy DEV_MOUSE_SCROLL or call vkbd_mouse_scroll
```

Important for C:

- Integer division truncates toward zero.
- For signed residue this is usually correct if residue is always reduced by
  `detents * 120`.
- Explicitly test negative cases:

  - `-40`, `-40`, `-40` -> `-1 detent`, residue `0`.
  - `+80`, `-40` -> no detent, residue `+40`.
  - `+40`, `-80` -> no detent, residue `-40`.

Where to integrate:

- If `DEV_MOUSE_SCROLL_V120` is chosen, conversion is best done in `daemon.c`
  or in a small input filter/helper called from `daemon.c`.
- Do not do conversion in `usb_host/linux/*`.
- Do not do conversion in `pointing/pointer.c`.

Readiness criteria:

- High-resolution wheel `40, 40, 40` produces one legacy wheel tick.
- High-resolution wheel `60, 60` produces one legacy wheel tick.
- High-resolution wheel `30, 30, 30` produces no tick until the fourth `30`.
- Direction changes do not create extra scroll.

## Stage 5. Move Acceleration Profiles into the Correct Layer

Goal: use the port from the `accel_profiles` branch without tying it only to
local PMW sensors.

Wrong final location:

- `pointing/pointer.c`

Why:

- Only local PMW devices are visible there.
- USB Host mice do not pass through that code.
- The result would be one mouse accelerated and another not.

Correct location:

- After `keyd/port/device.c`, once the raw evdev frame has become canonical
  `DEV_MOUSE_MOVE` / `DEV_MOUSE_SCROLL_V120`.
- Before `vkbd_mouse_move(...)` / `vkbd_mouse_scroll(...)`.
- Practically this means integration near `keyd/port/daemon.c`, but state must
  be per-device, not global.

Why per-device:

- `evloop.c` passes `ev.dev` into `event_handler(...)`.
- Every physical device should have its own filter state.
- Every device has its own scroll residue.
- Different devices may have different CPI/resolution/behavior.

Recommended structure:

```text
keyd/port/device.c
  raw evdev -> framed device events

keyd/port/daemon.c
  receives ev.dev + ev.devev
  calls per-device pointer/scroll filter
  sends result to keyd action layer or vkbd

accel/filter code
  adapted from accel_profiles branch / libinput model
  stores state per struct device or per-device sidecar
```

File placement:

- If the code from `accel_profiles` currently lives under `pointing/accel/*`,
  it should not stay under `pointing` after the move, because it is no longer
  sensor-driver logic.
- Possible locations:

  - `keyd/port/accel/*`
  - `keyd/port/input_filter.*`
  - `pointing/accel/*` only if its meaning is renamed into a project-wide input
    filter, but then the path will be misleading.

Recommendation:

- For the first implementation, choose `keyd/port/accel/*` or one
  `keyd/port/input_filter.c` so the boundary is clearly keyd input, not the PMW
  driver.

Readiness criteria:

- USB Host mouse and PMW mouse go through the same acceleration path.
- Disabled acceleration gives byte-for-byte previous deltas on output.
- Enabled acceleration changes only motion/scroll, not key events.

## Stage 6. Separate Pointer Motion Acceleration and Wheel Scroll Acceleration

Goal: do not blindly apply a mouse-motion profile to a physical wheel.

Pointer motion:

- Input: `DEV_MOUSE_MOVE.x/y`.
- Units: relative device counts.
- Filter type: motion.
- Output: accelerated dx/dy.
- Then:

  - if `kbd->scroll.active == false`, send `vkbd_mouse_move(...)`;
  - if `kbd->scroll.active == true`, use keyd scroll mode.

Physical wheel scroll:

- Input: `DEV_MOUSE_SCROLL_V120.x/y`.
- Units: v120.
- Filter type: scroll.
- Output: accelerated v120.
- Then residue conversion into legacy detents.

Important:

- In libinput, adaptive/flat wheel input is usually not accelerated as pointer
  motion.
- Wheel acceleration needs a custom scroll profile or an explicit rule.
- If no custom profile is configured for scroll, fallback must be clear:

  - pass-through, or
  - custom fallback profile if it is explicitly configured for all movement
    types.

Keyd scroll mode:

- This is a separate case: `kbd->scroll.active` turns mouse motion into scroll.
- It is not a physical wheel.
- It can be treated like continuous/button-scroll-like movement.
- The first implementation should leave it as-is to avoid mixing two tasks.
- A later stage can decide whether motion-to-scroll should go through:

  - pointer motion acceleration before conversion to scroll,
  - a separate scroll profile after conversion,
  - or remain raw as it is today.

Recommendation for the first version:

1. Pointer acceleration applies to `DEV_MOUSE_MOVE`.
2. Physical wheel acceleration applies only to `DEV_MOUSE_SCROLL_V120`.
3. Leave `kbd->scroll.active` on the current logic first.
4. After stabilization, discuss acceleration for keyd scroll mode separately.

Readiness criteria:

- Pointer acceleration does not break physical wheel input.
- Scroll acceleration does not alter mouse movement.
- Keyd scroll mode does not receive unexpected double acceleration.

## Stage 7. Acceleration Configuration

Goal: add a controllable config surface only after the pipeline works with a
hardcoded/no-op profile.

Recommended order:

1. Start with a hardcoded pass-through profile.
2. Then add one simple compile-time or default profile for smoke testing.
3. Then add JSON config.

Why not start with config:

- First prove that event semantics are correct.
- Config adds parser/validation noise and makes review harder.

Where to store config:

- Most likely `config.json`, because this is firmware/device behavior.
- Not `default.conf`, because keyd config describes key remapping actions,
  while acceleration is closer to input device behavior.

Minimal future config shape:

```json
{
  "pointer_accel": {
    "enabled": true,
    "profile": "adaptive",
    "speed": 0
  },
  "scroll_accel": {
    "enabled": false,
    "profile": "custom",
    "points": []
  }
}
```

This is only a sketch. Before implementing config, agree on:

- field names,
- default values,
- per-device or global behavior,
- how to distinguish a PMW mouse from a USB Host mouse,
- how to store custom profile points compactly.

Readiness criteria:

- Without config, firmware behaves as before.
- With config, pointer acceleration can be enabled/disabled.
- Scroll acceleration can remain disabled until separately confirmed.

## Stage 8. ABS Frame Semantics

Goal: carefully decide whether `ABS_X/Y` should also be framed now.

Current state:

- Upstream keyd emits `ABS_X` and `ABS_Y` immediately.
- Our `struct device` already has fields:

  - `_pending_abs_x`
  - `_pending_abs_y`
  - `_pending_abs`

- But the current active ABS logic in `device.c` emits axes immediately.

Risk:

- If a device sends `ABS_X`, `ABS_Y`, `SYN_REPORT`, immediate emit gives two
  separate `DEV_MOUSE_MOVE_ABS` events instead of one coherent absolute-position
  frame.

Recommendation:

- Do not mix ABS into the first scroll/acceleration stage unless there is a
  concrete ABS device bug right now.
- After the relative/scroll pipeline, make a separate ABS patch:

  1. On `ABS_X`, store normalized x and set flag.
  2. On `ABS_Y`, store normalized y and set flag.
  3. On `EV_SYN`, emit one `DEV_MOUSE_MOVE_ABS(x, y)` if there was an ABS
     update.
  4. Decide what to do if only one axis arrived:

     - emit only the changed axis with the last known other axis, or
     - store the full current absolute position per-device.

Readiness criteria:

- A touchpad/tablet-like absolute device does not get split X/Y updates.
- No changes for relative mouse/scroll behavior.

## Stage 9. Test Scenarios

Check event semantics, not only the build.

Minimal synthetic set:

### Relative Motion

```text
REL_X 5
REL_Y -3
SYN_REPORT
```

Expected:

```text
DEV_MOUSE_MOVE x=5 y=-3
```

### Separate Frames

```text
REL_X 5
SYN_REPORT
REL_Y -3
SYN_REPORT
```

Expected:

```text
DEV_MOUSE_MOVE x=5 y=0
DEV_MOUSE_MOVE x=0 y=-3
```

### Low-Resolution Wheel Fallback

```text
REL_WHEEL 1
SYN_REPORT
```

Expected:

```text
DEV_MOUSE_SCROLL_V120 y=120
legacy output y=1
```

### High-Resolution Wheel Accumulation

```text
REL_WHEEL_HI_RES 40
SYN_REPORT
REL_WHEEL_HI_RES 40
SYN_REPORT
REL_WHEEL_HI_RES 40
SYN_REPORT
```

Expected:

```text
three v120 frames: 40, 40, 40
one legacy output tick after accumulated 120
```

### High-Resolution Plus Low-Resolution in the Same Frame

```text
REL_WHEEL 1
REL_WHEEL_HI_RES 120
SYN_REPORT
```

Expected:

```text
canonical y=120
not y=240
legacy output y=1
```

### Horizontal High-Resolution

```text
REL_HWHEEL_HI_RES -40
SYN_REPORT
REL_HWHEEL_HI_RES -40
SYN_REPORT
REL_HWHEEL_HI_RES -40
SYN_REPORT
```

Expected:

```text
legacy output x=-1 after third frame
```

### Motion Plus Scroll in the Same Frame

```text
REL_X 2
REL_Y 1
REL_WHEEL_HI_RES 40
SYN_REPORT
```

Expected:

```text
DEV_MOUSE_MOVE x=2 y=1
DEV_MOUSE_SCROLL_V120 y=40
neither event waits for the next raw input event
```

### Direction Change Residue

```text
REL_WHEEL_HI_RES 80
SYN_REPORT
REL_WHEEL_HI_RES -40
SYN_REPORT
```

Expected:

```text
no legacy output
residue y=40
```

### Negative Detent

```text
REL_WHEEL_HI_RES -40
SYN_REPORT
REL_WHEEL_HI_RES -40
SYN_REPORT
REL_WHEEL_HI_RES -40
SYN_REPORT
```

Expected:

```text
legacy output y=-1
residue y=0
```

## Stage 10. Verification After Every Behavioral Patch

Required checks:

1. Build:

   ```sh
   cmake --build build -j4
   ```

2. Static check:

   ```sh
   git diff --check
   ```

3. Focused diff review:

   ```sh
   git diff -- keyd/port/device.c keyd/port/device.h keyd/port/daemon.c keyd/port/evloop.c
   ```

4. If accel code is moved:

   ```sh
   git diff -- pointing keyd/port
   ```

5. If the USB Host Linux slice is touched:

   - explain separately why it is really needed;
   - check that the upstream path was not removed;
   - keep the upstream block next to the port-specific replacement if a port
     difference is required.

Forbidden without separate confirmation:

- changing `tusb/usb_descriptors.c`;
- changing `tusb/usb_descriptors.h`;
- adding a high-resolution HID output descriptor;
- rewriting Linux HID wheel handling;
- moving acceleration only into `pointing/pointer.c`.

## Suggested Patch Breakdown

### Patch 1: Relative Motion Frame Parity with Upstream keyd

Scope:

- `keyd/port/device.c`
- maybe `keyd/port/device.h` if fields are missing

Behavior:

- `REL_X/Y` accumulate until `SYN_REPORT`.
- One `DEV_MOUSE_MOVE` per frame.

No scroll changes.

### Patch 2: Pending High-Level Device Output Drain

Scope:

- `keyd/port/device.c`
- `keyd/port/device.h`
- `keyd/port/evloop.c`

Behavior:

- One raw `SYN_REPORT` can generate multiple high-level events.
- `evloop.c` drains pending device output before blocking.

No high-resolution scroll yet.

### Patch 3: Canonical v120 Scroll Event

Scope:

- `keyd/port/device.c`
- `keyd/port/device.h`
- `keyd/port/daemon.c`

Behavior:

- Add `DEV_MOUSE_SCROLL_V120`.
- `REL_WHEEL/HWHEEL` become fallback `* 120`.
- `REL_WHEEL_HI_RES/HWHEEL_HI_RES` are preserved.
- No double-scroll when both streams arrive in one frame.

### Patch 4: v120 Residue to Legacy Output

Scope:

- probably `keyd/port/daemon.c`
- maybe per-device state in `struct device`

Behavior:

- Accumulate v120 residue per device.
- Emit legacy `vkbd_mouse_scroll(...)` only for full detents.

No acceleration yet.

### Patch 5: Move/Import Acceleration Profile Code

Scope:

- new or moved accel/filter files under `keyd/port/`
- `keyd/port/daemon.c`
- build system

Behavior:

- Default profile must be pass-through.
- Pointer path and scroll path call filter hooks, but output remains unchanged
  when disabled.

### Patch 6: Enable Pointer Acceleration

Scope:

- accel/filter files
- `keyd/port/daemon.c`
- maybe config later

Behavior:

- `DEV_MOUSE_MOVE` gets motion acceleration.
- USB Host and PMW devices share the same acceleration path.

### Patch 7: Enable Scroll Acceleration

Scope:

- accel/filter files
- `keyd/port/daemon.c`

Behavior:

- `DEV_MOUSE_SCROLL_V120` gets custom scroll acceleration.
- Adaptive/flat wheel remains pass-through unless explicitly designed
  otherwise.

### Patch 8: Config Surface

Scope:

- `jconfig.c`
- `jconfig.h`
- docs
- maybe accel/filter initialization

Behavior:

- User can enable/disable profiles.
- Defaults preserve previous behavior.

### Patch 9: ABS Frame Semantics

Scope:

- `keyd/port/device.c`
- `keyd/port/device.h`

Behavior:

- `ABS_X/Y` can be emitted as coherent frame-level absolute movement.

Do this only after scroll/motion is stable or if a real ABS device needs it.

## Open Decisions Before Coding

1. Use `DEV_MOUSE_SCROLL_V120` or introduce a larger `DEV_POINTER_FRAME` event?

   Recommendation: `DEV_MOUSE_SCROLL_V120`, because it preserves current
   `DEV_MOUSE_SCROLL` semantics and keeps the patch easier to review.

2. Where exactly should filter state live?

   Recommendation: per `struct device`, either embedded directly or through a
   small sidecar pointer owned by the device.

3. Should keyd `scroll()` mode be accelerated?

   Recommendation: not in the first implementation. Keep it unchanged, then
   decide separately.

4. Should high-resolution scroll be exposed to the host as true HID
   high-resolution wheel?

   Recommendation: not now. The current goal is internal correctness and future
   readiness. Descriptor/report changes require separate explicit approval.

5. Should ABS be framed in the same patch as scroll?

   Recommendation: no. Do relative motion and scroll first.

## Final Target Behavior

When everything above is complete:

- USB Host mouse motion respects evdev `SYN_REPORT` frame semantics.
- PMW sensor motion and USB Host motion use one acceleration pipeline.
- High-resolution wheel input is preserved internally as v120.
- Low-resolution wheel input is normalized to v120 only when high-resolution is
  absent.
- Devices that emit both low-resolution and high-resolution wheel events do not
  double-scroll.
- Scroll acceleration can operate on v120 before legacy output conversion.
- Current TinyUSB mouse output still works as legacy wheel/pan.
- Future true high-resolution USB-device output remains possible without
  rewriting the input pipeline again.
