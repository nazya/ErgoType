# HID Emulator Coverage

This note tracks what the external emulator repo covers:
`../ErgoType-hid-devices`.

This file is the authority for fixtures and observed hardware signals, not the
current host implementation, TinyUSB patch inventory, or RAM accounting. A
recorded SHA remains attached only to the verdict from that exact test.

It is intentionally about hardware-test coverage, not about every active HID
driver in CMake. The current host build links the CMake HID allowlist from
`usb_host/linux/drivers/hid`, while the emulator repo has one branch per
targeted behavior or device family.

The AppleIR entry below retains historical coverage from checkpoint `hid: stabilize stadia ff teardown` and
remains unlinked. Stadia/`ff-memless` is also outside the current CMake
allowlist, but its retained mutex conversion was retested before deferral on
2026-07-22.

### Hardware-verified Microsoft wired-USB fixture

The compact `device/microsoft-usb` fixture is based on commit `c4e5361` plus
the current initial-profile activation-delay working-tree change. It groups
the 14 active Microsoft IDs by six distinct driver behaviors instead of
reconnecting once per identical quirk row:

- Office Keyboard `045e:0048` covers every ergonomic mapping/event branch,
  all F14--F18 selectors, and two simultaneous HID interfaces with interleaved
  per-device `last_key` ownership;
- Presenter `045e:0713` covers its five vendor controls separately;
- LK6K `045e:00f9` supplies exactly 571 descriptor bytes with
  `rdesc[557]=0x19` and `rdesc[559]=0x29`, then sends A/B/C;
- Wireless Optical Desktop `045e:009d` rejects any GET_REPORT and requires
  exactly one feature SET_REPORT for ID 9 with payload `0x05`;
- Comfort Mouse `045e:076c` asserts only the second duplicate Button 1 usage;
- Power Cover `045e:07da` covers forced HIDINPUT and one same-PID reconnect.

The fixture changes descriptors only while D+ is down and waits for every
report completion. Terminal `f10` means the device-side sequence completed;
`f12` followed by `a` through `f` identifies the failed phase. Its initial
Office profile now uses the same post-mount activation delay as every
reconnect profile, after the first hardware run proved that device-side mount
can precede the host's final input activation.

The first complete-input run used host
`360637adc1b5b5d829f75ac85d99b50eeffcb5930bc442d7629d9baa7af11fe2`
and emulator
`10ac61fa8725694d7061425855d33b50dc50547b33ac778609933efa84d1fde2`.
The complete sequence reached `f10` with balanced Microsoft-profile removal,
`oom=0`, and no host `ERR`, but the run rejected pinned upstream's
function-static ownership: the interleaved F14 remained pressed until
input-device disconnect.

The current per-device fix is built as:

```text
host UF2 SHA-256       f809966886aa4bdaf42c694055ea08b9313d4dbd1120f1a96f0cebd8ba71fd48
emulator UF2 SHA-256   10ac61fa8725694d7061425855d33b50dc50547b33ac778609933efa84d1fde2
emulator text/data/bss 59364 / 0 / 255012 B
hardware verdict       passed 2026-07-31
```

The exact pair passed on hardware on 2026-07-31. All eight Microsoft interface
lifetimes were balanced, including two simultaneous Office Keyboard
interfaces and two clean Power Cover generations. The decisive interleave was
F14 down, F15 down, F14 up, F15 up before either Office interface was removed.
The fixture reached terminal `f10` with no `f12`, host `ERR`, `WARN`, or OOM.
Free heap returned to 60752 bytes after each complete profile; the recorded
minimum-ever free heap was 47592 bytes. Minimum free task watermarks remained
nonzero: TUH 265, KeyD 658, async 430, work 346, timer 348, lifecycle 171, and
report 862 words.

The repeated `unrecognized evdev event type: 4` lines are the existing
downstream KeyD boundary for Linux `EV_MSC`/`MSC_SCAN`; every relevant line was
followed by the expected mapped key event. Packed-wheel output has no
production CDC marker and remains source-audited unless observed at the
firmware's USB output.

### Hardware-verified Apple external-USB host and focused fixture

The Apple host selects 18 external USB keyboard/Mighty Mouse IDs,
and the focused fixture is intentionally limited to four distinct behaviors
rather than one
reconnect per PID:

- Aluminum Rev B `05ac:024f` for Fn translation and `ALWAYS_POLL`;
- Mighty Mouse `05ac:0304` for button swap and inverted horizontal wheel;
- Magic Keyboard 2015 `05ac:0267` for the older key table, ISO/JIS correction,
  the protocol-equivalent 83-byte battery-fixup signature, and cancellation
  before its deadline;
- Magic Keyboard 2024 `05ac:0320` for the newer F4/F5/F6 table, one real
  60-second battery deadline, disconnect during GET_REPORT, and reconnect.

The exact hardware-verified artifacts are:

```text
host UF2 SHA-256       e55bc54d78acc77e14548d6f7dfc6f816fe8d00f712803c7aef9fb977cc5f096
host text/data/bss     600920 / 788 / 245360 B
emulator branch        device/apple-external-usb
emulator UF2 SHA-256   6e6ba43c7efad0677a372079f079701e5fa51f8cb65c30f4321a3fbaafe72255
emulator text/data/bss 60296 / 0 / 254452 B
emulator UF2 size      120832 B
hardware verdict       passed 2026-07-31
```

The regular `hid_hw_request()` GET is rounded to EP0 maxpacket by the existing
upstream-shaped transport. With `bMaxPacketSize0=64`, the fixture requires
setup `a1 01 84 01 01 00 40 00`; this is not the exact-length raw-request
contract used by other drivers. Its terminal `f10` is internally gated by the
immediate GET, exact second-GET setup near 60 seconds, canceled queued SETUP
callback, and fresh GET after reconnect. Production CDC does not log the raw
GET itself. The exact pair reached terminal `f10` after all of those gates,
with no `f12` or host `ERR`. Aluminum, Mighty Mouse buttons, and both Magic
Keyboard profiles produced their expected host-visible mapped input. The
fixture also delivered both signs of relative Z, but production CDC has no
REL_HWHEEL marker, so the inversion remains source-audited. The 2024 profile
disconnected with the delayed GET queued, reconnected, completed a fresh
immediate GET, and returned to the same 60496-byte removal plateau. The run
reported `oom=0`, a 38224-byte minimum-ever free heap, and nonzero minimum task
watermarks: TUH 265, KeyD 658, async 395, work 346, timer 315, lifecycle 220,
and report 870 words. Exact detached power-snapshot values remain outside the
production-log verdict.

### Measured ordinary-Logitech and Lenovo capacity pair

The combined fixture covers Logitech USB receivers `046d:c532/c52f/c534` and
external Lenovo USB TrackPoint keyboards `17ef:6009/6047/60ee` without
replaying the existing `c52b` matrix. Its exact corrected pair completed on
hardware with temporary `HID_MAX_FIELDS=64`, `HID_MAX_USAGES=256`.

The compact combined fixture covers only the newly reached behavior:

- Logitech `c532` as a real three-interface DJ receiver, including exact
  switch-to-DJ, notifications, paired-device query, child input, and unpair;
- Logitech `c52f` as a real two-interface mouse-only receiver, including its
  unnumbered high-resolution mouse report and full physical/virtual detach;
- Logitech `c534` as a real two-interface HID++ receiver, including its
  numbered mouse report, shared-interface lifetime, and reconnect;
- Lenovo `6009` keyboard plus TrackPoint, Button 16/F20, validated feature
  report 4 and output report 3, exact default feature payload, disconnect with
  the asynchronous SET pending, then one normal generation;
- Lenovo `6047` keyboard plus mouse, exact four feature commands, Fn-F12 raw
  fixup, middle-click/wheel arbitration, disconnect during the first
  synchronous SET, then one normal generation;
- Lenovo `60ee` keyboard, mouse, and third vendor/no-input HID interface,
  exact three feature commands, one report-ID-5 vendor key, input, and normal
  detach.

Gaming, Lightspeed/Powerplay, legacy 27 MHz, Bluetooth-proxy, Dinovo, Bolt,
and all non-USB Lenovo paths are outside this fixture. The exact temporary
capacity pair is:

```text
host parser policy     HID_MAX_FIELDS=64, HID_MAX_USAGES=256
host UF2 SHA-256       9546c46c0196718cfabd2d8b7662468598ed86701285d3a8eead81996dc0de88
emulator branch        device/logitech-lenovo-usb
emulator UF2 SHA-256   4fb530a84cec731a4cbde00fe22502384c8eedc9daacffc6e1fd68df9f216636
hardware verdict       passed at temporary 64/256; not retained-policy support
```

The run reached terminal `f10` with no host `ERR`, `oom=0`, balanced removals,
and nonzero task watermarks. `c52f` reached `min=17000`, `c534` reached
`min=9296`, and both returned to the 60,752-byte removal plateau. The corrected
full `60ee` graph left 11,152 bytes free and returned to 60,496 after removal.

That logic result exposed a retained-policy capacity failure. At 675 usages,
each Nano profile needs a 20,980-byte physical Consumer field and a
21,716-byte virtual Consumer field, 26,080 bytes more per pair than at 256.
The full `60ee` Consumer field alone grows another 13,408 bytes. Consequently
the RP2040 host now selects only `c52b/c532` and Lenovo `6009/6047`; the pinned
`c52f/c534/60ee` rows remain in source behind compile gates. The emulator is
unchanged and still presents all six profiles, so it is no longer a passing
automatic sequence for that reduced host allowlist. It stops at `c52f` because
generic HID sends none of the DJ startup reports; that unchanged run does not
reach or independently verify the `c534` gate or `60ee` allocation boundary.

### Hardware-verified work-input and long-enumeration fixture

Host checkpoint `hid: enable audited work-input drivers` enables the upstream-shaped `hid-elecom.c`,
`hid-kensington.c`, and `hid-topre.c` drivers. The host image used for this
hardware pass added the audited `hid-evision.c` receiver fixup and event-driven
long enumeration. The four driver-specific paths use the emulator repository
`../ErgoType-hid-devices` on branch
`device/work-input-drivers`; haptic/multitouch used the separate
`device/haptic-touchpad` pass recorded below.

One emulator image repeatedly disconnects and re-enumerates as five devices:

- ELECOM `056e:00fc`, with the real M-XT3DRBK three-button/five-padding defect
  and six-button reports;
- Kensington `047d:2041`, with Microsoft vendor usages 1 and 2 that only its
  input-mapping hook turns into middle/side buttons;
- Topre `0853:0313`, with the faithful 106-byte REALFORCE descriptor whose
  232-key bitmap is incorrectly marked Array at offsets 30..31.
- EVision TeLink `320f:226f`, with the exact 236-byte/offset-59 match signature
  whose three-button Usage Maximum is fixed to five.
- ErgoType `cafe:1005`, with a valid 600-byte configuration, its mouse HID
  interface beginning at byte 575, and a deliberate STALL on the first full
  configuration GET so enumeration must take the 100-ms retry continuation.

This directly checks all four fixup drivers, the Kensington mapping hook, a
nontrivial but bounded Topre parser allocation, the long-configuration buffer,
the error-only enumeration retry, and repeated lifecycle cleanup. None of the
four Linux drivers adds transport calls, tasks, heap allocations, or a new
Linux subsystem.

### Hardware-verified haptic lifecycle and Magic Trackpad 2 fixture

Repeated hot-plug and cold-start runs on 2026-07-23 verified:

- numbered interrupt OUT (`cafe:1006`), unnumbered report ID 0 interrupt OUT
  (`cafe:1007`), and unnumbered report ID 0 EP0 fallback (`cafe:1008`);
- all five HID Haptics waveforms, same-ID replay, in-place replacement,
  erase/re-upload, HOST/DEVICE mode changes, queued-work unplug, removal, and
  fresh-ID replay after reconnect;
- USB Magic Trackpad 2 probe on all four interfaces, `{ 0x02, 0x01 }` mode
  SET, native report `0x02` low-contact/release/button parsing, removal, and
  reconnect.

The runs completed with `oom=0` and a stable removal heap plateau. The
automatic compatibility pass below separately verifies unplug while the mode
SET is pending. Still unverified are accepted `-EIO` during that SET,
high-contact queue saturation/contact ID 32, and a real physical touchpad.

### Hardware-verified automatic USB keyboard compatibility pass

The emulator branch `device/usb-keyboard-test` contains one automatic
two-board test. It changes identity only while its own D+ pull-up is down, so
the operator does not need to unplug a cable or hit a timing window. The
emulator reconnects itself after two seconds where a reconnect is part of the
case. Results are visible only through the host board's existing CDC log.

The verified input sequence was:

| Host-visible input | Device/case proved |
| --- | --- |
| `a` | `cafe:1100`: BULK precedes INTERRUPT and duplicate same-direction interrupt endpoints do not hide the first usable interrupt IN/OUT pair |
| `b` | USB Magic Trackpad 2: D+ disappears after mode-SET data but before EP0 status, then a fresh mode SET completes after reconnect |
| `c` | `cafe:1102`: D+ disappears during endpoint `CLEAR_HALT`, reconnect completes, and input resumes |
| `d` | `cafe:1103`: `CLEAR_HALT` is rejected, terminal host reset/re-enumeration completes, and input resumes |
| `e`, `f`, `g` | valid configuration descriptors of exactly 512, 513, and 4096 bytes |
| no key; `ERR: HID_ENUM_CONFIG_TOO_LARGE` | `cafe:1107`: declared configuration length 4097 is rejected before a full transfer |
| no key; `ERR: HID_ENUM_CONFIG_INVALID` | `cafe:1108`: malformed `wTotalLength` is rejected without publishing a device |
| `h` | `cafe:1109`: a valid 4096-byte HID report descriptor is fetched and parsed |
| `f10` | every preceding case completed |

The `c` marker reached KeyD as `key c` and `input c`. No `vkbd output c` is
expected with the tested KeyD configuration because `c` is assigned to the
set-layout action rather than a forwarded key.

Three `f12` presses are the emulator's terminal failure marker. For the
terminal reset case, the normal diagnostic chain is
`HID_RX_STALL`, `HID_CLEAR_HALT_FAIL`, `HID_RESET_Q`, and `HID_RESET_OK`.
Those short diagnostics may coalesce in the one-entry async message slot; the
later `d` event is the stronger end-to-end result. The corresponding rule
applies to `b` and `c`: removal plus a later marker proves that the old USB
epoch retired and a new one became usable.

A complete hardware run on 2026-07-24 reached `f10` with no `f12`, allocation
failure, reset failure, wait timeout, or teardown diagnostic. The deliberately
requested `HID_RX_STALL`, `HID_CLEAR_HALT_FAIL`,
`HID_ENUM_CONFIG_TOO_LARGE`, and `HID_ENUM_CONFIG_INVALID` diagnostics were
followed by the expected recovery markers.

The run kept `oom=0`; its minimum-ever free heap was 13,072 bytes. Repeated
ordinary attached states returned to 46,976 bytes free, and the removal
snapshot repeatedly returned to 60,832 bytes free with a 55,176-byte largest
block. That snapshot is taken before KeyD releases the removed client's queue
and wrapper. The 4096-byte report-descriptor case temporarily reduced the
attached-state heap to 42,944 bytes, then the next ordinary profile returned
to 46,976 bytes, so no cumulative heap loss was observed. The final `f10`
identity intentionally remains attached.

`WARN: HID_IGNORED` for an unsupported auxiliary Magic Trackpad interface and
`WARN: EVDEV_BATCH_CAP` did not prevent the mode-SET/reconnect result. The
latter remains the reason this pass does not certify maximum-contact
multitouch batches.

### Superseded unverified dirty checkpoint

The following host artifact was built but never received a recorded hardware
verdict:

- path: `./build/ErgoType.uf2`
- SHA-256:
  `0a7ff16341260c7d78300989142b30fecb30b8314c0b6316f2a7a353a3586ba7`
- linked ELF: `text=519612`, `data=788`, `bss=245320`; 504 B of main-bank
  link headroom and 1,260 B between scratch X and the core-1 stack
- hardware verdict: never recorded; this SHA is historical and is not the
  current build

The planned validation for that exact image required the following two separate
emulator passes. They were not recorded as completed for this SHA; reuse the
matrix only after recording the new host artifact first:

1. `device/work-input-drivers`, using emulator SHA-256
   `58d309156aa7fb4a8152f6623ff2a4a4e1e2c7c0fcebe44f90afcdc307eef496`:
   cold boot, board reset, then at least three complete five-identity cycles.
   Require all ELECOM/Kensington/Topre/EVision signals and working `cafe:1005`
   pointer/wheel input from configuration offset 575 after its injected first
   full-configuration GET failure. Record free heap, minimum heap, largest
   block, block count, OOM count, and all host-task stack watermarks before the
   first attach, at the peak, and after each complete removal.
2. Rebuild `device/haptic-touchpad` and record its exact commit and emulator
   UF2 SHA before flashing it. Require multitouch/pointer input, feature
   GET/SET probe, haptic output feedback, cold boot, and repeated unplug/replug
   without a falling post-removal heap plateau. This is the basic haptic
   transport/driver regression only. The emulator's separate boot-mouse
   interface can move the cursor even when the downstream Linux-to-KeyD
   `ABS_MT_*` consumer is wrong, so this pass does not certify an MT-only
   touchpad's pointer semantics. It also does not prove in-place effect replacement,
   PLAY-to-erase/reuse, queued-PLAY unplug, all five slots, or final mode
   restoration unless the fixture explicitly drives those sequences.

For both passes, `WARN: HID_IGNORED` can be expected for an intentionally
unsupported interface. A single `ERR: HID_RX_XFER_FAIL` immediately adjacent
to physical removal can be the endpoint abort winning before REMOVE is
processed; repetition while the device remains attached, missing resumed
input, or a falling removal plateau is a failure. Normal traffic must not
produce `ERR: HID_WAIT_TIMEOUT`, `ERR: HID_WAIT_UNLINK`,
`ERR: HID_TEARDOWN_WAIT`,
`ERR: HID_ASYNC_XFER_TUPLE`, `ERR: HID_ASYNC_HUB_PIN`, probe/allocation errors,
or a nonzero OOM counter. The old aggregate name
`ERR: HID_ASYNC_INVARIANT` is not a current diagnostic; use the two specific
`HID_ASYNC_*` messages above.

Passing these two smoke regressions does not claim the still-separate fault
matrix: REMOVE or a foreign ATTACH during the armed 100-ms retry, hub-child
reset, fast address reuse, host-event queue saturation, terminal clear-halt/reset,
exact configuration lengths 512/513/4096 and malformed/oversize descriptors,
the haptic replacement races listed above, or the downstream KeyD Type-B
multitouch conversion. Record each as unverified unless its targeted fixture
was actually run.

The combined host also links the upstream USB-only Magic Mouse 2 / Trackpad 2
driver. The 2026-07-23 combined fixture exposed all four Trackpad 2 HID
interfaces (one mouse and three non-mouse), required the `{ 0x02, 0x01 }`
feature SET, delivered native report ID `0x02`, exercised one/two contacts plus
release, and repeated the full handshake after reconnect. `CFG_TUH_HID=4` fits
that standalone device exactly. Accepted `-EIO` and unplug while the mode SET
is pending remain separate fault-injection checks. Upstream returns early for
USB Magic Mouse 2, so a synthetic `0x12` stream is not evidence of a wired
mode-switch path.

### Historical interrupt-IN STALL fixture

The previously tested dirty `device/haptic-touchpad` fixture stalled
marker-keyboard endpoint
`0x85` exactly once after marker `2`. TinyUSB device core does not make that
endpoint ready again until the host sends standard endpoint
`CLEAR_FEATURE(HALT)`. The emulator then sends marker key `s`, which is the
authoritative end-to-end result:

```text
key 2 -> interrupt-IN STALL -> host clear-halt/DATA0/rearm -> key s
```

The expected host diagnostics are one `ERR: HID_RX_STALL` and normally
`DBG: HID_CLEAR_HALT_OK`. Those task-side messages share a one-entry async
diagnostic slot and may coalesce, so the later `s` input is stronger evidence:
it proves remote clear-halt, local host DATA0 reset, and interrupt-IN rearm.
`HID_CLEAR_HALT_OK` by itself proves only the remote EP0 request.

This STALL ran once per emulator task start, not again after its programmed
soft reconnect. Reset or reflash that fixture to repeat it. It waited for
clear-halt before continuing, so it did not test unplug
during active recovery or the terminal clear-halt-failure reset path.

The first 2026-07-21 hardware run, before the DATA0 helper moved behind
TinyUSB's HCD API, reached the complete sequence
`2 -> HID_RX_STALL -> HID_CLEAR_HALT_OK -> s` and continued through the
programmed disconnect, three interface removals, re-enumeration, and the final
markers. The second enumeration returned to the same active heap plateau
(`free=10248`, `largest=8784`, `blocks=3`, `oom=0`), so that run found no
per-replug transport allocation leak.

The generated PIO-HCD adapter was subsequently verified with host UF2 SHA256
`7f1d77e9f2d589db50a1e1a863fe12a0bb4126456ab9707200bd51eab25647ef`
and emulator UF2 SHA256
`370afde1483ca15c346c3ec25726f5b48b2d2ad3db245d567d8dcf476f42b0b5`.
It repeated the complete STALL sequence, continued input, removed all three
interfaces, and re-enumerated. Its active heap returned exactly to
`free=10264`, `largest=8800`, `blocks=3`, with `oom=0`.

Both runs exposed a separate test-fixture
problem: submitting all 33 contacts as one logical input frame overflows the
62-record normal evdev allowance. That is downstream of the USB transport and
must not be presented as a STALL-recovery failure.

### Isolated interrupt-IN STALL fixture

For a smaller recovery-only checkpoint, a separate worktree exists at
`$HOME/tmp/ErgoType-rx-stall` on pending branch
`device/rx-stall`, based on the clean generic fixture. Its built UF2 is
`$HOME/tmp/ErgoType-rx-stall/build/ErgoType.uf2`, SHA-256
`9b0c256f7d373cf30d21670a31556c0365bb3b39746b56caabc7ee5f529650a6`.

The first boot-mouse report completes normally. Its TinyUSB device completion
then stalls interrupt-IN endpoint `0x84` exactly once per mount. The device
core owns standard `CLEAR_FEATURE(ENDPOINT_HALT)` and DATA0 state; the fixture
does not clear the endpoint itself. Expected host behavior is one
`HID_RX_STALL`, normally `HID_CLEAR_HALT_OK`, and continued left/right mouse
plus wheel cycles. Continued input is the end-to-end assertion if the two
one-slot async diagnostics coalesce. Unplug/replug rearms the one-shot STALL.

## Hardware Verified

These entries were checked with the external emulator board connected to the
ErgoType host board. They prove the named path only; they are not blanket
claims for unrelated drivers.

| Date | Branch | Verified signal |
| --- | --- | --- |
| 2026-07-10 | `device/razer-blackwidow` | host sends Razer raw SET_REPORT, emulator then emits macro usage, and Pico host sees unsupported KeyD code `0x290` events |
| 2026-07-14 | `device/google-stadiaff` (historical host `hid: stabilize stadia ff teardown`) | layout-change FF trigger sends Stadia rumble start and ff-memless timer stop; emulator marker moves pointer up on start and down on stop; this does not verify the current mutex conversion |
| 2026-07-22 | `device/google-stadiaff` (`f8f9a38`) with temporary linked/instrumented host trigger | retained mutex conversion passes start, 300-ms timer stop, same-ID replay, unplug during the exact running Stadia work, and clean remove in two complete cycles separated by reconnect; both cycles return to the same 60,816-byte free-heap plateau with `oom=0` |
| 2026-07-14 | `device/apple-ir` | AppleIR two-packet middle command reaches `hid-appleir.c`; host emits `enter down`, and timer release emits `enter up` |
| 2026-07-14 | `device/hires-wheel` | resolution-multiplier SET_REPORT is queued/completed; keyboard, pointer, scroll, and hi-res wheel/hwheel events reach the Pico host input boundary |
| 2026-07-14 | `device/quirks-atmel-ma901` | after fixing upstream-style `hid->name` construction, the fixture matches laptop Linux behavior and generic pointer/scroll input is not falsely ignored |
| 2026-07-14 | `device/razer-blackwidow` | rechecked after upstream-style `hid->name` construction fix; Razer macro-enable SET_REPORT and macro input path still work |
| 2026-07-14 | `device/quirks-jabra-version` | old `bcdDevice` reaches `hid_lookup_quirk()` before probe; host ignores both HID interfaces and no KeyD input events appear |
| 2026-07-19 | `device/haptic-touchpad` | active multitouch/haptic build enumerates, pointer events move the cursor, and haptic output produces the emulator cursor-feedback signal |
| 2026-07-21 | dirty `device/haptic-touchpad` STALL fixture | one interrupt-IN STALL is cleared remotely and reset to DATA0 locally; marker `s` proves rearm and resumed input, and the programmed reconnect repeats the same active heap plateau |
| 2026-07-22 | `device/work-input-drivers` (`67c1aea`) | ELECOM, Kensington, Topre, and EVision driver signals pass; three `cafe:1005` cycles recover the injected full-configuration GET failure and reach the HID interface at byte 575 of a 600-byte configuration; removal returns to stable `free=60936/60944` plateaus with `oom=0` |
| 2026-07-23 | combined haptic lifecycle / Magic Trackpad 2 fixture | all three numbered/ID0 haptic transports, five-slot replacement/erase/replay/unplug/reconnect, and the four-interface Magic Trackpad 2 mode/native/reconnect path pass in repeated hot- and cold-start runs with `oom=0` |
| 2026-07-26 | `device/logitech-hidpp-dj-waitqueue` | the combined direct HID++/battery/DJ sequence completes at retained `64/675`; simultaneous M705 + ordinary keyboard works with `free=5160`, `min=4008`, and no new OOM, while the complete HID++ eQuad keyboard profile reaches the measured RP2040 memory boundary and adds the run's only OOM count; later device phases complete and teardown recovers without cumulative heap loss |
| 2026-07-29 | `device/wacom-wired-matrix`, exact no-PIO host `bdf6ab6c…` and 32-reconnect emulator `61440170…` | CTL-472 completes `f1, f2, f3, f4, f10` with no `f12`; exact mode SET/GET, Pen/eraser input, pre-deadline and held-callback disconnect, recovery, 36 Pen add/removes, 36 expected ghost-interface warnings, stable removal plateaus, nonzero task watermarks, and `oom=0` |
| 2026-07-30 | expanded `device/wacom-wired-matrix`, host `4efcd108…`, emulator `037c8fe0…` | CTL-472, CTL-672, PTK-450, CTH-470, and PTH-650 complete `f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10`; all 23 physical attachments publish and remove the expected 46 Wacom input nodes, all 115 heap snapshots have `oom=0`, and the one 256-byte immediate PTH teardown difference is reclaimed; production logging does not expose exact power-snapshot values or ordering |
| 2026-07-30 | AES/receiver `device/wacom-wired-matrix`, host `8e07cbab…`, emulator `8dd6dd64…` | Yoga 260 AES and USB receiver phases complete without a failure marker or host `ERR`; four AES and four receiver attachments produce 20 balanced input lifetimes across control/input/battery, pair/unpair/re-pair, sibling-init cancellation, held rebind/teardown controls, physical disconnect, and recovery; all 47 heap snapshots have `oom=0` and nonzero task watermarks |
| 2026-07-31 | external wired Intuos `device/wacom-wired-matrix`, host `1bd3124a…`, emulator `93f034b6…` | eleven selected `INTUOSHT`, `INTUOSPS/PM/PL`, and `INTUOSHT2` PID profiles publish and remove 29 expected Pen/Pad/Finger nodes, then complete `f15, f10` without `f12` or host `ERR`; exact-ID Pro feature-usage compaction leaves enough heap for Finger, all 63 snapshots have `oom=0`, and family captures do not imply byte-exact retail descriptors for every PID |
| 2026-07-31 | Deco/Parblo `device/uclogic-deco-parblo` (`c843295`), host `a9ba49cf…`, emulator `373df86d…` | Deco 01 original and Parblo A610 Pro each complete an initial and same-PID reconnect generation; ten target input lifetimes balance, all 22 snapshots have `oom=0`, and terminal `f15, f10` arrives without `f12`, `HID_REPORT_SKIP`, or host `ERR` |
| 2026-08-01 | focused UC-Logic failed-probe fixture | three reached `hid_hw_start()` failures after combined-descriptor generation repeat the same cleanup plateau, then one normal Parblo generation publishes and removes Mouse/Pen/Pad; all 25 snapshots have `oom=0`, with no host `ERR`; `hid_parse()` remains source-audited only |
| 2026-08-01 | Artist `device/uclogic-artist` (`287acbf`), host `34aeccba…`, emulator `7173d5f1…` | Artist 22R full input plus reconnect smoke and Artist 24 input complete six balanced Pen/Pad lifetimes, six expected `HID_IGNORED`, 21 `oom=0` snapshots, and terminal `f15, f10` without `f12`, `HID_REPORT_SKIP`, or host `ERR`; exact Artist 24 reconstructed ABS_X is not visible in production logging |
| 2026-08-01 | Cintiq 13HD `device/wacom-cintiq-13hd` (`160a0ac`), host `ec47bd67…`, emulator `1f7bb41c…` | three `056a:0304` generations complete pre-deadline cancellation, two exact Feature report 2 mode exchanges, Pen input, all nine Pad buttons, same-PID reconnect, six balanced Pen/Pad lifetimes, 21 `oom=0` snapshots, and terminal `f1, f2, f3, f10` without `f12`, `HID_REPORT_SKIP`, or host `ERR`; the descriptor is protocol-equivalent and does not prove Touch Ring or `ABS_WHEEL` |

## Recorded Emulator Branches

Recorded emulator branches:

| Branch | Commit |
| --- | --- |
| `device/a4tech-x5-005d` | `d11b859` |
| `device/apple-ir` | `547d781` |
| `device/chicony-wireless-radio` | `c6d2769` |
| `device/creative-sb0540` | `9a58ea2` |
| `device/cypress-mouse` | `a40ce2f` |
| `device/google-stadiaff` | `f8f9a38` |
| `device/hires-wheel` | `1e3114e` |
| `device/holtek-kbd-a055` | `bd6753f` |
| `device/ite8595-rfkill` | `4686eea` |
| `device/kye-easypen-m406` | `cd1f262` |
| `device/microsoft-usb` | `c4e5361` plus the tested activation-delay working-tree change |
| `device/primax-keyboard` | `a187788` |
| `device/pxrc-phoenixrc` | `8072356` |
| `device/quirks-atmel-ma901` | `f35de09` |
| `device/quirks-jabra-version` | `2cfc0b9` |
| `device/rapoo-2_4g-receiver` | `14ffb87` |
| `device/razer-blackwidow` | `127a06f` |
| `device/saitek-rat7` | `1d3d945` |
| `device/zydacron-remote` | `40ee6a8` |
| `device/work-input-drivers` | `67c1aea` |
| `device/haptic-lifecycle` | `05607cc` |
| `device/wacom-wired-matrix` | `66c5dde` |
| `device/uclogic-deco-parblo` | `c843295` |
| `device/uclogic-artist` | `287acbf` |
| `device/wacom-cintiq-13hd` | `160a0ac` |

- `device/a4tech-x5-005d`: A4Tech mapping/mapped/event/probe path; wheel
  orientation and hi-res wheel behavior.
- `device/apple-ir`: `raw_event`, `input_configured`, timer key release, and
  `HID_CONNECT_HIDDEV_FORCE` coverage.
- `device/chicony-wireless-radio`: `HID_QUIRK_INPUT_PER_APP`, wireless-radio
  application, raw RFKILL event path.
- `device/creative-sb0540`: forced custom input device, `input_configured`,
  suppressed normal mapping, raw media-key events.
- `device/cypress-mouse`: Cypress mapped/event path; Button 5 + wheel rewrite
  coverage.
- `device/microsoft-usb`: six representative identities cover every active
  Microsoft quirk branch, one same-PID reconnect, exact NOGET control
  behavior, and per-device `last_key` ownership; the exact fixed-host pair
  passed on hardware on 2026-07-31.
- `device/google-stadiaff`: Google Stadia VID/PID and a report descriptor with
  gamepad input report ID 1 plus rumble output report ID 5. This branch
  exercised `hid-google-stadiaff.c` plus `ff-memless.c` with the short
  layout-rumble trigger at historical host checkpoint `hid: stabilize stadia ff teardown`, then verified the
  retained mutex conversion with the temporary linked/instrumented trigger
  recorded below.
- `device/hires-wheel`: generic HID resolution multiplier path:
  async `GET_REPORT` followed by async `SET_REPORT`, plus hi-res wheel and AC
  Pan input.
- `device/holtek-kbd-a055`: Holtek keyboard report fixup and boot-keyboard LED
  output redirect.
- `device/ite8595-rfkill`: ITE RFKILL mapping/event path, including synthetic
  press/release from a zero-valued report.
- `device/kye-easypen-m406`: KYE report fixup and probe-time SET_REPORT tablet
  enable path.
- `device/uclogic-tablet-matrix`: generic pen regression, Huion H640P and
  Kamvas 13 dynamic string-parameter paths, and the three-interface XP-Pen
  Deco 01 V2 interrupt-OUT/string path.
- `device/uclogic-deco-parblo`: focused Deco 01 original v1 and Parblo A610
  Pro UGEE-v2 string/request/input paths, including same-PID reconnect.
- `device/uclogic-artist`: focused Artist 22R/24 Pro OUT-before-string
  initialization, Pen/Pad/Dial layouts, and same-PID 22R reconnect.
- `device/wacom-cintiq-13hd`: focused `056a:0304` delayed mode exchange,
  pre-deadline cancellation, Pen/nine-button Pad input, and same-PID reconnect.
- `device/primax-keyboard`: raw event rewrite and re-entry into the HID parser.
- `device/pxrc-phoenixrc`: report fixup plus stateful raw axis shuffle.
- `device/quirks-atmel-ma901`: name-based `hid_ignore()` quirk requiring the
  product string before probe.
- `device/quirks-jabra-version`: version-based Jabra ignore quirk requiring
  `bcdDevice` before probe.
- `device/rapoo-2_4g-receiver`: USB interface-number gate, extra driver-owned
  input device, raw event path.
- `device/razer-blackwidow`: Razer raw SET_REPORT macro-enable path, then macro
  key events.
- `device/saitek-rat7`: report fixup, raw mode bits, and event-generated
  synthetic press/release.
- `device/zydacron-remote`: report fixup, input mapping, raw event injection,
  and generic-path suppression.

## What This Covers Well

- ordinary Linux HID parser/input flow
- `report_fixup`
- `input_mapping`
- `input_mapped`
- `input_configured`
- simple `probe`
- `raw_event`
- driver `.event`
- `timer_list`
- firmware workqueue path
- Wacom delayed-work deadline, pre-deadline cancel, held-callback disconnect,
  PTK LED work, CTH/PTH Pen/Touch/Pad arbitration, ordinary/AES battery-report
  transfer, AES timer cancellation, receiver pair/unpair/re-pair and sibling
  rebind, physical disconnect, work-disconnect recovery, and teardown paths
- async raw SET_REPORT
- async regular SET_REPORT
- async GET_REPORT to SET_REPORT continuation
- extra driver-owned `input_dev`
- USB interface protocol/interface-number metadata before probe
- product-string quirks before probe
- `bcdDevice` version quirks before probe
- normal keyboard LED output report path through HID SET_REPORT

## Active Driver Hook Audit

This audit is against the current active CMake allowlist only; commented-out
drivers are not counted here.

| Hook / behavior | Active examples | Emulator coverage |
| --- | --- | --- |
| plain generic HID parser/input path | `hid-generic`, `hid-core`, `hid-input` | every emulator branch |
| `report_fixup` | `hid-apple`, `hid-elecom`, `hid-evision`, `hid-microsoft`, `hid-topre`, `hid-holtek-kbd`, `hid-holtek-mouse`, `hid-kye`, `hid-pxrc`, `hid-zydacron`, and other active lightweight fixups | `work-input-drivers`, `microsoft-usb`, `apple-external-usb`, `holtek-kbd-a055`, `kye-easypen-m406`, `pxrc-phoenixrc`, and `zydacron-remote` verified; the `holtek-mouse` hardware pass remains pending |
| `input_mapping` / `input_mapped` | `hid-a4tech`, `hid-apple`, `hid-cypress`, `hid-evision`, `hid-ite`, `hid-kensington`, `hid-microsoft`, `hid-zydacron` | `work-input-drivers`, `microsoft-usb`, `apple-external-usb`, `a4tech-x5-005d`, `cypress-mouse`, `ite8595-rfkill`, and `zydacron-remote` verified |
| driver `.event` hooks | `hid-a4tech`, `hid-apple`, `hid-cypress`, `hid-ite`, `hid-microsoft`, `hid-saitek` | `a4tech-x5-005d`, `apple-external-usb`, `cypress-mouse`, `ite8595-rfkill`, `microsoft-usb`, and `saitek-rat7` verified |
| `raw_event` hooks | `hid-chicony`, `hid-creative-sb0540`, `hid-primax`, `hid-pxrc`, `hid-rapoo`, `hid-saitek`, `hid-zydacron` | `chicony-wireless-radio`, `creative-sb0540`, `primax-keyboard`, `pxrc-phoenixrc`, `rapoo-2_4g-receiver`, `saitek-rat7`, `zydacron-remote` |
| `input_configured` / extra input device naming | `hid-creative-sb0540` | `creative-sb0540` |
| `HID_QUIRK_MULTI_INPUT` / `HID_QUIRK_INPUT_PER_APP` | KYE entries from `hid-quirks.c`, `hid-chicony` | `kye-easypen-m406`, `chicony-wireless-radio` |
| workqueue callback | `hid-input` LED work, active `hid-haptic` effect/stop work, and Wacom initialization, LED, ordinary/AES battery, and receiver work | LED path via `holtek-kbd-a055`; `haptic-lifecycle` verifies ordinary work; the exact Wacom artifacts cover pre-deadline and held-callback removal, PTK/PTH work disconnect, AES pending work, and receiver sibling-init/rebind/teardown lifetime |
| async raw SET_REPORT | `hid-razer` | `razer-blackwidow` |
| async regular SET_REPORT | `hid-kye`, `hid-input` LED work | `kye-easypen-m406`, `holtek-kbd-a055` |
| async GET_REPORT to SET_REPORT continuation | `hid-input` resolution multiplier path | `hires-wheel` |
| periodic regular GET_REPORT and timer teardown | `hid-apple` Magic Keyboard battery path | `apple-external-usb` verified immediate GET, one real 60-second deadline, queued-request disconnect, timer teardown, and reconnect |
| USB interface metadata before probe | `hid-rapoo`, Razer mouse/keyboard protocol split | `rapoo-2_4g-receiver`, `razer-blackwidow` |
| product-string quirk before probe | name-based ignore entries in `hid-quirks.c` | `quirks-atmel-ma901` |
| `bcdDevice` version quirk before probe | Jabra version ignore entries in `hid-quirks.c` | `quirks-jabra-version` |
| Deferred Stadia `FF_RUMBLE` through memless FF (`ff-core.c` remains active for HID Haptics) | retained `hid-google-stadiaff.c` and `ff-memless.c`; upload/timer/replay/running-work-remove/reconnect path passed before deferral | `google-stadiaff` |
| USB-only Magic Mouse / Trackpad parsing, MT mapping, and mode SET | `hid-magicmouse.c` | normal four-interface Trackpad 2 mode/native/reconnect path verified by the combined 2026-07-23 fixture; fault injection remains |
| Wacom mode SET/GET, record FIFO, Pen/Pad/Touch, LED, ordinary/AES/receiver battery, arbitration, receiver rebind, and ghost-interface rejection | `wacom_sys.c`, `wacom_wac.c`; seven base IDs plus external wired `056a:0302/0303/0304/030e/0314/0315/0317/0323/033b/033c/033d/033e` | historical CTL-472, expanded five-profile wired, separate AES/receiver, focused eleven-ID Intuos, and focused Cintiq 13HD artifacts passed on hardware; exact power values, elapsed AES expiry, receiver child profiles other than `0027`, byte-exact retail descriptors for every selected PID, Cintiq Touch pair `0333/0335`, and Remote remain outside the verdict |
| Historical timer/HIDDEV-force path, inactive | `hid-appleir.c` at `hid: stabilize stadia ff teardown` | `apple-ir` |

## Pending Dedicated Hardware Passes

`hid-holtek-mouse` is active so `CONFIG_HID_HOLTEK` no longer marks six mouse
IDs as special without linking their report fixup. Its unverified
`device/holtek-mouse` fixture preserves the broken `0x7fff` descriptor fields,
checks the repaired `0x2fff` boundary, pointer input, and reconnect. The normal
USB Magic Trackpad 2 route is hardware-verified above; accepted mode-SET
`-EIO`, disconnect during that SET, and queue saturation remain targeted fault
passes rather than normal-driver gaps.
Stadia has a dedicated emulator fixture and a current result for its retained,
unlinked `FF_RUMBLE` implementation. The remaining active vendor allowlist is
A4Tech,
Apple external USB, Chicony, Creative SB0540, Cypress, ELECOM, EVision, Holtek keyboard, ITE,
Kensington, KYE, Lenovo `6009/6047`, Microsoft, Primax, PXRC, Rapoo, Razer,
Saitek, Topre, Wacom,
and Zydacron. Apple and Microsoft passed their focused exact-artifact hardware
runs on 2026-07-31. Wacom's historical exact
32-reconnect CTL-472 artifact, expanded five-profile wired artifact, and
separate AES/receiver artifact are hardware-verified; its focused external
wired Intuos artifact passed all eleven selected IDs using the documented
family captures, and the focused Cintiq 13HD artifact passed exact `0304`.
Exact power-snapshot
values, elapsed AES expiry, receiver children outside selected profile
`056a:0027`, and Remote coverage remain separate.
Core/common glue (`hid-core`,
`hid-input`, `hid-generic`, `hid-drivers`, and `hid-quirks`) is exercised by all
fixtures.

Files present under `usb_host/linux/drivers/hid` but commented out in CMake are
not active drivers and must not be listed here as covered merely because they
reuse an already tested hook shape.

## Coverage Decision

The existing emulator set plus the hardware-verified work-input, combined
haptic/Trackpad, Microsoft, Apple, exact Wacom, and temporary-capacity
Logitech/Lenovo fixtures covers the long-enumeration success path, normal USB
Magic Trackpad 2 path, selected Wacom CTL-472/CTL-672/PTK-450/CTH-470/PTH-650/
Yoga 260 AES/USB receiver plus external wired Intuos and Cintiq 13HD
`056a:0304` flows, active Logitech `c532`, and Lenovo
`6009/6047`. The active allowlist is covered except for the Holtek mouse
driver-specific hardware result. Memory-gated `c52f/c534/60ee` have logic
coverage at temporary `64/256`, not RP2040 support at retained `64/675`.
Stadia's existing fixture covers its deferred mutex-conversion path if it is
relinked later. The complete Wacom fixture also passed the working input/devres
and evdev identity correction. The later fixture certifies the selected
receiver rebind and AES control/input paths, but not exact detached
power-snapshot values or ordering, elapsed AES expiry, the generic delayed-work
branches listed below, or the separate Rapoo managed extra-input path.

Reasoning:

- the host CMake allowlist links only the active HID `.c` files selected in
  `CMakeLists.txt`
- the recorded behavior-specific emulator branches cover the nontrivial hook
  classes in the earlier set, and the work-input fixture directly checks the
  four newly enabled work-input drivers
- the known metadata-sensitive quirks are covered by dedicated negative tests:
  product string and `bcdDevice`
- output SET_REPORT paths are represented by keyboard LED/Holtek and Razer/KYE
  style request paths

Do not add more emulator branches unless a specific active driver fails or a
specific hook class looks suspicious in hardware. One reasonable optional
target is another
`HID_QUIRK_MULTI_INPUT` device if KYE/Chicony coverage turns out too narrow.

The Stadia fixture covers the retained, currently unlinked mutex conversion:
simple memless rumble, automatic stop, same-ID replay, unplug during a running
driver work, remove, and reconnect. The active standard HID Haptics Page path
has separate coverage for in-place replacement, explicit erase/reuse, all five
slots, queued-work unplug, remove, and reconnect. Stadia-specific stress beyond
its deterministic teardown window remains relevant only if that deferred
driver gains a product client.

## Hardware Test Matrix

A future broad regression pass need not exhaust all emulator branches. Use
these as targeted gates when the allowlist or their shared hook paths change:

For each emulator branch:

```sh
cd ../ErgoType-hid-devices
git checkout device/<branch>
cmake --build build -j4
```

Then flash `../ErgoType-hid-devices/build/ErgoType.uf2` to
the emulator board. The host board should run the current ErgoType host build.

Recommended smoke order:

1. `device/work-input-drivers`: proves ELECOM, Topre, and EVision descriptor
   fixups plus Kensington vendor-usage mapping; use its separate artifact
   documented above.
2. `device/razer-blackwidow`: proves raw async SET_REPORT and macro event path.
3. `device/hires-wheel`: proves resolution multiplier GET_REPORT to SET_REPORT
   continuation and hi-res wheel input.
4. `device/quirks-atmel-ma901`: proves product-string metadata reaches
   `hid_ignore()` before probe.
5. `device/quirks-jabra-version`: proves `bcdDevice` metadata reaches
   `hid_lookup_quirk()` before probe.
6. `device/holtek-kbd-a055`: proves ordinary LED output SET_REPORT does not
   block/assert.
7. `device/wacom-wired-matrix`: use the separately recorded exact artifacts.
   The expanded wired artifact proves mode, input, LED/work, composite
   teardown, and reconnect; the final AES/receiver artifact proves the added
   timer, battery-work, pair/unpair/re-pair, sibling-rebind, and recovery paths.

These gates cover the current host architecture. Run the useful second-pass
branches only if one fails or a specific driver family needs confirmation.

### Must Pass

- `device/work-input-drivers`
  - flash the emulator UF2 documented above
  - expected ELECOM signal: `Fixing up Elecom mouse button count`, pointer and
    wheel movement, and buttons 4..6 down/up
  - expected Kensington signal: middle and side button events from vendor
    usages 1 and 2
  - expected Topre signal: `fixing up Topre REALFORCE keyboard report
    descriptor`, followed by C and Left Shift press/release
  - expected EVision signal: `fixing EVision:TeLink Receiver report
    descriptor`, followed by pointer/wheel and Forward/Back events
  - expected long-enumeration signal: `cafe:1005` mounts after its injected
    full-GET failure and produces pointer/wheel input from the HID interface at
    configuration offset 575
  - failure signal: a missing identity-specific event, HID probe/allocation
    errors, or input stopping/leaking across repeated re-enumeration cycles
- `device/razer-blackwidow`
  - build command: `git checkout device/razer-blackwidow && cmake --build build -j4`
  - expected host signal: Razer SET_REPORT is queued and completes, then macro
    usages reach KeyD as unsupported code `0x290` until KeyD learns those keys.
  - expected emulator signal: CDC may print `HID_SET_REPORT` after the host
    sends the Razer macro-enable report.
  - failure signal: no macro events after mount, or SET_REPORT submit/complete
    errors.
- `device/hires-wheel`
  - build command: `git checkout device/hires-wheel && cmake --build build -j4`
  - expected host signal: resolution multiplier async GET_REPORT/SET_REPORT
    path completes and hi-res wheel reports reach the input boundary.
  - failure signal: no wheel reports, repeated async request errors, or device
    never reaches KeyD.
- `device/quirks-atmel-ma901`
  - build command: `git checkout device/quirks-atmel-ma901 && cmake --build build -j4`
  - expected host signal: no KeyD events from this emulator. This is a negative
    test: the product string should make `hid_ignore()` ignore the HID device.
  - failure signal: generic keyboard/mouse events appear.
- `device/quirks-jabra-version`
  - build command: `git checkout device/quirks-jabra-version && cmake --build build -j4`
  - expected host signal: no KeyD events from this emulator. This is a negative
    test: low `bcdDevice` should make the Jabra version quirk ignore it.
  - failure signal: generic events appear, which means the version metadata did
    not reach `hid_lookup_quirk()`.
### Interpreting Negative Tests

The quirk tests are successful when the emulator is visible on USB but does not
produce ordinary KeyD input events. That is intentional:

- `quirks-atmel-ma901`: ignored because product string matches MA901.
- `quirks-jabra-version`: ignored because `bcdDevice` is below the Jabra
  threshold.

If any of these produces normal keyboard/mouse events, the metadata/probe
boundary is wrong even though the board appears to be "working".

### Useful Second Pass

- `device/a4tech-x5-005d`: verifies mapped/event wheel rewrite behavior.
- `device/cypress-mouse`: verifies Cypress button/wheel event rewrite behavior.
- `device/apple-ir`: verifies timer-based delayed key release and hiddev-force
  does not break normal input.
- `device/creative-sb0540`: verifies driver-owned input device creation and raw
  media-key injection.
- `device/kye-easypen-m406`: verifies probe-time SET_REPORT tablet enable.
- `device/primax-keyboard`: verifies raw-event rewrite plus re-entry into the
  HID parser.
- `device/rapoo-2_4g-receiver`: verifies interface-number matching and extra
  input device ownership.
- `device/saitek-rat7`: verifies raw mode bits and `.event` synthetic
  press/release.
- `device/zydacron-remote`: verifies raw-event injection and generic-path
  suppression.

### Output Paths

The following route-specific checks are pending and must not be copied into
Hardware Verified until their signals are observed.

- `device/keyboard-led` (`c09244d`, pending)
  - shape: base-HID boot-keyboard interface with no interrupt OUT endpoint;
    the NKRO profile has protocol NONE and does not exercise boot startup.
  - expected enumeration-time CDC signal:
    `HID_SET_REPORT inst=0 id=0 type=2 len=1 ...` before periodic CapsLock
    traffic.
  - proves the startup NumLock reset and EP0 fallback together.
- `device/haptic-touchpad` (`2a4f966`, historical pending-route base; rebuild
  and record the exact current commit/UF2 before a new result)
  - exposes interrupt OUT, but the existing layout-haptic path calls
    `.output_report()` directly.
  - regresses interrupt OUT transport but does not prove that
    `.request(HID_REQ_SET_REPORT)` selected interrupt OUT; that requires a
    request-path trigger or `HID_REPORT_OUT_Q` / `HID_REPORT_OUT_OK` trace.

- `device/holtek-kbd-a055`
  - expected device-side signal: if the host sends keyboard LEDs, the emulator
    CDC log reports `HID_SET_REPORT`.
  - expected host-side signal: no assert/block in the LED output path.
  - an enumeration-time `first=0x00` report proves only boot reset/EP0
    fallback; the later `first=0x02` report is still required to prove the
    Holtek LED redirect.
- `device/google-stadiaff`
  - historical host `hid: stabilize stadia ff teardown` used a layout-change `FF_RUMBLE` trigger and
    observed SET_REPORT ID 5 plus the ff-memless timer stop.
  - a temporary linked/instrumented host image was tested with a targeted
    `FF_RUMBLE` trigger before the pair returned to the deferred CMake set; its
    exact SHA256 is recorded in Hardware Test Identity below. The ordinary
    layout-change hook still emits the unrelated `FF_HAPTIC` type.
  - expected device-side signal: emulator CDC log reports SET_REPORT for
    report ID 5, with its pointer marker moving on rumble start and stop.

### Future FF client boundary

The concise client recipe and lifetime rules are in the
`Future firmware FF client hook` section of `hid-host-bringup.md`. The
historical temporary Stadia client exercised this conceptual sequence:

```text
upload FF_RUMBLE id=-1  -> assigned effect ID
write EV_FF(id, 1)      -> play
ff-memless timer        -> automatic stop at replay.length
write EV_FF(id, 1)      -> replay the same uploaded slot
write EV_FF(id, 0)      -> optional explicit stop
erase effect ID         -> release the slot
```

The targeted Stadia run exercised upload, play, automatic timer stop, and
same-ID replay. It did not issue the explicit stop or erase calls in this
generic future-client sequence.

All calls belong to a task-owned client. Never make them from a TinyUSB
callback. An effect ID dies with its `struct device`: clear it on
`EV_DEV_REMOVE` and upload a new `id = -1` effect after reconnect. The
temporary raw helpers are not part of the current API; an immediate writer
result was not USB completion. The `EV_FF` echo may reach the current KeyD reader as
`unrecognized evdev event type: 21`; that message is a downstream-consumer gap,
not a failed FF request.

## Remaining Gaps

The active `hid-haptic` driver creates an FF device through `ff-core.c`, and
the indexed KeyD policy exercises the generic evdev/input output plumbing:

- `device_haptic_upload()` reaches `input_ff_upload()`
- `device_haptic_erase()` reaches `input_ff_erase()`
- `device_haptic_play()` sends `EV_FF` through `evdev_write()` /
  `input_inject_event()`

The 2026-07-23 lifecycle pass exercised `hid-haptic.c`,
`hid-multitouch.c`, asynchronous feature GET_REPORT probe, all five waveform
slots, same-ID replay, in-place replacement, erase/re-upload, HOST/DEVICE mode
changes, queued-work unplug, removal, and fresh replay after reconnect. It
covered numbered interrupt OUT (`cafe:1006`), unnumbered report ID 0 interrupt
OUT (`cafe:1007`), and unnumbered report ID 0 EP0 fallback (`cafe:1008`).
The exact diagnostic generator and transport/parser oracles were then removed;
none of their delays, markers, or fixture identities is product behavior.

This standard HID Haptics coverage is independent of the retained, unlinked
`ff-memless`/Stadia path. The ordinary layout-change hook queues a virtual
request for a preloaded `FF_HAPTIC`, whereas Stadia requires `FF_RUMBLE`; the
separate Stadia result above covers that deferred path.

The post-probe activation step also needs a composite regression: all input
devices must receive one devmon ADD with final capabilities and no writer may
be reachable before every matching handle has opened. Input must begin only
after all prepared queues are published; unplug during partial/complete
activation or just before `driver_ready` must yield one matching removal
without a stale writer or post-detach publication.

For fixtures that queue a mode SET during probe (`haptic-touchpad` and the Kye
tablet-mode cases), its completion must precede the first published input. Also
unplug once while that SET is pending; lifecycle must leave through the normal
fenced teardown without `ERR: HID_WAIT_TIMEOUT`, `ERR: HID_WAIT_UNLINK`,
`ERR: HID_TEARDOWN_WAIT`, or a stalled board. The
former `HID_WAIT_BUSY`/`HID_WAIT_OWNER` names belonged to the retired
polling/owner implementation and are not emitted by the current artifact.

The upstream `8813b061` multitouch fix additionally needs a fixture advertising
`ContactCountMaximum >= 33` and reporting contact ID 32. Include a sticky-finger
timeout, then verify release, continued input, unplug/replug, and restored heap;
ordinary low-slot pointer motion does not directly cover the former overflow.
The first 2026-07-21 attempt sent 33 newly active contacts in one logical frame;
Linux correctly emitted hundreds of changes and overflowed the deliberately
bounded evdev-to-KeyD queue. Refine the emulator by staging contacts 0--31 in
separate drained frames, then send the maximum-size frame with only contact 32
changing. That still exercises physical slot 32 and the multi-report frame
without turning this transport test into an artificial queue-overload test.
The refined run must produce neither `EVDEV_INPUT_DROP` nor a QueueSet assert;
unplug after the stream must still deliver every removal.

The older `device/haptic-touchpad` descriptor uses only numbered output report
ID 6. The lifecycle pass hardware-verified the generic unnumbered-report
convention on both interrupt OUT and EP0 fallback: byte zero is transport
padding for report ID 0 and does not replace the first haptic payload byte.

## Direct HID++ Request/Reply Diagnostic Checkpoint

Host candidate
`9577a3e2820e99615b62e6535a1c01fbd403546c3bad233d9834a42f0dc88902`
and emulator branch `device/logitech-hidpp-direct`, UF2
`2a0ff72e47fb4a8bff7e46a2550b69b5f58f0be3bb8ee91eb2e4d6d837a8ca4b`,
have reproducible clean-build and descriptor coverage. The emulator builds as
`text/data/bss = 61804 / 0 / 254960 B`. Two exact-pair hardware runs passed on
2026-07-24.

The automatic fixture exposes the selected upstream direct identity
`046d:c08d` with HID++ reports `0x10` and `0x11`, then advances profiles by its
own D+ disconnect/reconnect sequence:

1. HID++ 1.0: answer the protocol ping with the expected INVALID_SUBID status,
   then answer the read-only `GET_REGISTER(HIDPP_REG_FEATURES)` request. Host
   must emit `HIDPP10_REPLY_OK`, ordinary pointer input, and `f1`.
2. HID++ 2.0: answer the protocol ping, return BUSY once for the ordinary root
   feature query, then return its successful response. Host must emit the
   single-slot-safe combined marker `HIDPP_BUSY_REPLY_OK`, pointer input, and
   `f2`.
3. Protocol status: answer the ping with a non-version protocol status. Host
   must emit `HIDPP_PROTOCOL_STATUS`, pointer input, and `f3`, not either
   reply-success marker.
4. Timeout: accept the request but send no response. The pinned upstream loop
   performs three five-second attempts; host must eventually emit
   `HIDPP_REPLY_TIMEOUT`, pointer input, and `f4`, and must not emit
   `HIDPP_BUSY_REPLY_OK`.
5. Pending-response disconnect: turn D+ off only after receiving the request,
   so the SET_REPORT has completed but its response is pending. Host must emit
   `HIDPP_REPLY_CANCEL` promptly. A fresh generation must then reconnect and
   complete with `HIDPP20_REPLY_OK`, pointer input, and `f5`.
6. Perform an additional HID++ 1.0 hot cycle with `HIDPP10_REPLY_OK`, pointer
   input, and `f6`.
7. Finish attached as HID++ 2.0 with `HIDPP20_REPLY_OK`, pointer input, and
   `f10`. Three `f12` reports instead mean the emulator stopped before the
   terminal profile.

Both runs completed every marker and input step, reached `f10`, and emitted no
`f12`. The first returned to
`free/largest/blocks = 60832/53384/4` after each of its six removals. The
second kept `free=60864` and `blocks=8` after all six removals; its largest
block grew from the first warm-up snapshot to `52960` and then stayed there.
Both reported `oom=0`. Minimum stack watermarks were 265 words for TinyUSB,
191 for HID work, 348 for the timer, 224 for lifecycle, 860 or 871 for report,
and 658 for KeyD. The separate duplicate-GPIO validation was intentional and
is excluded from this USB result.

Run the full sequence from cold boot through its seven automatic hot reconnects.
After each complete detach, `free`, `largest`, and `blocks` must return
to the first detached plateau with no monotonic loss and `oom=0`; record the
minimum-ever heap separately. Every host task watermark must remain nonzero and
stabilize after the first full cycle, with at least 64 words left in
`hid-work`. Ordinary pointer input after each successful protocol profile must
still reach the existing input boundary. Battery, identity publication,
high-resolution wheel, extra buttons, touchpad behavior, HIDRAW, receiver
children, Bluetooth, FF, and delayed initialization are not covered by this
candidate.

### Production-clean request/reply post-test cleanup

The diagnostic-only host markers, BUSY marker state, and unused RAP/FAP probe
are absent from host UF2
`a79e4385cec2987571226273951b492276e0275f495ebdc598bce2d8bba8498b`.
Matching emulator UF2
`31aa4485df67432e0d146a2d8fda3b46e93d2d4b70daab7e42e8016408a40149`
checks the exact requests internally and reports progress through the existing
host key log:

1. `f1`: HID++ 1.0 protocol detection.
2. `f2`: short BUSY response to the protocol ping, a second ping, then HID++
   2.0 success.
3. `f3`: non-version protocol status.
4. `f4`: three unanswered five-second attempts.
5. Automatic D+ removal after the next ping's completed SET_REPORT, followed
   by a fresh-generation HID++ 2.0 success and `f5`.
6. `f6`: an additional HID++ 1.0 hot reconnect.
7. `f10`: the final HID++ 2.0 generation remains attached.

Three `f12` reports remain the distinct incomplete-sequence signal. This exact
cleaned pair has reproducible build/descriptor coverage. The user accepted it
without another hardware run because the cleanup removed temporary markers,
trace state, otherwise unused RAP/FAP traffic, and the matching emulator delay
without changing the host wait/reply/cancel implementation or memory
structures. The hardware verdict remains attached only to the exact diagnostic
pair above; the production-clean SHA pair is recorded as accepted post-test
cleanup, not as a separate hardware run.

## Full DJ/HID++ RP2040 Capacity Checkpoint

The later automatic fixture exercises these DJ graphs:

- a standalone M705 mouse child (`046d:101b`);
- a standalone ordinary DJ keyboard child (`046d:4024`);
- simultaneous M705 mouse and ordinary DJ keyboard children;
- a complete HID++ eQuad keyboard connection child (`046d:4024`) with
  keyboard, Consumer, power, media-center, and HID++ descriptors.

At the retained `HID_MAX_FIELDS=64`, `HID_MAX_USAGES=675` policy, the
standalone M705 and standalone ordinary keyboard both attach and deliver input,
and both children also work simultaneously. The combined graph leaves
`free=5160`, `min=4008`, `largest=4184`, and `oom=0`. The complete eQuad
keyboard child cannot be created when it is the only paired child and adds the
run's only OOM count. The following M705, ordinary keyboard, and direct HID++
phases still work, and receiver removal returns to the established heap
plateau.

The earlier two-OOM capture used the upstream-sized 256-field table: under that
configuration the second simultaneous child and the complete eQuad child each
reached the memory boundary. At `32/675`, `8/675`, and the retained `64/675`,
simultaneous M705 and ordinary keyboard children both fit, but the complete
eQuad keyboard child still does not. The entire automatic sequence passes at
the temporary `8/256` policy. With that profile live, the heap reaches
`free=4480`, `min=2744`, and `largest=2624`, with `oom=0`; child removal
returns to 42,512 B free and full receiver removal returns to 60,400 B.

The eQuad keyboard Consumer descriptor declares usages 1 through 767. Under
the temporary `8/256` policy only selectors 1 through 256 are retained.
Consequently that run verifies child creation, keyboard input, and lifecycle
cleanup, while Consumer selector mappings 257 through 767 remain outside the
temporary checkpoint.

This is a bounded allocation-capacity result rather than cumulative heap loss
or a broken DJ lifecycle. Other devices with similarly wide usage arrays, many
fields, multiple live receiver children, or large composite HID graphs may
reach the same boundary and need individual measurement or a larger-RAM target.
See [`pio-usb-memory.md`](pio-usb-memory.md) for the allocation breakdown.

## Hardware-verified practical HID++/Unifying matrix

The host candidate and emulator branch
`device/logitech-hidpp-unifying-matrix` completed one automatic hardware pass
on 2026-07-26 for four exact pinned-upstream classes without broad Logitech
matching:

- M560 `046d:402d`: two full pair/configure/input/unpair generations,
  including X/Y, left/right, middle/back/forward, vertical high-resolution
  plus compatibility wheel, and horizontal wheel;
- T650 `046d:4101`: two full pair/raw-configure/input/unpair generations,
  including two-contact absolute position/pressure and click release;
- K400 `046d:4024`: HID++ identity/fallback discovery with standard keyboard
  and touchpad input;
- K750 `046d:4002`: solar light-measurement enable, two solar state events,
  and standard keyboard input around those events.

Each large child runs in its own fresh receiver generation. Earlier generic
keyboard and complete-eQuad capacity phases now use unmatched product
`0x4003`, leaving `0x4024` exclusively for the exact K400 stage. The run keeps
all earlier direct HID++, M705, simultaneous-slot, independent-unpair,
receiver-detach, and final direct-regression checks.

The emulator compares every required host request byte-for-byte and exposes
failure only through the alert keyboard: `f12` followed by the failed phase
letter three times. Emulator CDC is not a verdict source. The hardware run
completed both generations of M560 and T650, one K400 and one K750, all
input/removal signals, and terminal direct `f10`, with no `f12`, `ERR`, or
`WARN`. The retained complete-eQuad capacity phase added the run's only OOM.

The K750 path reaches the existing reduced `power_supply` queue, but the UI
consumer intentionally reads and ignores snapshots. Keyboard input between and
after the two solar reports proves continued execution and lifetime cleanup;
it does not independently expose the capacity/status values. No temporary
value-level diagnostic was added.

The tested builds are:

```text
host text/data/bss       536776 / 788 / 245160 B
emulator text/data/bss    73480 / 0 / 254964 B
hardware verdict         passed 2026-07-26
```

Both M560 generations repeated `free=27008` live and `40832` after child
removal. Both T650 generations repeated `25184` and `40832`. K400 used
`16928` live and returned to `40832`. K750 used `18536` live; its immediate
removal snapshot was `40576` while terminal power-queue cleanup remained
asynchronous. The following direct profile returned exactly to the earlier
attached tuple `free=39408`, `largest=39248`, `blocks=5`, so there is no
cumulative retention. Whole-run minimum heap was 3,640 B during the
simultaneous M705/generic-keyboard phase; all stack watermarks stayed nonzero,
with `hid-work=117` words at minimum.

This stage stops at Linux input/evdev. It does not add KeyD high-resolution
wheel policy, absolute-touch policy, UI battery presentation, HIDRAW
subscribers, Bluetooth, Bolt, legacy 27 MHz classes, or force feedback.

## UC-Logic Tablet Matrix

Branch `device/uclogic-tablet-matrix` is the automatic fixture for the first
UC-Logic host stage. It cold-boots as a generic pen, then reconnects
sequentially as Huion H640P `256c:006d`, Huion Kamvas 13
`256c:006e`, and XP-Pen Deco 01 V2 `28bd:0905`. A repeated
`cafe:10ff UC-Logic Test Alert` keyboard makes the result visible solely in
the host log:

```text
f1  generic pen complete
f2  H640P string 201/200 plus Pen/Pad/Touch Strip/Dial input complete
f3  Kamvas 13 string 201/200 plus the same input classes complete
f4  Deco exact interrupt-OUT/string 100 plus Pen/Pad/Mouse input complete
f10 complete sequence
f12 + a/b/c/d repeated three times: corresponding phase did not complete
```

The Huion fixture requires firmware string 201 before the raw 18-byte
parameter string 200. Deco exposes IN endpoints `0x81/0x82/0x83` and OUT
endpoint `0x03`; interface 2 must receive exactly
`02 b0 04 00 00 00 00 00 00 00` before raw string 100 is served. The
emulator then sends representative position, pressure, tilt, frame-button,
touch/dial, and relative-mouse reports using the raw report IDs and byte
offsets transformed by pinned upstream.

Both images build:

```text
host text/data/bss       549776 / 788 / 245208 B
emulator text/data/bss    59720 / 0 / 254432 B
hardware verdict         passed 2026-07-26
```

The hardware run reached `f1`, `f2`, `f3`, `f4`, then `f10`, with no `f12`.
All expected Linux input nodes and events appeared, removal returned to
equivalent heap plateaus, `oom=0`, and the lifecycle watermark remained
nonzero at 92 words. Deco interface 1 is intentionally invalidated by upstream
and produced the one expected `WARN: HID_IGNORED`. This result certifies the
two emulated Huion parameter profiles, not every retail model sharing
`256c:006d/006e`.

### Modern UGEE-v2 expansion coverage

Branch `device/uclogic-ugee-v2-matrix` extends the first-stage fixture with
wired Deco L, its missing-product-string variant, wireless Deco LW, Deco Pro S,
Pro SW, Pro MW, and a focused USB Magic Trackpad 2 sparse-battery regression.
It covers Pen, Pad, Mouse, both dial directions, capacity/charging traffic,
wireless reconnect re-probe, detach with reconnect work queued, the Trackpad
mode SET, and its report-ID-5 battery GET/input path.

Current builds are:

```text
host text/data/bss       551888 / 788 / 245208 B
emulator text/data/bss    61136 /   0 / 254440 B
hardware verdict         passed 2026-07-27
```

The host log reached
`f1, f2, f3, f4, f5, f6, f9, f11, f14, f15, f10` in order, with no `f12` or
host `ERR`. Seven `WARN: HID_IGNORED` entries are the expected invalid
interface 1 of each XP-Pen profile. Two `WARN: EVDEV_BATCH_CAP` entries belong
to the two Trackpad input devices and leave the mode/battery verdict intact,
but maximum-contact multitouch batches remain uncertified.

Repeated alert-attached heap snapshots stayed within `48376..48384` bytes free,
alert removals returned to `60736..60744`, minimum-ever free heap was `39672`,
and `oom=0`. Minimum watermarks remained nonzero: TinyUSB `265`, KeyD `658`,
async `389`, work `246`, timer `304`, lifecycle `86`, and report `862` words.
Wireless markers prove their exact battery transfers and reconnect re-probes;
the noop UI does not independently acknowledge the queued power snapshots.
UI battery presentation and KeyD tablet policy remain outside this fixture.

### Deco 01 original / Parblo A610 Pro coverage

Branch `device/uclogic-deco-parblo` at `c843295` is the focused fixture for the
two later exact-ID table rows. It does not replay the earlier Huion or modern
UGEE-v2 matrices. Its exact artifacts are:

```text
host UF2 SHA-256       a9ba49cf53d0ce0ba44679d85b11bca301809094cb7705c1721f8e93a12cbd71
host text/data/bss     605256 / 788 / 245408 B
emulator UF2 SHA-256   373df86d428d9c528dbf642b9a2931d38b5c0f8e584f218695d7f14392b65eca
emulator text/data/bss 60912 / 0 / 254436 B
hardware verdict       passed 2026-07-31 for the scope below
```

Deco 01 original `28bd:0042` uses the captured three-interface USB/report
topology and exact raw string-100 bytes. The fixture requires no OUT report,
then sends v1 Pen states, every one of the eight Pad keys, and a compact Pen/
last-key smoke after same-PID reconnect. Parblo A610 Pro `28bd:1903` uses a
protocol-equivalent three-interface topology because pinned Linux replaces its
placeholder reports after probing. Its Pen interface must receive exactly one
10-byte endpoint-`0x03` OUT probe before the pinned 12-byte portion of raw
string 100 is requested. The fixture then sends Pen/tilt, every one of the nine
Pad keys, both 10-byte Dial directions, Mouse button/relative input, and a
smaller repeat after same-PID reconnect.

Failure switches to the alert keyboard and emits `f12`, followed by `a` for the
first Deco generation, `b` for Deco reconnect, `c` for the first Parblo
generation, `d` for Parblo reconnect, or `e` for terminal-alert failure. The
passing sequence instead ends with alert `f15, f10`.

The clean run published and removed four Deco and six Parblo target inputs.
All ten lifetimes balanced, six `HID_IGNORED` warnings matched the deliberately
unused interfaces, and there was no `f12`, `HID_REPORT_SKIP`, or host `ERR`.
All 22 heap snapshots reported `oom=0`; minimum-ever free heap was 46,688 B,
and Deco and Parblo removals repeated at 60,752 B and 60,736 B. Minimum task
watermarks were TinyUSB 265, KeyD 658, async 389, work 346, timer 348,
lifecycle 85, and report 854 words. Pad keys reached KeyD. Pen tool/absolute
events and Parblo Dial reached Linux input/evdev but remained visible as the
existing downstream unsupported-event diagnostics; this fixture does not
claim KeyD tablet or Dial policy.

### UC-Logic failed-probe cleanup coverage

A focused fixture on branch `device/uclogic-failed-probe` at `09ab11e`
exercised the production cleanup. The exact hardware-test artifacts were host
UF2 `9ac5393b9ff7073d3507a13cebcd3dd1856f41e9cee1075960099c29a0c19bda`
and emulator UF2
`ab4fc9dbefd2eee3d872a84f05140dfe9b4639b57951d4daba0a936936f17287`.
The host test image completed the normal Parblo
OUT/string exchange and combined-descriptor generation, then forced
`hid_hw_start()` to fail on three separate generations. Each failed generation
published and removed only the independently successful Mouse input and
returned to the same 60,752-byte heap plateau. A final non-failing generation
published and removed Mouse, Pen, and Pad and repeated that terminal plateau.

The run contained six balanced target input lifetimes, seven expected
`HID_IGNORED` warnings, no host `ERR` or `HID_REPORT_SKIP`, and `oom=0` in all
25 heap snapshots. Minimum-ever free heap was 46,664 B. Minimum task
watermarks were TinyUSB 265, KeyD 658, async 389, work 346, timer 348,
lifecycle 105, and report 862 words. The temporary host fault was removed after
the run. The production-clean host builds as `605272/788/245408`, UF2 SHA-256
`e7d2e2c9469efa27ef7101c14cd070c0d75b00e39df0101fc250c3cfdd64f23c`,
but that exact image was not flashed unchanged. A post-generation
`hid_parse()` failure reaches the same cleanup label and free order; it remains
source-audited rather than runtime-tested.

### XP-Pen Artist 22R Pro / Artist 24 Pro coverage

The focused fixture is commit `287acbf` on branch `device/uclogic-artist`. It
uses ordinary TinyUSB descriptors and callbacks and does not replay the earlier
UC-Logic matrices. The exact hardware-test artifacts are:

```text
host UF2 SHA-256       34aeccbabd836ec82cd5d6f627ac03fd0be9b658af56711b18b0c1835161cc73
host text/data/bss     605304 / 788 / 245408 B
emulator UF2 SHA-256   7173d5f1baa3598f405b4eb85456f7efabaf04bfc086ff488e04b416e27159ce
emulator text/data/bss 49780 / 0 / 252360 B
hardware verdict       passed 2026-08-01 for the scope below
```

Each physical profile exposes two deliberately rejected compatibility
interfaces and the interface-2 interrupt IN/OUT path used by pinned UC-Logic.
The fixture's internal oracle accepts exactly one 10-byte Output report before
exactly one raw string-100 request, with the 12-byte Artist 22R or 14-byte
Artist 24 response. It then sends 10-byte 22R or 12-byte 24 Pen/frame reports.
The 22R generation exercises all 20 Pad buttons and both directions of both
Dials, followed by one same-PID Pen/Pad reconnect smoke. The 24 generation
exercises Pen, the first and last Pad buttons, both Dials, and report bytes that
enter the pinned `fragmented_hires2` rewrite. Alert markers are
`f1, f2, f3, f15, f10`; a recorded phase failure is signalled by `f12` plus
its phase key.

The clean run produced three physical Artist generations, six balanced Pen/Pad
input lifetimes, and six expected `HID_IGNORED` warnings. All 21 heap snapshots
reported `oom=0`; Artist Pad removal returned to 60,752 B after every
generation, and the minimum-ever free counter was 47,592 B. Minimum task
watermarks were TinyUSB 265, KeyD 658, async 389, work 346, timer 348,
lifecycle 36, and report 873 words. There was no `f12`, `HID_REPORT_SKIP`, or
host `ERR`.

This final sizing run retained the existing 512-word lifecycle stack. Its
36-word observed margin must be remeasured after any lifecycle call-graph or
build change. Removing the now-unneeded comment-only `main.c` diff changed only
compiled source-line diagnostics; the resulting clean host rebuild has SHA-256
`17ed2e5637f149b7c1909edd1789597977c825529e26e34a5226efac4dd07770` and was
not flashed unchanged.

The first 15 Artist Pad buttons reached the existing KeyD mappings in the full
22R pass; the final five remained the known unsupported-code boundary. Low-
resolution Dial events were consumed while their high-resolution companion
codes remained downstream diagnostics. Pen pressure/tool/tip/tilt diagnostics
are likewise outside current KeyD policy. Production CDC does not print the
numeric ABS_X value, so sending the 24 Pro high-X report establishes transport
and execution of the size-12 raw-event branch but not the exact reconstructed
coordinate value.

### Wired Wacom Cintiq 13HD coverage

The focused fixture is commit `160a0ac` on branch
`device/wacom-cintiq-13hd`. It uses ordinary TinyUSB descriptors, HID
callbacks, and soft disconnect/reconnect. No retail `056a:0304` report
descriptor capture is available, so its 91-byte descriptor is deliberately
protocol-equivalent: one 10-byte Pen input report ID 16, one 10-byte Pad input
report ID 17, and Feature report ID 2 with one data byte. It does not claim
byte-exact retail topology, Touch, Touch Ring, or `ABS_WHEEL`.

The exact hardware-test artifacts are:

```text
host UF2 SHA-256       ec47bd6725c1b2b49f7ded92fc220a7fb5d2408ee8a479dd76a6aeaa612546a5
host text/data/bss     605456 / 788 / 245408 B
emulator UF2 SHA-256   1f7bb41c608e2d9595320a28523cc3cf9d82859a25504230334450fbc00c0a9c
emulator text/data/bss 48828 / 0 / 252372 B
hardware verdict       passed 2026-08-01 for the scope below
```

The first Cintiq generation disconnects at 600 ms, before the approximately
one-second initialization deadline, and waits another 1,600 ms while detached.
The fixture rejects any mode SET or GET during that generation or after its
disconnect. The next generation requires exactly one Feature report 2 SET with
value 2 no earlier than 900 ms, followed by exactly one matching GET. It sends
Pen enter/move/exit with serial `0x12345678`, then all nine numbered Pad
buttons. A same-PID reconnect requires a fresh SET/GET exchange before Pen and
the ninth-button smoke. The terminal alert sequence is `f1, f2, f3, f10`;
`f12` plus a phase letter reports failure.

The hardware log contained three Cintiq generations, six matching Pen/Pad
adds and removes, all nine Pad mappings `f13` through `f21`, and the expected
second `f21` reconnect smoke. All 21 heap snapshots reported `oom=0`,
minimum-ever free heap was 47,592 B, and every terminal Cintiq removal returned
to `free/largest/blocks=60752/48520/9`. Minimum task watermarks were TinyUSB
265, KeyD 658, async 389, work 217, timer 348, lifecycle 218, and report 859
words. No `f12`, host `ERR`, or `HID_REPORT_SKIP` appeared. The diagnostics for
tablet ABS/tool/tip and MSC serial `305419896` are the existing downstream
KeyD boundary; the latter is the test serial `0x12345678`.

This fixture proves Wacom-specific cancellation before the delayed deadline
and two clean initialization generations. It does not add a disconnect while
the callback is running, generic promotion-window cancellation, simultaneous
cancelers, callback self-requeue, delayed queue destruction, tick-wrap, or the
paired `056a:0333/0335` topology to the existing common-layer verdicts.

### Wired Wacom CTL-472 coverage

The hardware-verified 32-reconnect revision of branch
`device/wacom-wired-matrix` emulates wired CTL-472 `056a:037a`. Its interface
0 uses the exact captured 228-byte report descriptor
(`956e9e8a183dcb56aec2fc42e059a4dfff0b90484e58875b8f2d0f02b0906429`).
Available captures contain only interface 1's 38-byte length and endpoint
shape, so that interface is functionally equivalent rather than byte-exact. It
still produces the required 64-byte packet length and reaches upstream's
ghost-interface rejection.

That automatic sequence checked:

1. one completed idle boot-mouse report proving interrupt IN is open, then D+
   removal 600 ms later, before the one-second delayed init;
2. exactly Feature SET report 2 payload `02`, then GET report 2 payload `02`,
   accepted no earlier than 900 ms after mount;
3. wired status with the wireless-module bit clear, then X/Y, pressure,
   distance including the parser's over-range clamp, tip, both stylus buttons,
   pen/eraser, proximity, range, and release reports after the completed
   exchange;
4. D+ removal while the device-side GET callback is suspended, callback
   release while disconnected, then a fresh generation and full pen matrix;
5. 32 additional reconnects, each with a fresh exact exchange and completed
   pen transfers.

The host-visible success sequence is `f1, f2, f3, f4, f10`; `f12` plus
`a`/`b`/`c`/`d` repeated three times identifies the failed phase. Emulator CDC
is not an oracle. The exact pair below passed on 2026-07-29. The host log
showed all 36 Wacom Pen add/remove generations, 36 expected
`WARN: HID_IGNORED` results, no input before mode exchange, clean input after
recovery, stable removal plateaus, `oom=0`, and nonzero task watermarks. F1
also showed the Pen device published before removal, so that phase credits the
pre-deadline pending-work path.

Verified artifacts and measurements are:

```text
host UF2 SHA-256       bdf6ab6ce3bfbe1ed81abb4dcfb9183030d597f92e4e9d301bae8f474a436737
host text/data/bss     585656 / 788 / 245260 B
emulator UF2 SHA-256   614401701850d5bbea0dde53ce005de9d0a76aacddf3bda5112a08c4b2c9048e
emulator text/data/bss 59804 / 0 / 254444 B
hardware verdict       passed 2026-07-29
```

Repeated Wacom removals returned to
`free/largest/blocks=60728/36192/7` after the first warm-up variant.
Minimum-ever free heap was 7,848 B and every snapshot reported `oom=0`.
Minimum stack watermarks were TinyUSB 265, KeyD 658, async 389, work 205,
timer 348, lifecycle 222, and report 870 words. The Wacom input reached the
Linux input/evdev boundary; current KeyD intentionally has no full tablet
policy and logs the unsupported pressure/distance/tool codes.

The fixture proves the Wacom-specific disconnect before its deadline and while
the device-side GET callback is held. It does not deterministically force
cancel after promotion but before callback entry, simultaneous synchronous
cancelers, callback self-requeue, workqueue destruction with delayed entries,
or FreeRTOS tick wrap. Those remain static code-audit results.

A separate shortened CTL-only 8-reconnect emulator build is distinct:

```text
emulator UF2 SHA-256   2d95713f875eb6115f1e862d2a48e8c9296c34d714e3e58128647f47988ecaff
emulator text/data/bss 59820 / 0 / 254444 B
hardware verdict       not run
```

The working host candidate now restores pinned-Linux
`void devm_release_action()`, uses two managed-input devres records, and
publishes VID/PID from `input_dev->id` without interpreting Wacom's opaque
driver data. The complete 32-reconnect artifact reran against this candidate:
the log contained 36 `056a:037a` Pen adds and removes, 36 expected
ghost-interface warnings, and `f1, f2, f3, f4, f10` with no `f12` or host
`ERR`. Repeated Wacom removal snapshots stabilized at
`free/largest/blocks=60736/36032/7`; every heap snapshot had `oom=0` and every
task watermark remained nonzero.

```text
host UF2 SHA-256      12a8f6826c9148eb03a9fb783e1f558674d067f562bfcec2ad69edcf3dd09790
host text/data/bss    585720 / 788 / 245260 B
hardware verdict      passed 2026-07-30 with complete 32-reconnect artifact
```

The subsequent cleanup removes the never-read port-only
`input_dev.registered` state and documents the reduced `WARN_ON` and kfifo
contracts. It builds, but this exact image has not been flashed:

```text
host UF2 SHA-256      c2542706419b44a00d1ba8dcaa562b50afc272faab798cab62267fa9c873e1ca
host text/data/bss    585704 / 788 / 245260 B
hardware verdict      not run
```

Bluetooth, receivers, other Wacom IDs, touch, pad, LED, battery, and Remote
paths are outside this fixture.

### Expanded wired Wacom matrix coverage

The later exact `device/wacom-wired-matrix` artifact extends the historical
CTL-472-only result to CTL-672 `056a:037b`, PTK-450 `056a:0029`, CTH-470
`056a:00de`, and PTH-650 `056a:0027`, while retaining CTL-472
`056a:037a`. The CTH and PTH report descriptors are capture-exact, but the
combined fixture uses its compile-time 64-byte endpoint-0 size instead of the
retail devices' 32-byte and 16-byte values. This is an exact host/emulator
binary result, not a claim that those two complete USB topologies are
byte-identical to retail hardware.

The exact pair and audited host log are:

```text
host UF2 SHA-256       4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5
host text/data/bss     592312 / 788 / 245264 B
emulator UF2 SHA-256   037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b
emulator text/data/bss 65820 / 0 / 254456 B
host log SHA-256       c925adc49044ec7ffe8286cb2bc445aaf652c147b5e99d6e307860b2bcdf5714
hardware verdict       passed 2026-07-30 for the qualified scope below
```

The host-visible marker order was
`f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10`, with no `f12`, failure
letter, host `ERR`, timeout, or input-drop marker. The 23 physical Wacom
attachments were six CTL-472, three CTL-672, five PTK-450, four CTH-470, and
five PTH-650 generations. They published and removed 46 input nodes:

- CTL-472 and CTL-672: nine Pen adds and nine removals;
- PTK-450: five Pen and five Pad adds, each with five removals;
- CTH-470: four Pen, four Finger, and four Pad adds, each with four removals;
- PTH-650: five Pen, five Finger, and five Pad adds, each with five removals.

There were nine expected `WARN: HID_IGNORED` messages, one for every CTL ghost
interface, and nine `WARN: EVDEV_BATCH_CAP` messages, one for every CTH/PTH
touch attachment. No other warning appeared. The bounded-batch warning means
the fixture's two-contact frames passed; it does not certify a maximum-contact
frame. All 226 logged key transitions appeared in identical adjacent
device/KeyD/virtual-output sequences, including every marker down/up. The ten
`HID_REPORT_SET_Q` diagnostics each had an immediately following
`HID_REPORT_SET_OK`; they belong to the ten alert-keyboard generations and are
not a separate Wacom control-value oracle. The emulator state machine remains
the oracle for the required Wacom mode and LED exchanges.

All 115 heap snapshots reported `oom=0`. Minimum-ever free heap was 7,568 B;
the lowest snapshot-time free heap was 18,232 B. Repeated terminal profile
plateaus were:

```text
CTL-472/CTL-672 free/largest/blocks  60752 / 36712 / 6
PTK-450          free/largest/blocks 60752 / 22576 / 8
CTH-470          free/largest/blocks 60752 / 21704 / 9
PTH-650          free/largest/blocks 60752 / 26992 / 8
```

The first CTL removal had the same free/largest values with seven blocks. One
immediate PTH status-disconnect snapshot was
`free/largest/blocks=60496/26992/9`, exactly 256 B and one block below the
normal PTH plateau. The next PTH attachment and every later terminal removal
returned to their normal tuples, so the allocation was transient rather than
cumulative retention. The ten alert-attached snapshots were exactly
`48312/24960/8`; the first nine alert removals were exactly
`60736/34712/8`. The capture ends immediately after terminal `f10 up`, with
the final alert still attached, so its removal is not part of the verdict.

Minimum remaining stack watermarks were TinyUSB 265, KeyD 658, async 389,
work 198, timer 348, lifecycle 218, and report 854 words.

The `f11` state machine proves completion of the PTH battery-report transfers,
the immediate-status disconnect, the battery-plus-running-LED-work
disconnect, and recovery. Production host logging contains no `POWER` lines,
however. The 256-byte transient and later heap recovery are lifetime evidence,
but they do not establish the exact detached `ADDED`, `CHANGED`, and `REMOVED`
snapshot values or ordering, nor independently prove which consumer reclaimed
that allocation. Exact power-snapshot observation remains outside this
hardware verdict.

Bluetooth, receiver `056a:0084`, AES `056a:5048`, battery-expiry timing, and
ExpressKey Remote remain outside this expanded wired fixture.

### Wacom AES and USB receiver coverage

The final `device/wacom-wired-matrix` artifact runs only the newly added Yoga
260 AES `056a:5048` and Wacom USB receiver `056a:0084` phases; it does not
repeat the already-qualified wired profiles. The AES interfaces use the exact
captured 522-byte touch and 434-byte pen report descriptors. The receiver uses
the captured three-interface ACK-40401 topology and monitor/pen descriptors;
its touch interface reuses the exact PTH-650 descriptor.

The available receiver capture reports child PID `033b`, which is selected by
the later Intuos stage but was not part of this older receiver artifact. The
fixture deliberately selects PTH-650 profile `0027` to exercise the same
pinned sibling-rebind path. This is protocol coverage, not a claim that
`0084 -> 0027` was captured from retail hardware.

The exact pair and audited host log are:

```text
host UF2 SHA-256       8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8
host text/data/bss     594360 / 788 / 245264 B
emulator UF2 SHA-256   8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48
emulator text/data/bss 68504 / 0 / 254480 B
hardware verdict       passed 2026-07-30 for the qualified scope below
```

The host-visible alert sequence completed the AES phase, receiver phase, and
terminal success without a failure marker or host `ERR`. Four AES attachments
published and removed four Pen and four Finger inputs. Four physical receiver
attachments produced four balanced dynamic `056a:0027 (WL)` graphs, each with
Pen, Pad, and Finger inputs: 20 Wacom input adds and 20 matching removes in
total.

The AES phases cover exact control IDs, directions, lengths and payloads,
Pen/Finger input, ordinary battery updates, disconnect with delayed battery
work pending, idle-proximity timer cancellation, and recovery. The receiver
phases cover:

- pairing before the original one-second sibling `init_work` deadline;
- synchronous cancellation of both original sibling callbacks before resource
  release, with the fresh rebind control held beyond the stale deadline so a
  surviving callback would create a rejected duplicate;
- Pen/Pad/Finger input, center-button LED selection, and battery update;
- logical unpair while teardown control is running, queued re-pair, fresh
  control exchange, and recreated-input smoke;
- physical disconnect before monitor-report work can be relied upon;
- physical disconnect with teardown control held and re-pair queued;
- final clean pair, input, battery update, and removal.

All 47 heap snapshots reported `oom=0`. Minimum-ever free heap was 9,464 B.
AES and terminal physical receiver removal returned to the established 60,496
or 60,752 B plateaus without cumulative retention; logical unpair correctly
kept the monitor allocation alive. Minimum remaining stack watermarks were
TinyUSB 265, KeyD 658, async 389, work 202, timer 332, lifecycle 182, and
report 854 words.

The log contains four `EVDEV_BATCH_CAP` warnings plus unsupported evdev code,
absolute-axis, and miscellaneous-event messages. These are the existing
Linux-to-KeyD consumer boundary for tablet events; no `EVDEV_INPUT_DROP` or
host transport error appeared.

Production firmware does not log detached power-supply snapshots, so exact
battery values and event ordering are not part of the verdict. A device-side
interrupt-IN acknowledgement alone cannot prove that PTH/receiver pending
battery work had already reached the host report task. The fixture constructs
and cancels AES delayed work but does not wait for the real 30-minute expiry.
It deterministically proves the receiver's original sibling work while pending,
but the short pre-PID callback cannot be externally held after promotion or
while executing without a host test hook or SWD. Generic promotion-window,
simultaneous-canceler, callback-self-requeue, queue-destruction, and tick-wrap
branches remain static workqueue audit results.

Bluetooth, ExpressKey Remote, bootloader, I2C, PCI, receiver children outside
the selected `0027` profile, and all other Wacom IDs remain outside this
fixture.

### External wired Wacom Intuos coverage

The focused `device/wacom-wired-matrix` artifact selects eleven external wired
profiles without replaying the previously qualified base, AES, or receiver
matrices: Intuos `056a:0302/0303/030e/0323`, Intuos Pro
`056a:0314/0315/0317`, and Intuos 2 `056a:033b/033c/033d/033e`. Exact captures
are available only for PTH-851 `0317` Pen/Pad and CTH-490 `033c`
Pen/Pad/Touch plus its compatibility mouse. The `0314/0315/0317` profiles use
`0317` Pen/Pad plus the earlier PTH-650 Touch capture; `033b/033d/033e` reuse
the `033c` topology; and `0302/0303/030e/0323` reuse the earlier CTH-470 Pen
plus CTH-490 Pad/Touch captures. This verifies selected identities and family
behavior, not byte-identical retail topology for every model.

The exact pair and audited host log are:

```text
host UF2 SHA-256       1bd3124acf0d3058bf798df8b59cc796aebc43cca09a36cf39ea4a96b8f94fed
host text/data/bss     605224 / 788 / 245408 B
emulator UF2 SHA-256   93f034b64d9eec4537477acfb9763d3cbe0590017338f5a561619b74d5da2fac
emulator text/data/bss 66080 / 0 / 254480 B
host log SHA-256       070ac67476e632924b5cb7a2491989dc5c445b455751e7e8e792a63d05d3cd10
hardware verdict       passed 2026-07-31 for the qualified scope below
```

All eleven selected PID profiles completed mode SET/GET and Pen/Pad smoke;
`0314/0315/0317` completed Pro LED initialization, and the seven touch-capable
profiles completed Finger smoke. The sequence performed ten different-PID
detach/attach transitions. Representative battery reports were injected for
`0302`, `0314`, and `033b`. The profiles published and removed 29 expected
Pen/Pad/Finger input nodes and produced eleven
expected compatibility-interface `HID_IGNORED` and seven
`EVDEV_BATCH_CAP` warnings, and ended with `f15, f10`. No `f12`, host `ERR`,
timeout, input-drop marker, generic compatibility-mouse input, or OOM appeared.

The first Pro run exposed a target-memory boundary rather than an emulator
failure. Linux normally duplicates a single explicitly declared vendor usage
to every VARIABLE Feature report value. On the captured `0317` PTH-851 family
descriptor this created 626 duplicate usage/priority pairs; the emulator
reuses that capture for `0314/0315`. The exact-ID port quirk for
`0314/0315/0317` retains the complete report count, value/new-value arrays,
wire length, raw bytes, and request return semantics, but stores only the one
declared usage mapping consumed by pinned Wacom. It is a firmware memory
optimization, not an upstream Linux bugfix and not a global reduction of
`HID_MAX_USAGES`. The 626-pair count is not a claim about separately captured
retail `0314/0315` descriptors.

Before that compaction, Pro Pen and Pad left 10,688 and 10,560 B free and the
Finger probe failed with `HID_PROBE_NOMEM`, `oom=2`, and a 976-byte
minimum-ever value. In the verified image the corresponding snapshots were
30,696, 30,568, and 17,320 B free. All 63 heap snapshots reported `oom=0`, the
run's minimum-ever value was 12,832 B, and every terminal tablet removal
returned to 60,736 B free without cumulative retention. Minimum remaining
task watermarks were TinyUSB 265, KeyD 658, async 384, work 207, timer 348,
lifecycle 212, and report 859 words.

The fixture completes representative battery interrupt-IN transfers on the
emulator side, but that cannot prove host parser/work enqueue. Production
firmware does not log detached power-supply snapshots, so exact `PRESENT`,
status, capacity, and event ordering remain outside this verdict.

This focused sequence advances only after each profile's initialization and
input smoke completes. It does not add pending/running-work cancellation,
same-PID reconnect, Touch Ring semantics, explicit pen-touch arbitration,
receiver `0084 -> 033b`, or exact multitouch/max-contact oracles. Earlier
focused Wacom fixtures cover the cancellation, same-PID reconnect, ring, and
arbitration paths; receiver `0084 -> 033b` and exact multitouch/max-contact
oracles remain uncovered.

Heavier FF drivers should stay deferred for now:

- `hid-sony.c`
- `hid-playstation.c`
- `hid-nintendo.c`
- `hid-lg.c` / `hid-lg4ff.c`
- the broader FF/capability paths in `hid-logitech-hidpp.c`

Those drivers mix FF with larger request/response protocols, LEDs, sysfs,
device state, or controller-specific workers.

## More Useful Emulator Targets

The current emulator set gives good architectural coverage, but not complete
per-driver coverage. The next useful targets are:

1. one HID interface with `bulk IN, interrupt IN, bulk OUT, interrupt OUT`,
   followed by a variant with two interrupt endpoints per direction. These
   prove the new upstream-shaped first-interrupt-pair selection and PIO slot
   conservation; the ordinary haptic fixture does not.
2. a multi-device/hub sequence that creates at least two overlapping retiring
   cache epochs before reconnect. It must prove deferred ATTACH resumes on the
   final-release wake without `HID_USB_DEV_ALLOC_FAIL`.
3. one generic `HID_QUIRK_MULTI_INPUT` device not already represented by KYE or
   Chicony, only if multi-input behavior looks suspicious in hardware.

Most remaining active lightweight drivers do not need one emulator each before
another broad hardware pass. They mostly reuse the same already-covered hook classes:
fixup, mapping, mapped, event, simple probe, or raw event.

## Hardware Result Template

Use this checklist when logs come back from hardware. Paste the relevant host
and emulator CDC lines under each item.

- [x] full direct HID++/battery/DJ lifecycle at temporary RP2040 limits
  - `HID_MAX_FIELDS=8`, `HID_MAX_USAGES=256`
  - verdict: all current automatic device graphs completed with stable cleanup
    and `oom=0`; temporary capacity/lifecycle checkpoint only
- [x] retained `HID_MAX_FIELDS=64`, `HID_MAX_USAGES=675` capacity boundary
  - standalone and simultaneous M705 and ordinary keyboard children work
  - the complete HID++ eQuad keyboard profile exceeds the available RP2040
    heap and adds exactly one OOM count
  - teardown recovers and following standalone devices still work
- [x] Logitech Nano and full Lenovo logic at temporary `64/256`
  - `c532/c52f/c534` and `6009/6047/60ee` reached terminal `f10`, with
    balanced cleanup, `oom=0`, and nonzero watermarks
  - retained 675-usage allocation arithmetic excludes `c52f/c534/60ee` from
    the RP2040 special-driver allowlist; this is not a retained-policy pass
- [x] practical exact-class HID++/Unifying matrix
  - emulator branch `device/logitech-hidpp-unifying-matrix`
  - two M560 and two T650 generations, then one K400 and one K750 generation
  - all required Linux input events, independent removals, terminal direct
    `f10`, no `f12`, and stable equivalent heap/stack plateaus
  - K750 snapshot values remain intentionally unacknowledged by the noop UI
  - verdict: passed 2026-07-26; exactly one expected complete-eQuad OOM
- [x] first UC-Logic tablet matrix
  - emulator branch `device/uclogic-tablet-matrix`
  - require `f1`, `f2`, `f3`, `f4`, terminal `f10`, and no `f12`
  - require Huion Pen/Pad/Touch Strip/Dial and Deco Pen/Pad/Mouse events
  - compare every repeated alert heap plateau; require `oom=0` and nonzero
    lifecycle stack watermark
  - allow exactly one `WARN: HID_IGNORED` for Deco interface 1
  - verdict: passed 2026-07-26; lifecycle minimum 92 words
- [x] modern UGEE-v2 tablet expansion
  - cover Deco L/LW and Deco Pro S/SW/MW exact IDs and shared topology
  - require dial direction, battery capacity/charging, reconnect re-probe, and
    detach during reconnect work
  - include the Magic Mouse/Trackpad generic-battery regression
  - compare live and removal heap/stack snapshots; require `oom=0`
  - verdict: passed 2026-07-27; complete markers, no `f12`/host `ERR`, stable
    plateaus, and 86-word minimum lifecycle watermark
- [x] Deco 01 original / Parblo A610 Pro exact-ID matrix
  - emulator branch `device/uclogic-deco-parblo`, commit `c843295`
  - require exact per-profile string/probe order, all Pad keys, Parblo Dial and
    Mouse, one same-PID reconnect per profile, and terminal `f15, f10`
  - require ten balanced target input lifetimes, stable 60,752/60,736-byte
    removal plateaus, `oom=0`, nonzero task watermarks, no `f12`,
    `HID_REPORT_SKIP`, or host `ERR`
  - verdict: passed 2026-07-31; all 22 heap snapshots have `oom=0` and the
    lifecycle minimum is 85 words; KeyD tablet/Dial policy remains outside scope
- [x] UC-Logic failed-probe combined-descriptor cleanup
  - three reached `hid_hw_start()` failures after descriptor generation return
    to the same 60,752-byte plateau, followed by one normal recovery generation
  - verdict: passed 2026-08-01 with six balanced target input lifetimes, all 25
    snapshots at `oom=0`, and nonzero task watermarks
  - the temporary fault control was removed; `hid_parse()` is source-audited
    only, and the exact production-clean UF2 was not flashed unchanged
- [x] wired Wacom CTL-472
  - emulator branch `device/wacom-wired-matrix`
  - require `f1`, `f2`, `f3`, `f4`, terminal `f10`, and no `f12`
  - require exact mode SET/GET, wired status, Pen/eraser input boundaries,
    pending/running work removal, recovery, and 32 reconnect cycles
  - require 36 Pen add/remove generations, 36 expected ghost-interface
    warnings, equivalent removal heap plateaus, `oom=0`, and nonzero task
    watermarks
  - verdict: exact no-PIO host and 32-reconnect emulator passed 2026-07-29;
    all markers and 36 generations completed, no host `ERR`, `oom=0`, stable
    removal plateau, and every task watermark nonzero
  - the Linux-shaped input/devres and evdev identity correction passed the
    same complete fixture on 2026-07-30 with all 36 devices published as
    `056a:037a`, stable removal plateaus, no host `ERR`, and `oom=0`
  - the separate shortened CTL-only 8-reconnect artifact is build-only and does not
    inherit that verdict
- [x] expanded wired Wacom matrix
  - exact host
    `4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5`
    and emulator
    `037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b`
  - require `f1, f2, f5, f3, f6, f7, f8, f9, f11, f4, f10`, no `f12`,
    failure letter, host `ERR`, timeout, or input-drop marker
  - require 23 physical attachments and balanced publication/removal of all
    46 CTL/CTH/PTK/PTH Pen, Pad, and Finger input nodes
  - allow exactly nine CTL ghost-interface `HID_IGNORED` and nine CTH/PTH
    `EVDEV_BATCH_CAP` warnings; require no other warning
  - require 115 heap snapshots with `oom=0`, nonzero task watermarks, stable
    per-profile removal plateaus, and reclamation of the one immediate
    256-byte PTH teardown difference
  - verdict: passed 2026-07-30 for mode/input/LED/work/composite lifecycle and
    memory cleanup; exact detached power-snapshot values and ordering remain
    unverified because production logging emits no `POWER` records
- [x] Wacom AES and USB receiver matrix
  - exact host
    `8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8`
    and emulator
    `8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48`
  - require four AES and four receiver attachments, balanced Pen/Finger and
    dynamic Pen/Pad/Finger lifetimes, receiver pair/unpair/re-pair, fresh
    rebind after held teardown, physical-disconnect recovery, no failure marker
    or host `ERR`, `oom=0`, and nonzero task watermarks
  - require cancellation of both original sibling initialization callbacks
    before rebind resource release; keep the fresh control held beyond the
    stale deadline so duplicate initialization is rejected
  - verdict: passed 2026-07-30 with 20 balanced input lifetimes and `oom=0` in
    all 47 heap snapshots; exact power values/order, real 30-minute AES expiry,
    post-promotion/running original pre-PID callback, and other receiver child
    profiles remain outside the verdict
- [x] external wired Wacom Intuos matrix
  - exact host
    `1bd3124acf0d3058bf798df8b59cc796aebc43cca09a36cf39ea4a96b8f94fed`
    and emulator
    `93f034b64d9eec4537477acfb9763d3cbe0590017338f5a561619b74d5da2fac`
  - require all eleven selected PID profiles to complete mode SET/GET and
    Pen/Pad smoke, the three Pro profiles to complete LED initialization, and
    the seven touch-capable profiles to complete Finger smoke
  - require 29 balanced input lifetimes, ten different-PID detach/attach
    transitions, representative `0302/0314/033b` battery IN completion,
    `f15, f10`, no `f12` or host `ERR`, `oom=0`, and nonzero task watermarks
  - require the three Pro profiles to complete paired Finger probe under the
    retained `64/675` parser policy and every profile removal to return to one
    stable heap plateau
  - verdict: passed 2026-07-31; all 63 heap snapshots reported `oom=0`, minimum
    heap was 12,832 B, terminal removals returned to 60,736 B, and the Pro
    feature-usage optimization retained full report values and wire bytes;
    family captures do not establish byte-exact retail descriptors for every
    selected PID, and exact power snapshots remain unobserved
- [x] wired Wacom Cintiq 13HD `056a:0304`
  - exact host
    `ec47bd6725c1b2b49f7ded92fc220a7fb5d2408ee8a479dd76a6aeaa612546a5`
    and emulator branch `device/wacom-cintiq-13hd`, commit `160a0ac`, image
    `1f7bb41c608e2d9595320a28523cc3cf9d82859a25504230334450fbc00c0a9c`
  - require `f1, f2, f3, f10`, no `f12`, host `ERR`, `HID_REPORT_SKIP`, or OOM
  - require pre-deadline cancellation with no late Feature traffic, then two
    fresh Feature report 2 SET/GET exchanges with value 2
  - require Pen enter/move/exit, serial `0x12345678`, all nine Pad buttons,
    same-PID reconnect, six balanced Pen/Pad lifetimes, stable terminal heap,
    and nonzero task watermarks
  - verdict: passed 2026-08-01; all 21 snapshots reported `oom=0`, every
    terminal Cintiq removal returned to `60752/48520/9`, and the minimum
    lifecycle watermark was 218 words
  - the protocol-equivalent descriptor is not a retail capture; Touch, Touch
    Ring, `ABS_WHEEL`, and paired `056a:0333/0335` remain outside this verdict
- [x] production-clean direct HID++ post-test cleanup
  `a79e4385cec2987571226273951b492276e0275f495ebdc598bce2d8bba8498b`
  with emulator
  `31aa4485df67432e0d146a2d8fda3b46e93d2d4b70daab7e42e8016408a40149`
  - build/descriptor result: reproducible; host
    `text/data/bss=517376/788/245088`, emulator
    `60964/0/254960`
  - hardware basis: the exact diagnostic pair below completed the same
    wait/reply/BUSY/timeout/cancel/reconnect implementation twice
  - verdict: accepted by the user without a separate post-cleanup hardware run;
    do not relabel these exact production SHA values as directly flashed
- [x] direct HID++ request/reply host
  `9577a3e2820e99615b62e6535a1c01fbd403546c3bad233d9834a42f0dc88902`
  - emulator branch `device/logitech-hidpp-direct`, UF2
    `2a0ff72e47fb4a8bff7e46a2550b69b5f58f0be3bb8ee91eb2e4d6d837a8ca4b`:
  - host: HID++ 1.0, HID++ 2.0, BUSY retry, protocol status, timeout,
    pending-response D+ disconnect, fresh-generation reconnect, and ordinary
    pointer input; terminal `f10` and no triple `f12`: complete twice
  - post-detach `free` / `largest` / `blocks`, minimum heap, `oom`, and every
    task stack watermark for the cold boot plus seven hot reconnects: stable
    values recorded above, `oom=0`, HID work minimum 191 words
  - verdict: passed 2026-07-24; diagnostic layer is removed before production
- [ ] superseded unverified host
  `0a7ff16341260c7d78300989142b30fecb30b8314c0b6316f2a7a353a3586ba7`
  with `device/work-input-drivers`
  `58d309156aa7fb4a8152f6623ff2a4a4e1e2c7c0fcebe44f90afcdc307eef496`
  - host: cold boot/reset, all five identities and `cafe:1005` input complete
    for at least three cycles; attach/peak/removal heap and stack values:
  - emulator:
  - verdict:
- [ ] superseded unverified host
  `0a7ff16341260c7d78300989142b30fecb30b8314c0b6316f2a7a353a3586ba7`
  with a freshly built `device/haptic-touchpad`
  - emulator commit and UF2 SHA:
  - host: multitouch/pointer, feature GET/SET, haptic feedback, and repeated
    unplug/replug; attach/peak/removal heap and stack values:
  - exact replacement/erase/queued-PLAY cases exercised, if any:
  - verdict:
- [x] 2026-07-23 haptic lifecycle and Magic Trackpad 2 certification
  - verdict: repeated hot and cold passes for three haptic output profiles,
    five-slot lifecycle/unplug/reconnect, and four-interface Trackpad 2
    mode/native/reconnect; `oom=0`
- [x] `device/razer-blackwidow`
  - host: Razer raw SET_REPORT completes; macro usage reaches KeyD as
    unsupported code `0x290`.
  - emulator: macro usage is gated by host SET_REPORT.
  - verdict: pass, checked on Pico host with emulator.
- [x] `device/hires-wheel`
  - host: `HID_REPORT_SET_Q` / `HID_REPORT_SET_OK`, leftshift down/up,
    pointer movement, scroll, and hi-res `REL_WHEEL_HI_RES` /
    `REL_HWHEEL_HI_RES` reach KeyD boundary as unrecognized REL codes 11/12.
    `EV_MSC` scan events may still log as unrecognized event type 4.
  - emulator: hi-res wheel fixture sends keyboard, pointer, wheel, and AC Pan
    reports after mount.
  - verdict: pass for HID async transport and input-boundary coverage; KeyD
    consumer support for hi-res scroll remains separate.
- [x] `device/quirks-atmel-ma901`
  - host: after upstream-style `hid->name` construction, no `HID_ADD_FAIL`;
    pointer/scroll/key input reaches the ErgoType host like laptop Linux.
  - emulator: emits generic keyboard, relative pointer, and wheel reports.
  - verdict: pass, checked on Pico host with emulator.
- [x] `device/quirks-jabra-version`
  - host: pre-probe descriptor/string callbacks complete, then old Jabra
    `bcdDevice` path ignores both HID interfaces; no KeyD input events appear.
  - emulator: emits generic keyboard, relative pointer, and wheel reports that
    would be visible if the quirk failed.
  - verdict: pass, checked on Pico host with emulator.
- [ ] `device/holtek-kbd-a055` LED output path
  - host:
  - emulator:
  - verdict:
- [x] `device/google-stadiaff` historical FF output path at `hid: stabilize stadia ff teardown`
  - host: layout change triggers FF upload/play, then ff-memless timer stop.
  - emulator: pointer moves up on rumble start and down on rumble stop.
  - verdict: pass, checked on Pico host with emulator.
- [x] `device/google-stadiaff` retained Stadia/ff-memless mutex conversion
  - host: targeted `FF_RUMBLE` upload/play reaches report ID 5, then the
    ff-memless duration timer stops it; the ordinary `FF_HAPTIC` hook is not a
    substitute for this trigger.
  - emulator: pointer moves up on rumble start and down on rumble stop.
  - verdict: pass on 2026-07-22 with the temporary linked/instrumented host
    image; two complete cycles separated by reconnect covered same-ID replay
    and running-work unplug/remove before the pair returned to the deferred
    CMake set.
- [x] `device/apple-ir` historical AppleIR path at `hid: stabilize stadia ff teardown`
  - host: `enter down` arrives from the two-packet AppleIR middle command;
    `enter up` arrives from the host timer path.
  - emulator: sends no explicit key-up report.
  - verdict: pass, checked on Pico host with emulator.

Overall result:

- superseded host `0a7ff16341260c7d78300989142b30fecb30b8314c0b6316f2a7a353a3586ba7`
  was not given a hardware verdict; record a new host SHA before using this
  template again
- needs another emulator branch before commit: yes/no, branch:
- FF host-driver gate still open: yes/no
- targeted reset/hub/cancel/event-overflow fault coverage actually run: list,
  or explicitly `none`
