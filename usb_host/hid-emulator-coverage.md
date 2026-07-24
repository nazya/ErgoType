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
  current dirty build

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
| `report_fixup` | `hid-elecom`, `hid-evision`, `hid-topre`, `hid-holtek-kbd`, `hid-holtek-mouse`, `hid-kye`, `hid-pxrc`, `hid-zydacron`, and other active lightweight fixups | `work-input-drivers`, `holtek-kbd-a055`, `kye-easypen-m406`, `pxrc-phoenixrc`, and `zydacron-remote` verified; `holtek-mouse` fixture written, hardware pass pending |
| `input_mapping` / `input_mapped` | `hid-a4tech`, `hid-cypress`, `hid-evision`, `hid-ite`, `hid-kensington`, `hid-zydacron` | `work-input-drivers`, `a4tech-x5-005d`, `cypress-mouse`, `ite8595-rfkill`, and `zydacron-remote` verified |
| driver `.event` hooks | `hid-a4tech`, `hid-cypress`, `hid-ite`, `hid-saitek` | `a4tech-x5-005d`, `cypress-mouse`, `ite8595-rfkill`, `saitek-rat7` |
| `raw_event` hooks | `hid-chicony`, `hid-creative-sb0540`, `hid-primax`, `hid-pxrc`, `hid-rapoo`, `hid-saitek`, `hid-zydacron` | `chicony-wireless-radio`, `creative-sb0540`, `primax-keyboard`, `pxrc-phoenixrc`, `rapoo-2_4g-receiver`, `saitek-rat7`, `zydacron-remote` |
| `input_configured` / extra input device naming | `hid-creative-sb0540` | `creative-sb0540` |
| `HID_QUIRK_MULTI_INPUT` / `HID_QUIRK_INPUT_PER_APP` | KYE entries from `hid-quirks.c`, `hid-chicony` | `kye-easypen-m406`, `chicony-wireless-radio` |
| workqueue callback | `hid-input` LED work and active `hid-haptic` effect/stop work | LED path via `holtek-kbd-a055`; `haptic-lifecycle` verifies five-slot replacement, erase/replay, queued-work unplug, teardown, and reconnect |
| async raw SET_REPORT | `hid-razer` | `razer-blackwidow` |
| async regular SET_REPORT | `hid-kye`, `hid-input` LED work | `kye-easypen-m406`, `holtek-kbd-a055` |
| async GET_REPORT to SET_REPORT continuation | `hid-input` resolution multiplier path | `hires-wheel` |
| USB interface metadata before probe | `hid-rapoo`, Razer mouse/keyboard protocol split | `rapoo-2_4g-receiver`, `razer-blackwidow` |
| product-string quirk before probe | name-based ignore entries in `hid-quirks.c` | `quirks-atmel-ma901` |
| `bcdDevice` version quirk before probe | Jabra version ignore entries in `hid-quirks.c` | `quirks-jabra-version` |
| Deferred Stadia `FF_RUMBLE` through memless FF (`ff-core.c` remains active for HID Haptics) | retained `hid-google-stadiaff.c` and `ff-memless.c`; upload/timer/replay/running-work-remove/reconnect path passed before deferral | `google-stadiaff` |
| USB-only Magic Mouse / Trackpad parsing, MT mapping, and mode SET | `hid-magicmouse.c` | normal four-interface Trackpad 2 mode/native/reconnect path verified by the combined 2026-07-23 fixture; fault injection remains |
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
Chicony, Creative SB0540, Cypress, ELECOM, EVision, Holtek keyboard, ITE,
Kensington, KYE, Primax, PXRC, Rapoo, Razer, Saitek, Topre, and Zydacron; each
of those entries has a matching emulator branch. Core/common glue (`hid-core`,
`hid-input`, `hid-generic`, `hid-drivers`, and `hid-quirks`) is exercised by all
fixtures.

Files present under `usb_host/linux/drivers/hid` but commented out in CMake are
not active drivers and must not be listed here as covered merely because they
reuse an already tested hook shape.

## Coverage Decision

The existing emulator set plus the hardware-verified work-input and combined
haptic/Trackpad fixtures covers the previously active allowlist,
long-enumeration success path, and the normal USB Magic Trackpad 2 path. The
Holtek mouse driver-specific hardware result remains pending. Stadia's existing
fixture covers its deferred mutex-conversion path if it is relinked later.

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
