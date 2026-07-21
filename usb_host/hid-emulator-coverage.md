# HID Emulator Coverage

This note tracks what the external emulator repo covers:
`../ErgoType-hid-devices`.

It is intentionally about hardware-test coverage, not about every active HID
driver in CMake. The current host build links the CMake HID allowlist from
`usb_host/linux/drivers/hid`, while the emulator repo has one branch per
targeted behavior or device family.

The Stadia and AppleIR entries below are retained as historical coverage for
checkpoint `hid: stabilize stadia ff teardown`. Neither path is in the active CMake allowlist.

## Build Status

Last sequential build pass: 2026-07-13

- host repo: `cmake --build build -j4` passed
- emulator repo: all 19 `device/*` branches built one by one and passed
- emulator branches built: 19

The emulator repo is currently on a dirty `device/haptic-touchpad` worktree.
Its `build/ErgoType.uf2` is always the last built artifact, not a stable
per-branch artifact archive. The current pending artifact has SHA-256
`370afde1483ca15c346c3ec25726f5b48b2d2ad3db245d567d8dcf476f42b0b5`.

### Current interrupt-IN STALL fixture

The dirty `device/haptic-touchpad` fixture stalls marker-keyboard endpoint
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

This STALL runs once per emulator task start, not again after its programmed
soft reconnect. Reset or reflash the emulator to repeat it. The current
fixture waits for clear-halt before continuing, so it does not test unplug
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
| 2026-07-14 | `device/google-stadiaff` | layout-change FF trigger sends Stadia rumble start and ff-memless timer stop; emulator marker moves pointer up on start and down on stop |
| 2026-07-14 | `device/apple-ir` | AppleIR two-packet middle command reaches `hid-appleir.c`; host emits `enter down`, and timer release emits `enter up` |
| 2026-07-14 | `device/hires-wheel` | resolution-multiplier SET_REPORT is queued/completed; keyboard, pointer, scroll, and hi-res wheel/hwheel events reach the Pico host input boundary |
| 2026-07-14 | `device/quirks-atmel-ma901` | after fixing upstream-style `hid->name` construction, the fixture matches laptop Linux behavior and generic pointer/scroll input is not falsely ignored |
| 2026-07-14 | `device/razer-blackwidow` | rechecked after upstream-style `hid->name` construction fix; Razer macro-enable SET_REPORT and macro input path still work |
| 2026-07-14 | `device/quirks-jabra-version` | old `bcdDevice` reaches `hid_lookup_quirk()` before probe; host ignores both HID interfaces and no KeyD input events appear |
| 2026-07-19 | `device/haptic-touchpad` | active multitouch/haptic build enumerates, pointer events move the cursor, and haptic output produces the emulator cursor-feedback signal |
| 2026-07-21 | dirty `device/haptic-touchpad` STALL fixture | one interrupt-IN STALL is cleared remotely and reset to DATA0 locally; marker `s` proves rearm and resumed input, and the programmed reconnect repeats the same active heap plateau |

## Current Emulator Branches

Current branch heads used for the build pass:

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
  historically exercised `hid-google-stadiaff.c` plus `ff-memless.c` and the
  short layout-rumble trigger preserved in checkpoint `hid: stabilize stadia ff teardown`.
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
| `report_fixup` | A4-style simple fixups plus `hid-holtek-*`, `hid-kye`, `hid-pxrc`, `hid-zydacron`, and other lightweight fixup-only drivers | `holtek-kbd-a055`, `kye-easypen-m406`, `pxrc-phoenixrc`, `zydacron-remote` |
| `input_mapping` / `input_mapped` | `hid-a4tech`, `hid-cypress`, `hid-ite`, `hid-zydacron`, simple key-remap drivers | `a4tech-x5-005d`, `cypress-mouse`, `ite8595-rfkill`, `zydacron-remote` |
| driver `.event` hooks | `hid-a4tech`, `hid-cypress`, `hid-ite`, `hid-saitek`, `hid-speedlink`, `hid-xinmo` | `a4tech-x5-005d`, `cypress-mouse`, `ite8595-rfkill`, `saitek-rat7` |
| `raw_event` hooks | `hid-chicony`, `hid-creative-sb0540`, `hid-primax`, `hid-pxrc`, `hid-rapoo`, `hid-saitek`, `hid-waltop`, `hid-zydacron` | `chicony-wireless-radio`, `creative-sb0540`, `primax-keyboard`, `pxrc-phoenixrc`, `rapoo-2_4g-receiver`, `saitek-rat7`, `zydacron-remote` |
| `input_configured` / extra input device naming | `hid-creative-sb0540`, `hid-retrode` | `creative-sb0540`; `hid-retrode` only renames per-report input devices and reuses the same `HID_QUIRK_MULTI_INPUT` input-core path |
| `HID_QUIRK_MULTI_INPUT` / `HID_QUIRK_INPUT_PER_APP` | `hid-retrode`, KYE entries from `hid-quirks.c`, `hid-chicony`, `hid-glorious` | `kye-easypen-m406`, `chicony-wireless-radio` |
| workqueue callback | `hid-input` LED work, future FF workers | LED path via `holtek-kbd-a055`; FF worker still gated below |
| async raw SET_REPORT | `hid-razer` | `razer-blackwidow` |
| async regular SET_REPORT | `hid-kye`, `hid-input` LED work | `kye-easypen-m406`, `holtek-kbd-a055` |
| async GET_REPORT to SET_REPORT continuation | `hid-input` resolution multiplier path | `hires-wheel` |
| USB interface metadata before probe | `hid-rapoo`, Razer mouse/keyboard protocol split | `rapoo-2_4g-receiver`, `razer-blackwidow` |
| product-string quirk before probe | name-based ignore entries in `hid-quirks.c` | `quirks-atmel-ma901` |
| `bcdDevice` version quirk before probe | Jabra version ignore entries in `hid-quirks.c` | `quirks-jabra-version` |
| Historical force feedback, inactive | `hid-google-stadiaff.c`, `ff-core.c`, and `ff-memless.c` at `hid: stabilize stadia ff teardown` | `google-stadiaff` |
| Historical timer/HIDDEV-force path, inactive | `hid-appleir.c` at `hid: stabilize stadia ff teardown` | `apple-ir` |

## Active Drivers Without Dedicated Fixtures

These active drivers do not need one emulator branch each before the first
hardware pass. They reuse hook classes already covered above.

- Core/common glue: `hid-core`, `hid-input`, `hid-generic`, `hid-drivers`,
  `hid-quirks`. These are exercised by every emulator branch plus the dedicated
  quirk branches.
- Fixup-only or simple fixup/probe drivers: `hid-aureal`, `hid-elecom`,
  `hid-gembird`, `hid-glorious`, `hid-holtek-mouse`, `hid-huawei`,
  `hid-keytouch`, `hid-macally`, `hid-maltron`, `hid-nti`, `hid-ortek`,
  `hid-redragon`, `hid-semitek`, `hid-sigmamicro`, `hid-topre`,
  `hid-viewsonic`, `hid-vrc2`, `hid-xiaomi`. These are covered by the Holtek,
  KYE, PXRC, and Zydacron fixup/probe fixtures.
- Mapping-only or fixup-plus-mapping drivers: `hid-accutouch`, `hid-cherry`,
  `hid-evision`, `hid-kensington`, `hid-lcpower`, `hid-monterey`,
  `hid-penmount`, `hid-sunplus`, `hid-tivo`, `hid-topseed`, `hid-twinhan`.
  These are covered by A4Tech, ITE, and Zydacron mapping fixtures.
- Mapping/event drivers without new transport behavior: `hid-ezkey`,
  `hid-gyration`, `hid-icade`, `hid-speedlink`, `hid-xinmo`. These are covered
  by A4Tech, Cypress, ITE, and Saitek event fixtures.
- Raw-event drivers without new request/lifecycle behavior: `hid-waltop`.
  This is covered by Primax, PXRC, Saitek, Rapoo, and Zydacron raw-event
  fixtures.
- Metadata/input-device naming only: `hid-retrode`. This uses
  `HID_QUIRK_MULTI_INPUT` and `input_configured()` to name per-report input
  devices; KYE/Chicony cover the multi-input/input-per-application core path,
  and Creative covers `input_configured()`.

Add a dedicated fixture for one of these only if the hardware smoke pass points
at that driver family or at a hook class not represented by the current
fixtures.

## Coverage Decision

The current emulator set is enough for the next hardware pass.

Reasoning:

- the host CMake allowlist links only the active HID `.c` files selected in
  `CMakeLists.txt`
- the 19 emulator branches cover the nontrivial behavior classes in that set
- remaining active lightweight drivers mostly reuse already-covered classes:
  `report_fixup`, `input_mapping`, `input_mapped`, `.event`, simple `.probe`,
  or `.raw_event`
- the known metadata-sensitive quirks are covered by dedicated negative tests:
  product string and `bcdDevice`
- output SET_REPORT paths are represented by keyboard LED/Holtek and Razer/KYE
  style request paths

Do not add more emulator branches before the first hardware pass unless a
specific active driver fails or a specific hook class looks suspicious in
hardware. The one reasonable optional emulator target is another
`HID_QUIRK_MULTI_INPUT` device if KYE/Chicony coverage turns out too narrow.

The Stadia fixture preserves historical coverage of simple memless rumble at
`hid: stabilize stadia ff teardown`. The active build instead uses the standard HID Haptics Page path;
the haptic-touchpad fixture covers its basic probe, pointer, and output flow.
Effect replacement/reuse and teardown races remain the relevant gaps.

## Hardware Test Matrix

The first hardware pass should not try to exhaust all 19 branches. Use these
as gates:

For each emulator branch:

```sh
cd ../ErgoType-hid-devices
git checkout device/<branch>
cmake --build build -j4
```

Then flash `../ErgoType-hid-devices/build/ErgoType.uf2` to
the emulator board. The host board should run the current ErgoType host build.

Recommended smoke order:

1. `device/razer-blackwidow`: proves raw async SET_REPORT and macro event path.
2. `device/hires-wheel`: proves resolution multiplier GET_REPORT to SET_REPORT
   continuation and hi-res wheel input.
3. `device/quirks-atmel-ma901`: proves product-string metadata reaches
   `hid_ignore()` before probe.
4. `device/quirks-jabra-version`: proves `bcdDevice` metadata reaches
   `hid_lookup_quirk()` before probe.
5. `device/holtek-kbd-a055`: proves ordinary LED output SET_REPORT does not
   block/assert.

If these five pass, the current emulator coverage is enough for the host commit.
Run the useful second-pass branches only if one of these gates fails or if a
specific driver family needs confirmation.

### Must Pass

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
- `device/haptic-touchpad` (`2a4f966`, pending route distinction)
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
- `device/google-stadiaff` (historical, checkpoint `hid: stabilize stadia ff teardown`)
  - expected device-side signal: emulator CDC log reports SET_REPORT for
    report ID 5 after a host layout change triggers rumble.

## Still Not Covered

The active `hid-haptic` driver creates an FF device through `ff-core.c`, and
the generic evdev/input output plumbing is live:

- `device_upload_ff()` reaches `input_ff_upload()`
- `device_erase_ff()` reaches `input_ff_erase()`
- `device_set_ff()` sends `EV_FF` through `evdev_write()` /
  `input_inject_event()`

The standard HID Haptics Page touchpad fixture already exercises
`hid-haptic.c`, `hid-multitouch.c`, asynchronous feature GET_REPORT probe, and
basic output. It does not yet force in-place effect replacement, rapid
PLAY-to-erase/reuse, unplug during queued PLAY, all five effect slots, or mode
restoration after the final Press/Release effect. It does not use `ff-memless`
or the historical Stadia layout-rumble trigger.

The post-probe activation step also needs a composite regression: all input
devices must receive one devmon ADD with final capabilities and no writer may
be reachable before every matching handle has opened. Input must begin only
after all prepared queues are published; unplug during partial/complete
activation or just before `driver_ready` must yield one matching removal
without a stale writer or post-detach publication.

For fixtures that queue a mode SET during probe (`haptic-touchpad` and the Kye
tablet-mode cases), its completion must precede the first published input. Also
unplug once while that SET is pending; lifecycle must leave through the normal
fenced teardown without `HID_WAIT_BUSY`/`HID_WAIT_OWNER` or a stalled board.

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

Heavier FF drivers should stay deferred for now:

- `hid-sony.c`
- `hid-playstation.c`
- `hid-nintendo.c`
- `hid-lg.c` / `hid-lg4ff.c`
- `hid-logitech-hidpp.c`

Those drivers mix FF with larger request/response protocols, LEDs, sysfs,
device state, or controller-specific workers.

## More Useful Emulator Targets

The current emulator set gives good architectural coverage, but not complete
per-driver coverage. The next useful targets are:

1. one generic `HID_QUIRK_MULTI_INPUT` device not already represented by KYE or
   Chicony, only if multi-input behavior looks suspicious in hardware.

Most remaining active lightweight drivers do not need one emulator each before
the next hardware pass. They mostly reuse the same already-covered hook classes:
fixup, mapping, mapped, event, simple probe, or raw event.

## Hardware Result Template

Use this checklist when logs come back from hardware. Paste the relevant host
and emulator CDC lines under each item.

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
- [x] `device/apple-ir` historical AppleIR path at `hid: stabilize stadia ff teardown`
  - host: `enter down` arrives from the two-packet AppleIR middle command;
    `enter up` arrives from the host timer path.
  - emulator: sends no explicit key-up report.
  - verdict: pass, checked on Pico host with emulator.

Overall result:

- current emulator coverage sufficient for host commit: yes/no
- needs another emulator branch before commit: yes/no, branch:
- FF host-driver gate still open: yes/no
