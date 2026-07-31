# PIO USB Host Memory Notes

This note tracks the RAM cost of adding TinyUSB host through Pico-PIO-USB on
RP2040.

This is the authority for current RP2040 link layout, static pools, heap size,
and measured structure/allocation costs. Dated test notes may preserve their
historical numbers, but other current-design documents should link here rather
than copy values which drift with every dirty build.

The FreeRTOS heap size is critical for this firmware. Every byte moved into
static `.data` or `.bss` reduces the maximum possible `configTOTAL_HEAP_SIZE`.
Task stack tuning can improve free heap at runtime, but it does not fix a link
failure caused by static RAM layout.

## Current Link Picture

The Microsoft wired-USB per-device-release checkpoint was:

```text
text/data/bss                 596296 / 788 / 245312 B
__bss_end__                   0x2003fe88
main-bank headroom            376 B to 0x20040000
delta from Wacom baseline     +1936 text / +0 data / +48 bss
UF2 SHA-256                   f809966886aa4bdaf42c694055ea08b9313d4dbd1120f1a96f0cebd8ba71fd48
hardware verdict              passed 2026-07-31
```

The 48-byte static increase is one linker-owned builtin-driver runtime record.
The demonstrated upstream function-static `last_key` has moved into
devres-owned `ms_data`, adding four dynamic bytes per bound Microsoft HID
instead of global BSS. No Microsoft workqueue, timer, force-feedback, or
asynchronous-request object is linked. The exact emulator returned free heap
to 60752 bytes after each representative Microsoft profile and recorded
47592 bytes minimum-ever free heap. Minimum free task watermarks were TUH 265,
KeyD 658, async 430, work 346, timer 348, lifecycle 171, and report 862 words.
Its exact input and control coverage is recorded in
`hid-emulator-coverage.md`; the preceding Wacom AES/receiver memory result
remains recorded in its stage below.

The cumulative Apple external-USB stage passed its exact hardware fixture:

```text
text/data/bss                 600920 / 788 / 245360 B
__bss_end__                   0x2003feb8
main-bank headroom            328 B to 0x20040000
delta from Microsoft stage    +4624 text / +0 data / +48 bss
delta from Wacom baseline     +6560 text / +0 data / +96 bss
UF2 SHA-256                   e55bc54d78acc77e14548d6f7dfc6f816fe8d00f712803c7aef9fb977cc5f096
emulator UF2 SHA-256          6e6ba43c7efad0677a372079f079701e5fa51f8cb65c30f4321a3fbaafe72255
emulator text/data/bss        60296 / 0 / 254452 B
hardware verdict              passed 2026-07-31; complete focused sequence
```

The exact static increase over Microsoft is only the linker-owned 48-byte
builtin-driver runtime. The module-parameter values are compile-time firmware
defaults and produce no additional mutable storage in this build. A matched
Magic Keyboard interface allocates its devres-owned `apple_sc`, generic battery
state, timer/request state, and reduced power-supply queue from the runtime
heap; none of those per-device objects are included in the static figures.

The run reached terminal `f10` with no `f12`, host `ERR`, or OOM. Minimum-ever
free heap was 38224 bytes. Removal after Magic Keyboard 2015 and both Magic
Keyboard 2024 generations returned to the same 60496-byte plateau. Minimum
free task watermarks were TUH 265, KeyD 658, async 395, work 346, timer 315,
lifecycle 220, and report 870 words.

The corrected ordinary-Logitech-receiver and Lenovo external-USB diagnostic
completed on hardware at temporary `HID_MAX_FIELDS=64`,
`HID_MAX_USAGES=256`:

```text
host UF2 SHA-256              9546c46c0196718cfabd2d8b7662468598ed86701285d3a8eead81996dc0de88
emulator UF2 SHA-256          4fb530a84cec731a4cbde00fe22502384c8eedc9daacffc6e1fd68df9f216636
hardware verdict              passed at temporary 64/256; terminal f10
c52f minimum free heap        17000 B
c534 minimum free heap         9296 B
60ee attached free heap       11152 B
receiver removal plateau      60752 B
oom                            0
```

At 256 usages, each `c52f/c534` physical and virtual wide Consumer field is
8,308 bytes. At the retained 675 cap, the physical range contains 652 entries
and requests 20,980 bytes, while the virtual range is capped at 675 and
requests 21,716 bytes. The pair therefore grows from 16,616 to 42,696 bytes,
an additional 26,080 bytes which cannot fit with the live receiver graph. The
full corrected `60ee` descriptor has a Consumer range from 1 through 767; its
field alone grows by 13,408 bytes from a run which left only 11,152 bytes free.

The RP2040 build therefore restores the retained `64/675` policy but selects
special drivers only for Logitech `c52b/c532` and Lenovo `6009/6047`. The exact
upstream `c52f/c534/60ee` rows and driver logic remain in source behind compile
gates.
Their `64/256` run is lifecycle and request/input evidence, not support at the
retained memory policy. The exact `ERR: HID_FIELD_NOMEM` marker now identifies
a failed persistent field allocation; it was not emitted in the passing
temporary-policy run.

The gated IDs still have Linux-style generic fallback. `c52f/c534` may publish
their physical generic interfaces, but receive no DJ/HID++ startup sequence and
create no virtual child. A standalone `60ee` can publish smaller generic
interfaces before its wide mouse/Consumer interface reaches the memory
boundary. The unchanged combined emulator waits for `c52f` startup reports and
therefore stops there; it cannot reach `c534/60ee` in the same run.

The resulting retained-policy host build is:

```text
text/data/bss                 603496 / 788 / 245408 B
__bss_end__                   0x2003fee8
main-bank headroom            280 B to 0x20040000
UF2 SHA-256                   a937994862faabf54c7fd98372d650fd89215ecbfab3f3449b0a245f488d6127
hardware verdict              not rerun after allowlist gating
```

The original 232 KiB heap experiment is historical evidence for the static-RAM
ceiling. With that tested host branch, the link failed with:

```text
region `RAM' overflowed by 13868 bytes
```

The failed map still shows the useful layout:

```text
.data total                 20600 B
.bss starts                 0x20005138
ucHeap                      237568 B = 232 KiB
RAM overflow                13868 B
```

Meaning:

```text
232 KiB heap did not fit with that host static RAM.
About 218 KiB was the practical upper heap size for that code.
```

This is not caused by FreeRTOS task stack depth alone. FreeRTOS task stacks are
allocated from `ucHeap` at runtime. The link failure happens earlier because
`ucHeap` itself is a static `.bss` array and there is not enough RAM left for it.

The 2026-07-21 task-side-parser checkpoint instead used a 218.5 KiB heap
(`(219 * 1024) - 512`). The extra 2 KiB came from keeping the immutable FAT12
format image in flash and copying it to a task-local RAM buffer only while
formatting. After adding the audited ELECOM, Kensington, Topre, and EVision
builtin drivers, that historical image linked with `text=505836`, `data=708`,
and `bss=245364`.

The 2026-07-23 checkpoint excluded Stadia/`ff-memless` from the link. It used
the fixed 218.5 KiB (`223744`-byte) FreeRTOS heap and clean-built as:

```text
text/data/bss                 514752 / 788 / 245040 B
__bss_end__                   0x2003fd38
main-bank headroom            712 B to 0x20040000
scratch X                     788 B (0x20040000..0x20040314)
scratch X / core-1 gap        1260 B to 0x20040800
hardware verdict              passed 2026-07-23; stable removal heap, oom=0
```

The diagnostic direct-HID++ request/reply checkpoint adds one flash-resident
driver descriptor and one 48-byte builtin-driver runtime block. It builds as:

```text
text/data/bss                 517976 / 788 / 245088 B
__bss_end__                   0x2003fd68
main-bank headroom            664 B to 0x20040000
scratch X                     788 B (0x20040000..0x20040314)
scratch X / core-1 gap        1260 B to 0x20040800
candidate UF2 SHA-256         9577a3e2820e99615b62e6535a1c01fbd403546c3bad233d9834a42f0dc88902
hardware verdict              passed twice 2026-07-24; hot lifecycle, oom=0
```

Relative to the current verified checkpoint, this is `+3224 B` text,
unchanged `.data`, and `+48 B` `.bss`; main-bank headroom decreases by exactly
48 bytes. `sizeof(struct hid_device)` remains 608 bytes. Adding the
generation-bound protocol-wait pointer changes `struct usbhid_device` from 216
to 220 bytes and its heap_4 block from 224 to 232 bytes, an 8-byte live cost
for each HID interface.

A matching HID++ interface additionally owns one 264-byte `hidpp_device`
inside a 288-byte devres/heap_4 block and one 96-byte FreeRTOS mutex block:
384 bytes persistent. Each RAP or FAP wrapper uses one sequential 64-byte
message allocation, a 72-byte heap_4 block, while its response remains on the
work-task stack. BUSY retries reuse that allocation, and input reports allocate
nothing in the HID++ matcher. Hardware acceptance still requires identical
post-removal `free`/`largest`/`blocks` plateaus across repeated cycles,
`oom=0`, and stable nonzero task stack watermarks; the 384-word `hid-work`
stack must retain at least 64 words after the full timeout/reconnect profile.

Removing the fixed diagnostic strings, BUSY trace state, and otherwise unused
RAP/FAP probe produces the production-clean pair:

```text
host text/data/bss            517376 / 788 / 245088 B
host __bss_end__              0x2003fd68
host main-bank headroom       664 B to 0x20040000
host UF2 SHA-256              a79e4385cec2987571226273951b492276e0275f495ebdc598bce2d8bba8498b
emulator text/data/bss        60964 / 0 / 254960 B
emulator UF2 SHA-256          31aa4485df67432e0d146a2d8fda3b46e93d2d4b70daab7e42e8016408a40149
hardware verdict              accepted post-test cleanup; no separate run
```

This removes 600 bytes of host text and changes no host `.data`, `.bss`,
structure size, or dynamic allocation. Protocol detection still uses the same
72-byte transient message block and the same generation-bound wait/cancel
bridge. The exact diagnostic pair above remains the hardware evidence; the
user explicitly accepted the reproducibly built cleaned pair without another
hardware run.

Opening the pinned upstream pre-connect HID++ 2.0 name and unit-ID/serial path
first produced this historical host-only candidate. That checkpoint also
carried the unit ID through devmon solely for a task-side CDC marker:

```text
host text/data/bss            518544 / 788 / 245088 B
host __bss_end__              0x2003fd68
host main-bank headroom       664 B to 0x20040000
host UF2 SHA-256              0a83e8be9da5993ce2baeb9c0cde55fe5ca187b0c398f845b259d4d8351b34e6
hardware verdict              none; matching emulator intentionally deferred
```

Relative to the production-clean request/reply image, this adds 1,168 bytes of
text and changes neither static RAM section. The device name buffer is bounded
by the HID++ one-byte length, copied into the existing fixed `hdev->name`, and
freed before probe returns; unit ID is copied into existing `hdev->uniq`.
The current tree removes the port-only unit-ID copies and devmon pointer.
`hdev->uniq` and `input_dev->uniq` remain available to the Linux-derived
drivers, while the KeyD task reports attachment as
`DEVICE: added <vid:pid> <name>`. Evdev still owns the bounded final-name copy
needed across queued ADD/removal. The size above belongs to the historical
checkpoint; current build size is recorded by the later checkpoints.

The combined diagnostic checkpoint adds direct HID++ battery, reduced HIDRAW
lifecycle, a dedicated lifecycle work queue, and the first DJ receiver
checkpoint (`046d:c52b` to at most one generic M705 child) on top of identity:

```text
host text/data/bss            529768 / 788 / 245160 B
host __bss_end__              0x2003fdc0
host main-bank headroom       576 B to 0x20040000
host UF2 SHA-256              ee14a1aac2b8e65ef84ef977c41f0efed58b18209196a1937ba35960a0634911
emulator UF2 SHA-256          e391ef3c3bab809076e86c415e07e213a668b5bcaa243fb2218069b803051872
hardware verdict              passed 2026-07-25; complete combined sequence
```

Relative to the identity-only candidate above, this adds 11,224 bytes of text
and 72 bytes of BSS; data is unchanged. Power events do not enter the ordinary
devmon/KeyD path. Devmon owns a separate four-member QueueSet, and each live
supply owns one length-one value queue. Full `ADDED`/`CHANGED` snapshots
coalesce by overwrite; the UI task currently ignores their values and removes
the queue after consuming terminal `REMOVED`.

Static sections do not include the runtime HIDRAW object, HID++ power-supply
object/queue, power QueueSet, receiver mutex, guaranteed HID-mode UI task/timer,
or virtual HID/parser/input/evdev/devmon graph. The combined run must therefore
show a stable free/min/largest/blocks heap plateau and all task stack watermarks
across battery changes, repeated pair/unpair, link loss, hot receiver unplug
during input, and fresh reconnect.

The run reached `f10` without an `f12` marker. Direct HID++ returned to the
same attached `free/blocks = 32152/5` state four times and the same removed
`60144/7` state three times. The DJ child returned to `28800/2` while attached
and `33648/8` after unpair; full receiver removal returned to `60400/8`.
`oom=0`; minimum remaining stack watermarks were `tuh=265`, `keyd=658`,
`async=414`, `work=163`, `timer=348`, `lifecycle=196`, and `report=838` words.
The noop UI provides no value-level power snapshot acknowledgement, so the
stable repeated removal plateau is the observable power lifetime evidence.

Removing only the three test-progress messages `DJ_RESYNC`, `DJ_READY`, and
`DJ_RECEIVER_GONE` produces the production-clean host:

```text
host text/data/bss            529696 / 788 / 245160 B
host __bss_end__              0x2003fdc0
host main-bank headroom       576 B to 0x20040000
host UF2 SHA-256              a650da20f755f84640c97e8933efdb17d47b368b1252c3fe6eae4a353f98bfd6
hardware verdict              accepted post-test cleanup; no separate run
```

This removes 72 bytes of text and changes no data, BSS, allocation, timing, or
driver control flow. The exact diagnostic host/emulator pair above remains the
hardware evidence.

### Full upstream DJ/HID++ child memory boundary

The later pinned-upstream re-port of `hid-logitech-dj.c` changes the dynamic
memory envelope substantially. A paired device now receives its complete
standard descriptor set plus the HID++ descriptor, and several virtual
children may coexist. This restores the upstream driver shape, but the RP2040
heap cannot represent every graph that upstream Linux can represent.

The retained firmware policy and then-current static layout for that checkpoint
were:

```text
HID_MAX_FIELDS                64 (upstream Linux: 256)
HID_MAX_USAGES               675 (upstream Linux: 12288)
sizeof(struct hid_report)     316 B
host text/data/bss            535208 / 788 / 245160 B
host __bss_end__              0x2003fdd0
host main-bank headroom       560 B to 0x20040000
```

Hardware testing distinguishes these receiver graphs:

- one M705 mouse child (`046d:101b`);
- one ordinary DJ keyboard child (`046d:4024`);
- simultaneous M705 mouse and ordinary DJ keyboard children;
- one HID++ eQuad keyboard connection child (`046d:4024`) with
  `STD_KEYBOARD | MULTIMEDIA | POWER_KEYS | MEDIA_CENTER | HIDPP`.

At the retained `64/675` policy, the single M705 and the single ordinary
keyboard each attach and deliver input, and both children also work
simultaneously. With both live, the heap reaches `free=5160`, `min=4008`, and
`largest=4184` with `oom=0`. The complete HID++ eQuad keyboard connection child
cannot be created even when it is the receiver's only paired child and adds the
run's only OOM count.

After that allocation attempt, later M705, ordinary keyboard, and direct HID++
phases still work. Equivalent direct-device attached values return to 39,392 B
free, and removal values return to 60,464 or 60,736 B free. The result is a
bounded capacity limit, not cumulative heap loss or a receiver-lifecycle
problem.

Intermediate hardware comparisons used the same device graphs:

| `HID_MAX_FIELDS` / `HID_MAX_USAGES` | M705 + ordinary keyboard | Complete HID++ eQuad keyboard |
| --- | --- | --- |
| `256 / 675` | second child does not fit; one new OOM | child does not fit; one new OOM |
| `64 / 675` | passed; `free=5160`, `min=4008`, `largest=4184`, `oom=0` | child does not fit; one new OOM |
| `32 / 675` | passed; `free=8256`, `min=7232`, `oom=0` | child does not fit; one new OOM |
| `8 / 675` | passed; `free=10952`, `min=9912`, `oom=0` | child does not fit; one new OOM |
| `8 / 256` | passed; `free=10968`, `min=9936`, `oom=0` | passed; `free=4480`, `min=2744`, `largest=2624`, `oom=0` |

The earlier two-OOM capture used the upstream-sized 256-field table; it did not
measure the retained 64-field policy. The retained configuration was then
retested through the complete combined automatic sequence.

Reducing only the report field table is insufficient for the complete eQuad
keyboard: it still does not fit at `8/675`. It fits at `8/256`, after which
child removal returns free heap to 42,512 B and full receiver removal returns
it to 60,400 B with `oom=0`. Its Consumer descriptor declares usages 1 through
767; the temporary `8/256` policy retains only selectors 1 through 256.
Consequently that run verifies child creation, keyboard input, and lifecycle
cleanup, while Consumer selector mappings 257 through 767 remain outside the
temporary checkpoint. The retained compatibility policy remains `64/675`.

This boundary may also affect other devices with wide usage arrays, many
report fields, several simultaneous virtual children, or several large
composite HID interfaces. Both total free heap and the largest contiguous
block matter during parsing. A larger-RAM target must be measured with the
same descriptors rather than assumed to fit.

### Practical exact-class stage

Selecting the exact upstream M560, T650, K400, and K750 child entries changes
only host text in the current build:

```text
host text/data/bss            536776 / 788 / 245160 B
host __bss_end__              0x2003fdd0
host main-bank headroom       560 B to 0x20040000
emulator text/data/bss         73480 / 0 / 254964 B
hardware verdict              passed 2026-07-26; exact classes and cleanup
```

Relative to the full upstream DJ/HID++ checkpoint above, this is 1,568 bytes
of host text with unchanged static `.data`, `.bss`, and main-bank headroom.
The new cost is otherwise dynamic and device-specific: M560 and T650 create
delayed class input devices, T650 initializes two MT slots in the fixture,
K400 retains its standard composite child, and K750 registers one reduced
power-supply object and queue.

The automatic fixture runs those four large children sequentially in fresh
receiver generations. It therefore measures each exact class without turning
the result into a new simultaneous-live-graph requirement. Acceptance still
requires the earlier complete-eQuad phase to add exactly the already measured
single OOM at `64/675`, all later exact classes and final direct regression to
complete, and equivalent child/receiver removal plateaus to recover without
cumulative loss.

The hardware run met that acceptance:

```text
M560 live / child removed      27008 / 40832 B free, identical twice
T650 live / child removed      25184 / 40832 B free, identical twice
K400 live / child removed      16928 / 40832 B free
K750 live / immediate remove   18536 / 40576 B free
final direct attached          39408 B free, 39248 B largest, 5 blocks
minimum heap                   3640 B; largest block then 3552 B
OOM count                      exactly 1, at complete-eQuad capacity probe
minimum stack watermarks       tuh=265, keyd=658, async=389, work=117,
                               timer=348, lifecycle=158, report=841 words
```

The K750 removal snapshot precedes the UI's asynchronous terminal queue
cleanup, so its immediate value is 256 bytes below the other child-removal
plateau. The following final direct profile returns exactly to the earlier
direct attached `free/largest/blocks` tuple, establishing that the difference
is transient rather than cumulative retention.

### UC-Logic tablet stage

Linking the complete pinned-upstream UC-Logic implementation for the selected
Huion and Deco paths produces:

```text
host text/data/bss             549776 / 788 / 245208 B
host __bss_end__               0x2003fe00
host main-bank headroom        512 B to 0x20040000
emulator text/data/bss          59720 / 0 / 254432 B
hardware verdict               passed 2026-07-26
```

Relative to the hardware-passed M560/T650/K400/K750 checkpoint, this candidate
adds 12,952 bytes of host text, no data, and 48 bytes of BSS. The static cost
does not describe the live tablet graph: Huion generates Pen, Pad, Touch Strip,
and Dial reports from string 200, while Deco creates three HID interfaces and
Pen, Pad, and frame-Mouse input nodes after its OUT/string probe.

The hardware matrix completed every generic, H640P, Kamvas 13, and Deco 01 V2
phase with one expected ignored Deco interface and `oom=0`. The minimum live
tablet heaps were `39720` B for H640P, `39680` B for Kamvas 13, and `47136` B
for Deco. Removal converged to equivalent `60736`/`60752` B plateaus. The
lifecycle stack remained nonzero at a 92-word minimum.

The lifecycle stack is the narrow static-runtime boundary.
`uclogic_params_init()` has an approximately 644-byte optimized frame and
`usb_string()` uses a 256-byte local descriptor buffer inside the existing
512-word lifecycle task.

#### Modern UGEE-v2 expansion stage

Selecting Deco L/LW and Deco Pro S/SW/MW and enabling their shared generic HID
battery boundary produces:

```text
host text/data/bss             551888 / 788 / 245208 B
host __bss_end__               0x2003fe00
host main-bank headroom        512 B to 0x20040000
emulator text/data/bss          61136 /   0 / 254440 B
hardware verdict               passed 2026-07-27
```

Relative to the hardware-passed first UC-Logic stage, the extension adds 1,904
bytes of text before the final control-parser correction and 2,112 bytes in the
current build, with no static data or BSS increase. Generic battery support
adds runtime power-supply/devres/workqueue allocations for wireless profiles.

The expanded run completed every marker with no host `ERR`. Repeated
alert-attached snapshots stayed within `48376..48384` bytes free, alert
removals returned to `60736..60744`, minimum-ever free heap was `39672`, and
`oom=0`. Minimum stack watermarks were TinyUSB `265`, KeyD `658`, async `389`,
work `246`, timer `304`, lifecycle `86`, and report `862` words. The final
`uint32_t` timestamp representation retains its four-byte layout. The Apple
battery test covers the immediate sparse GET/input path; the later
firmware-selected 90-second repeat interval is not an elapsed-time claim of
this automatic sequence.

#### Deco 01 original / Parblo A610 Pro verified stage

Selecting the two existing pinned UC-Logic table rows on top of the external
Intuos checkpoint produces:

```text
host text/data/bss             605256 / 788 / 245408 B
host __bss_end__               0x2003fee8
host main-bank headroom        280 B to 0x20040000
scratch X                      788 B (0x20040000..0x20040314)
scratch X / core-1 gap         1260 B to 0x20040800
delta from Intuos stage        +32 text / +0 data / +0 bss
host UF2 SHA-256               a9ba49cf53d0ce0ba44679d85b11bca301809094cb7705c1721f8e93a12cbd71
emulator text/data/bss         60912 / 0 / 254436 B
emulator UF2 SHA-256           373df86d428d9c528dbf642b9a2931d38b5c0f8e584f218695d7f14392b65eca
hardware verdict               passed 2026-07-31; complete focused sequence
```

The static delta is only the two selected match entries. Their pinned v1 and
UGEE-v2 parameter, replacement-descriptor, request, input, and cleanup code was
already linked. Runtime descriptor and input graphs still allocate from the
heap; neither profile adds battery state or another mutable static object. The
common UC-Logic probe initializes its embedded in-range timer, but neither
selected profile schedules it or activates delayed work.

One full and one same-PID reconnect generation per profile published and
removed ten target inputs. All 22 heap snapshots reported `oom=0`; minimum-ever
free heap was 46,688 B. Deco removal repeated at 60,752 B and Parblo removal at
60,736 B without cumulative retention. Minimum free task watermarks were
TinyUSB 265, KeyD 658, async 389, work 346, timer 348, lifecycle 85, and report
854 words.

#### UC-Logic failed-probe cleanup stage

The production cleanup, after removing the temporary fault hook, builds as:

```text
host text/data/bss             605272 / 788 / 245408 B
host __bss_end__               0x2003fee8
host main-bank headroom        280 B to 0x20040000
delta from Deco/Parblo stage   +16 text / +0 data / +0 bss
host UF2 SHA-256               e7d2e2c9469efa27ef7101c14cd070c0d75b00e39df0101fc250c3cfdd64f23c
hardware verdict               cleanup branch passed with temporary fault;
                               exact clean UF2 not flashed unchanged
```

The reclaimed object is runtime heap memory, so unchanged BSS cannot establish
the behavior. Three injected `hid_hw_start()` failures after combined-
descriptor generation each returned to the same 60,752-byte removal plateau;
a following normal generation published and removed Mouse, Pen, and Pad and
returned there again. All 25 snapshots reported `oom=0`, and minimum-ever free
heap was 46,664 B. Minimum task watermarks were TinyUSB 265, KeyD 658, async
389, work 346, timer 348, lifecycle 105, and report 862 words. The temporary
fault hook is absent from the production source. The same-label `hid_parse()`
failure remains a static ownership result rather than a hardware verdict.

#### Wired Wacom CTL-472 verified stage

Linking the complete pinned Wacom implementation with only CTL-472
`056a:037a` active produces:

```text
host text/data/bss             585656 / 788 / 245260 B
host __bss_end__               0x2003fe44
host main-bank headroom        444 B to 0x20040000
scratch X                      788 B (0x20040000..0x20040314)
scratch X / core-1 gap         1260 B to 0x20040800
host UF2 SHA-256               bdf6ab6ce3bfbe1ed81abb4dcfb9183030d597f92e4e9d301bae8f474a436737
emulator text/data/bss          59804 /   0 / 254444 B
emulator UF2 SHA-256           614401701850d5bbea0dde53ce005de9d0a76aacddf3bda5112a08c4b2c9048e
hardware verdict               passed 2026-07-29
```

The static image includes Wacom's full pinned parser/source plus small common
devres, kfifo, and delayed-work glue. No task or queue was added. Live CTL-472
cost includes the HID/parser graph, one Pen input, Wacom state, a 128-byte
record FIFO allocation, shared-data ownership, and ordinary devres metadata.

The exact no-PIO pair completed all 36 Wacom add/remove generations with
`oom=0`. After the first warm-up variant, repeated Wacom removals returned to
`free/largest/blocks=60728/36192/7`; repeated live Wacom snapshots were
`18400/18176/6`. Minimum-ever free heap was 7,848 B. Minimum stack watermarks
were TinyUSB 265, KeyD 658, async 389, work 205, timer 348, lifecycle 222, and
report 870 words. The test included disconnect before the delayed deadline,
disconnect while the initialization callback was blocked in its mode GET,
recovery, and repeated reconnect, so no cumulative runtime leak appeared in
that exact sequence.

A separate shortened CTL-only emulator builds as `59820 / 0 / 254444 B` with UF2
SHA-256
`2d95713f875eb6115f1e862d2a48e8c9296c34d714e3e58128647f47988ecaff`,
but it has not run on hardware and does not inherit the 32-reconnect verdict.
Generic promotion-window cancellation, simultaneous cancelers, callback
self-requeue, workqueue destruction, and tick wrap remain static audit
results, not measured memory/lifetime coverage.

The Linux-shaped managed-input/devres and evdev identity correction builds as:

```text
host text/data/bss             585720 / 788 / 245260 B
host UF2 SHA-256               12a8f6826c9148eb03a9fb783e1f558674d067f562bfcec2ad69edcf3dd09790
hardware verdict               passed 2026-07-30; complete Wacom fixture
```

The complete 32-reconnect Wacom artifact produced 36 `056a:037a` adds and
removes, 36 expected ghost-interface warnings, `f1, f2, f3, f4, f10`, no host
`ERR`, and `oom=0` in all 83 heap snapshots. Repeated Wacom removals stabilized
at `free/largest/blocks=60736/36032/7`; minimum-ever heap was 7,856 B and all
task watermarks remained nonzero. The separate Rapoo managed extra-input
fixture has not yet been rerun.

Removing the never-read port-only `input_dev.registered` state and documenting
the reduced compatibility contracts produces:

```text
host text/data/bss             585704 / 788 / 245260 B
host UF2 SHA-256               c2542706419b44a00d1ba8dcaa562b50afc272faab798cab62267fa9c873e1ca
hardware verdict               not run
```

This cleanup removes 16 bytes of text with no data or BSS change. Compilation
does not transfer the preceding hardware verdict to this exact image.

#### Expanded wired Wacom verified stage

Selecting the wired CTL-472 `056a:037a`, CTL-672 `056a:037b`, PTK-450
`056a:0029`, CTH-470 `056a:00de`, and PTH-650 `056a:0027` paths, including
Pen/Pad/Touch, LED, battery-report, arbitration, and work-disconnect handling,
produces the exact hardware-tested pair:

```text
host text/data/bss             592312 / 788 / 245264 B
host __bss_end__               0x2003fe58
host main-bank headroom        424 B to 0x20040000
scratch X                      788 B (0x20040000..0x20040314)
scratch X / core-1 gap         1260 B to 0x20040800
host UF2 SHA-256               4efcd10843585da2ef41265be760bfba5b405e156b0e095c2a98d45d2ce604e5
emulator text/data/bss         65820 / 0 / 254456 B
emulator UF2 SHA-256           037c8fe0f947e1cc4a38815539f6c2af33827a0b8ef58363fc8cdaf00408cb1b
hardware verdict               passed 2026-07-30 for the qualified scope below
```

Relative to the unflashed CTL-only cleanup immediately above, the expanded
host has 6,608 more bytes of text, unchanged data, and four more bytes of BSS.
The important additional cost remains dynamic: PTK owns Pen and Pad inputs;
CTH and PTH each own Pen, Finger, and Pad inputs; PTH also reaches the reduced
power-supply object, length-one event queue, and changed work.

The exact run completed 23 physical attachments and balanced all 46 Wacom
input-node additions with 46 removals. Its 115 heap snapshots all reported
`oom=0`. Snapshot-time free heap ranged from 18,232 B to 62,360 B, while the
minimum-ever counter reached 7,568 B. The largest free block ranged from
18,136 B to 58,512 B, and the allocator reported 4 through 12 free blocks.
CTH padding-only reports reach two upstream zero-size allocations per attach.
Returning Linux's `ZERO_SIZE_PTR` sentinel, rather than passing those requests
to FreeRTOS, removed the former two false malloc-failure increments without
allocating memory.
Stable terminal tuples were:

```text
CTL-472/CTL-672 free/largest/blocks  60752 / 36712 / 6
PTK-450          free/largest/blocks 60752 / 22576 / 8
CTH-470          free/largest/blocks 60752 / 21704 / 9
PTH-650          free/largest/blocks 60752 / 26992 / 8
```

The first CTL teardown had one extra block without a free-byte difference.
The immediate PTH status-disconnect snapshot was
`free/largest/blocks=60496/26992/9`, exactly one 256-byte allocation below the
normal PTH plateau. Before the next PTH live snapshot, that allocation was
reclaimed; all later terminal PTH snapshots returned to
`60752/26992/8`. This proves that the measured difference was transient rather
than a cumulative heap leak.

Minimum remaining stack watermarks were TinyUSB 265, KeyD 658, async 389,
work 198, timer 348, lifecycle 218, and report 854 words. Nine expected CTL
ghost-interface warnings and nine expected CTH/PTH bounded-batch warnings were
the only warnings. The bounded-batch result covers the fixture's two-contact
touch frames, not a maximum-contact frame.

The PTH phase completed its battery-report transfers, immediate disconnect,
running LED-work disconnect, and recovery, but the production host emitted no
`POWER` snapshot records. The 256-byte transient and subsequent recovery are
heap-lifetime evidence only; they do not verify exact detached
`ADDED`/`CHANGED`/`REMOVED` values or ordering, or identify the consumer that
deleted the queue. Do not use this memory result as a value-level
power-snapshot verdict.

#### Wacom AES and USB receiver verified stage

Adding Yoga 260 AES `056a:5048`, Wacom USB receiver `056a:0084`, dynamic
PTH-650 child profile `056a:0027`, receiver sibling lookup/rebind, and the
HID-ordering lifetime correction produces the latest exact pair:

```text
host text/data/bss             594360 / 788 / 245264 B
host __bss_end__               0x2003fe58
host main-bank headroom        424 B to 0x20040000
scratch X                      788 B (0x20040000..0x20040314)
scratch X / core-1 gap         1260 B to 0x20040800
host UF2 SHA-256               8e07cbaba2c2822ef3e93e68aa318f29e9434976e275b2963bf25850dfda7cb8
emulator text/data/bss         68504 / 0 / 254480 B
emulator UF2 SHA-256           8dd6dd644047ee0fcdf4e3616c092df4019798338f618da9086d92f5f5c7ac48
hardware verdict               passed 2026-07-30 for the qualified scope below
```

Relative to the expanded wired host, this adds 2,048 B of text with unchanged
data, BSS, and main-bank headroom. The important cost is dynamic: each AES
attachment owns separate Pen and Finger HID/input graphs, while a paired
receiver owns monitor state plus dynamic Pen, Pad, and Finger graphs. Receiver
rebind frees each input/devres graph and the previous connect-lifetime HID
field-ordering graph before rebuilding the selected child profile.

The run completed four AES and four physical receiver attachments. Eight AES
and twelve dynamic receiver input additions had matching removals. All 47 heap
snapshots reported `oom=0`; snapshot-time free heap ranged from 9,992 B to
62,360 B and the minimum-ever counter reached 9,464 B. Stable AES and receiver
terminal physical-removal snapshots returned to 60,496 or 60,752 B without
cumulative retention; logical unpair correctly retained the receiver monitor
allocation. The smallest live receiver graph snapshot was 26,104 B free, with
a largest free block of 25,984 B.

Minimum remaining stack watermarks were TinyUSB 265, KeyD 658, async 389,
work 202, timer 332, lifecycle 182, and report 854 words. Four
`EVDEV_BATCH_CAP` warnings and the unsupported tablet event messages belong to
the downstream Linux-to-KeyD boundary; there was no `EVDEV_INPUT_DROP`, host
`ERR`, or transport failure.

This memory result covers AES delayed-work cancellation, idle-proximity timer
cancellation, receiver pair/unpair/re-pair, pending original sibling
initialization cancellation, held rebind/teardown controls, physical
disconnect, and recovery. It does not wait for real 30-minute AES expiry or
prove exact power-snapshot values/order. A device-side report acknowledgement
also cannot prove that every PTH/receiver pending battery callback had already
been queued. The selected receiver child `0027` is a protocol-equivalent active
profile; captured child `033b` is selected by the later Intuos stage but was
not exercised by this receiver artifact.

#### External wired Wacom Intuos verified stage

Selecting eleven external wired Intuos/Intuos Pro/Intuos 2 IDs and enabling
duplicate VARIABLE Feature usage compaction for the three Pro IDs produces:

```text
host text/data/bss             605224 / 788 / 245408 B
host __bss_end__               0x2003fee8
host main-bank headroom        280 B to 0x20040000
scratch X                      788 B (0x20040000..0x20040314)
scratch X / core-1 gap         1260 B to 0x20040800
host UF2 SHA-256               1bd3124acf0d3058bf798df8b59cc796aebc43cca09a36cf39ea4a96b8f94fed
emulator text/data/bss         66080 / 0 / 254480 B
emulator UF2 SHA-256           93f034b64d9eec4537477acfb9763d3cbe0590017338f5a561619b74d5da2fac
hardware verdict               passed 2026-07-31 for the qualified scope below
```

The port quirk does not reduce `HID_MAX_USAGES`, report count, cached
value/new-value storage, wire length, raw/control payloads, or request return
semantics. It stores only the explicitly declared usage callback for
FEATURE+VARIABLE fields whose same vendor usage Linux would repeat to every
value. On the captured `0317` PTH-851 family descriptor this avoids 626
duplicate `hid_usage`/priority pairs, about 20 KiB of persistent heap; the
emulator reuses that capture for `0314/0315`, so the count is not a claim about
separately captured retail descriptors. Selection is limited to exact Wacom
`056a:0084/0314/0315/0317/5048`; other HID drivers retain normal Linux
expansion because their mapping callbacks may depend on every usage index.
The selected Pro call graph does not request the large compressed B1/B2 vendor
reports;
its mode, LED, and proximity operations use separate small reports, and the
normal feature-mapping pass iterates only `field->maxusage`.

Relative to the preceding retained-policy host build, this exact-ID stage adds
1,728 bytes of text and no data or BSS; `__bss_end__` and the 280-byte main-bank
headroom are unchanged. The optimization targets persistent per-device heap
instead of trading away parser compatibility globally.

The distinguishing pre-compaction run left 10,688 B after Pro Pen and 10,560 B
after Pad, then failed Finger probe with `oom=2` and a 976-byte minimum-ever
counter. The verified image left 30,696 B after Pen, 30,568 B after Pad, and
17,320 B after Finger. All eleven profiles completed 29 balanced input
lifetimes and terminal `f15, f10`; all 63 heap snapshots reported `oom=0`.
Minimum-ever free heap was 12,832 B, and every terminal tablet removal returned
to 60,736 B without cumulative retention.

Minimum remaining task watermarks were TinyUSB 265, KeyD 658, async 384,
work 207, timer 348, lifecycle 212, and report 859 words. Seven
`EVDEV_BATCH_CAP` warnings and unsupported tablet event messages remain at the
downstream KeyD boundary; there was no host `ERR` or input-drop marker. The
family captures are not byte-exact retail descriptors for every selected PID.
Representative battery reports are injected only for `0302`, `0314`, and
`033b`. Only emulator-side interrupt-IN completion is established; host
parser/work enqueue and exact detached power-snapshot values remain
unobserved.

The optional linked Stadia experiment added `hid-google-stadiaff.c` and
`ff-memless.c`, with their active event-lock scopes mapped to firmware
priority-inheritance mutexes. It builds as:

```text
text/data/bss                 518204 / 788 / 245088 B
__bss_end__                   0x2003fd68
main-bank headroom            664 B to 0x20040000
scratch X                     788 B (0x20040000..0x20040314)
scratch X / core-1 gap        1260 B to 0x20040800
candidate UF2 SHA-256         5491f7e19c2230d35570a89191ca8cf4a65535c430275702ef79f776917f529b
hardware verdict              exact base image not flashed; integration tested below
```

The same retained Stadia/ff-memless source was hardware-tested before deferral
with a temporary targeted trigger and deterministic workqueue teardown window,
not by flashing the build-only candidate above unchanged:

```text
text/data/bss                 520188 / 788 / 245100 B
__bss_end__                   0x2003fd7c
main-bank headroom            644 B to 0x20040000
temporary test UF2 SHA-256    8b188a595689102872e65ff7529ad3bbef3cb21b1d026e3378498688ba04bfbf
hardware verdict              two full cycles separated by reconnect passed
post-remove free heap         60816 B in both cycles; oom=0
```

The temporary trigger and eight-second fault injection are not part of the
current checkpoint. They proved the retained driver, timer, workqueue cancel,
transport, and reconnect paths while making the teardown race observable. The
targeted run covered upload/play, automatic timer stop, and same-ID replay; it
did not issue the explicit stop or erase client calls.

Relative to the then-verified 514,948-byte-text image, the build-only linked
pair added 3,256 B of text and 48 B of BSS, reducing main-bank headroom by 48 B.
Those values belong only to the optional linked build; the current unlinked
build is the checkpoint above. The current architecture still has one ordinary
32-entry TinyUSB event queue with no spill state, a transient
long-configuration buffer rather than a permanent 4 KiB array, metadata-only
physical async slots, and stack-owned admission waiters. Exact structure/block
sizes below must be remeasured whenever those objects change.

The Linux input core's active `event_lock` sections use one equivalent 96-byte
heap_4 mutex block per live `input_dev`. Its embedded handle changes the
`input_dev` allocation from a 560-byte to a 568-byte heap_4 block, making the
total delta 104 B per live input. This is dynamic device cost, not static
`.bss`: it serializes CORE1 report parsing with CORE0 KeyD LED/FF injection and
is released with the input device. A global mutex would save RAM only when
several input devices coexist, but would depart further from Linux and couple
otherwise independent devices.

TinyUSB's permanent enumeration scratch remains 512 B. A full configuration
descriptor whose validated `wTotalLength` is 513..4096 bytes adds one exact-size
FreeRTOS heap allocation only while TinyUSB fetches, retries, and synchronously
parses that descriptor. With heap_4's 8-byte header/alignment on RP2040, the
largest 4096-byte request occupies about 4104 B; a 513-byte request occupies
528 B. The existing 512-byte static scratch is still present, so this is a
transient addition to the ordinary baseline, not a replacement for that static
buffer. It is freed before class `set_config` and Linux HID probe allocations,
or after exact retirement of the matching EP0 owner on error, timeout, or
removal. Retrying reuses the same buffer rather than allocating again. The
control-completion callback only records pending state; allocation happens
after callback unwind in the TinyUSB host-owner service. Successful parse frees
immediately inside TinyUSB's internal enum continuation; terminal failure frees
after the PIO abort result or its exact cancel-time FIFO-prefix fence.

Linux normally allocates `value` and `new_value` for every selector in a HID
field. This port preserves that layout except for INPUT ARRAY fields, whose
runtime values are the physical report slots. A six-slot Consumer array with
675 selectors therefore occupies 21,748 B instead of 27,100 B while retaining
all 675 selector usages and their priorities.

Upstream also embeds a 256-pointer `report_id_hash` in each of the three report
enums. RP2040 keeps the same complete 8-bit report-ID domain but resolves IDs
through the report enum's existing sparse `report_list`; typical devices have
only one to three reports of each type. On the ARM32 ABI this changes
`sizeof(struct hid_report_enum)` from 1,036 B to 12 B and
`sizeof(struct hid_device)` from 3,712 B to 640 B, saving exactly 3,072 B per
attached HID interface without capping report IDs or adding allocations. The
later removal of the unused embedded sysfs registry reduces the current object
again to 608 B; upstream publication calls remain visible but allocate nothing
until a real firmware attribute proxy exists.

The compatibility layer also avoids constructing the 2,324-byte Linux uevent
environment for plain lifecycle notifications. No firmware consumer receives
that environment; only the device lifecycle counters are retained.

## Layers

TinyUSB host state and Pico-PIO-USB state are separate layers.

```text
TinyUSB HID host state
-> TinyUSB USB host core state
-> Pico-PIO-USB host controller state
-> Pico-PIO-USB time-critical RAM code
-> pio_usb_ep_pool[]
-> PIO/DMA/D+/D-
```

TinyUSB stores logical USB state:

- device slots
- hub slots
- HID interface slots
- host event queues
- descriptor/control buffers
- HID IN/OUT transfer buffers

Pico-PIO-USB stores low-level transport state needed to drive D+/D- from PIO:

- root ports
- low-level USB device records
- low-level endpoint slots
- pre-encoded USB packet buffers
- PIO/transaction state
- RAM-resident timing-critical code

## Why Host Costs So Much RAM

The biggest surprise is not `pio_usb_ep_pool[]`. That pool is visible and
configurable, but the larger cost is Pico-PIO-USB code placed in RAM.

Pico-PIO-USB is effectively a software USB host controller/PHY on top of PIO.
The hot transaction path must hit tight USB timing windows. RP2040 flash runs
through QSPI XIP cache, so cache misses can stall unpredictably. Pico-PIO-USB
therefore marks many functions with `__not_in_flash_func`,
`__no_inline_not_in_flash_func`, or `.time_critical.*`; the linker puts those
functions into `.data` so they execute from SRAM.

The current candidate map still shows roughly this much RAM-resident
Pico-PIO-USB / PIO-HCD code:

```text
~12.2 KiB Pico-PIO-USB / hcd_pio_usb time-critical code in .data
```

Large entries from that candidate map:

```text
pio_usb_ll_encode_tx_data             4180 B
initialize_host_programs              1444 B
pio_usb_bus_receive_packet_and_handshake 704 B
handle_endpoint_irq                    692 B
pio_usb_host_frame                     468 B
crc16_tbl                              512 B
connection_check                       508 B
configure_fullspeed_host               268 B
configure_lowspeed_host                268 B
```

This memory is expensive but not safely removable by configuration. Moving any
of these functions/tables to flash is a timing experiment, not a safe cleanup.

## Current Static RAM Consumers

Approximate current host-related RAM costs after `PIO_USB_EP_POOL_CNT` was
reduced to `8`:

```text
~12.2 KiB  Pico-PIO-USB / hcd_pio_usb time-critical code in .data
~1.5 KiB   pio_usb_ep_pool[8]
~0.3 KiB   pio_usb_device[4]
~0.2 KiB   pio_port[1]
~0.1 KiB   pio_usb_root_port[1]
~2.0 KiB   TinyUSB host/HID/hub static state
```

Important entries in the same candidate map:

```text
pio_usb_ep_pool[8]          1504 B
usbhid_report_rx_slots       208 B
_hidh_epbuf                   32 B
_usbh_epbuf                  520 B
_usbh_devices               1720 B
_usbh_q handle                 4 B
host event xQueue block      736 B runtime heap (32 x 20-byte payload)
```

Non-host RAM that is also visible in the map:

```text
stdio_tusb_cdc pending_buf       2048 B
TinyUSB device queue buffer       768 B
MSC device endpoint buffer        512 B
CDC device state/buffers         ~1.2 KiB
log.c static format buffer       2048 B
```

Those are not the host regression root cause, but they are real RAM reserves if
heap pressure becomes more important than logging/CDC/MSC burst tolerance.

## `PIO_USB_EP_POOL_CNT`

`PIO_USB_EP_POOL_CNT` controls how many low-level Pico-PIO-USB endpoint slots
can be open at once.

Default upstream Pico-PIO-USB value:

```c
#define PIO_USB_EP_POOL_CNT 32
```

Current port value:

```c
#define PIO_USB_EP_POOL_CNT 8
```

One `endpoint_t` is about 188 bytes. Most of it is:

```c
uint8_t buffer[(64 + 4) * 2 * 7 / 6 + 2];
```

That buffer holds a pre-encoded USB packet stream for PIO TX:

- SYNC
- PID
- payload up to 64 bytes
- CRC16
- bit-stuffing worst case
- EOP

Cost:

```text
32 endpoint slots * ~188 B = ~6016 B
 8 endpoint slots * ~188 B = ~1504 B
saved                         ~4512 B
```

User-visible meaning: this is not the number of keys, reports, or HID usages.
It is the number of low-level USB transport endpoints that can be opened for
devices plugged into the PIO host port.

For a direct ErgoType-to-ErgoType host test without a hub, `8` should be enough:

```text
EP0 control
keyboard HID IN
mouse HID IN
WebHID IN
spare slots
```

If this is too low, enumeration or endpoint opening fails when
`pio_usb_host_endpoint_open()` cannot find a free slot. User-visible failures:

- keyboard works, but mouse does not
- keyboard/mouse works, but WebHID/config channel does not
- the whole device stays absent if the missing endpoint is required during enum

## TinyUSB Host Config

For one peer ErgoType in the current NKRO profile:

```text
one physical USB device
three HID interfaces:
  keyboard/consumer
  mouse
  WebHID
```

Consumer Control is a report ID inside the keyboard HID interface. It is not a
separate HID interface.

Current wider smoke-test config:

```c
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          4
#define CFG_TUH_API_EDPT_XFER       1
#define CFG_TUH_HID                 4
#define CFG_TUH_ENUMERATION_BUFSIZE 512
#define USBH_PORT_ENUMERATION_MAX_BUFSIZE 4096
#define CFG_TUH_HID_EPIN_BUFSIZE    1
#define CFG_TUH_HID_EPOUT_BUFSIZE   1
```

Direct single-peer config:

```c
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_HID                 3
#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HID_EPIN_BUFSIZE    1
#define CFG_TUH_HID_EPOUT_BUFSIZE   1
```

Tradeoffs:

- `CFG_TUH_HUB=0`: saves roughly 100-200 B, but external USB hubs do not work.
- `CFG_TUH_DEVICE_MAX=1`: saves roughly 250-350 B versus 4, but only one downstream physical USB device is supported.
- `CFG_TUH_HID=3`: saves roughly 100-150 B versus 4 with current buffers. Enough for ErgoType NKRO. Use `2` only for boot keyboard+mouse. Use more for composite devices with more HID interfaces.
- `CFG_TUH_ENUMERATION_BUFSIZE=256`: would save 256 B versus the current 512.
  A validated full configuration above the permanent scratch and no larger
  than `USBH_PORT_ENUMERATION_MAX_BUFSIZE` still uses the exact transient
  host-owner path. The build-local TinyUSB HID class separately skips every
  duplicate report-descriptor prefetch; task-side `usbhid_parse()` fetches the
  class-declared report size once through async EP0, up to Linux's 4 KiB limit.
- `CFG_TUH_HID_EPIN_BUFSIZE=1`: direct interrupt IN uses upstream's
  per-interface `inbuf` through the endpoint API, so TinyUSB's class buffer is
  an unused placeholder. Task context sizes that backing for the parsed INPUT
  report, with a 64-byte minimum and PIO-safe final-packet rounding.
- `CFG_TUH_API_EDPT_XFER=1`: stores an exact callback and request serial per
  endpoint. With five host slots and 16 endpoint numbers this costs 1,280 B,
  but exposes the real interrupt-transfer result and actual length.
- `CFG_TUH_HID_EPOUT_BUFSIZE=1`: saves 240 B versus 64 when
  `CFG_TUH_HID=4`. Interrupt OUT remains supported because this port submits
  the request-owned wire buffer directly instead of using TinyUSB's class
  staging buffer. Control SET_REPORT is separate.

`CFG_TUH_MEM_SECTION` places TinyUSB's DMA-visible host transfer metadata and
the port-owned interrupt-IN lifecycle slots in scratch X. At the earlier
direct-IN checkpoint they occupied 708 B and ended 1,340 B below the real
core-1 stack. The current stage occupies 788 B and ends 1,260 B below
that stack, as recorded above. The endpoint callback table remains in main SRAM. Per-interface interrupt-IN
payload backing is ordinary PIO-visible SRAM from heap_4: normally a 72-byte
block for 64 bytes, allocated once at start and freed after the detach fence.

The callback transport pool uses four 16-byte probe-identity slots instead of
four inline 512-byte descriptor buffers. Its payload shrinks by 2,044 B and the
aligned heap_4 allocation by 2,040 B. Lifecycle serializes probe, so task-side
`usbhid_parse()` holds at most one exact descriptor buffer across all devices;
a maximum-size descriptor consumes about 4 KiB transiently and is released on
parser return after completion or fenced cancellation.

That lifecycle report-descriptor allocation is not the same as the enumeration
configuration-descriptor allocation above. A long configuration is released
immediately after TinyUSB's synchronous class-open scan, before class
set-config completes and before lifecycle starts Linux `usbhid_parse()`.
Normal scheduling therefore does not overlap their two 4 KiB maxima. Hardware
tests must still check the long-configuration peak, repeated add/remove plateau,
and terminal paths independently; a normal descriptor at or below 512 B never
exercises the transient enum allocation.

Device-reset recovery reserves one additional 88-byte async slot which normal
requests cannot consume. Ten metadata-only slots request 880 B (an 888-byte
heap_4 block). Device/string pre-probe uses the aligned 256-byte scratch inside
the 2,564-byte lifecycle transport pool (a 2,576-byte block) and reaches TinyUSB
through the same generic control lane as other synchronous USB calls. The
dedicated recovery slot remains unavailable to normal traffic; moving scratch
ownership removes 72 B of persistent heap. Replacing the transport's global
critical regions adds one separate persistent 96-byte mutex block; it is
startup-only and does not churn during attach/report traffic.
The firmware workqueue uses the same memory-neutral exchange in its own domain:
its former 96-byte one-entry wake queue becomes a 96-byte mutex block, while a
task handle and stack-waiter head add 8 B of `.bss`. Direct notifications wake
the worker, and synchronous flush/cancel/destroy waiters add only caller-stack
state rather than persistent heap objects.
The timer bridge likewise exchanges its 96-byte wake queue for one 96-byte
mutex. Its task handle and stack-waiter head add 8 B of `.bss`; synchronous
timer deletion keeps waiter state on the caller stack and adds no heap churn.

At that same earlier direct-IN checkpoint the report task had no
interrupt-input queue. Its four fixed 32-byte completion slots occupied 128 B
in scratch X and retained the exact `hid`, `inbuf`, device generation, open
revision, and raw giveback until task-side parsing finished. Those 32/128-byte
figures are historical. The current candidate uses four 52-byte slots (208 B
total); each embeds its own reconciliation bit and generic CLEAR_HALT admission
node, with no separate reconciliation table or capacity-edge latch.
Removing the former four-entry input queue returns a 176-byte heap_4 block and
one 4-byte `.bss` handle; growing the old 20-byte slots adds 48 B to scratch X.
Net live RAM occupancy falls by 132 B and one persistent heap allocation. The
ordinary-control queue remains a 232-byte block (five 28-byte events plus its
queue object). Probe-owned GET completion bypasses that queue through a per-
interface pointer into the existing request buffer. Removing the still earlier
global handoff saved 36 B of `.bss`, and shrinking the control event saved 24 B
of persistent queue heap. Its temporary GET header is 16 B instead of 12 B and
is freed after parse/cancel.
Lifecycle state is separately durable in flags/cache slots; its sole owner now
uses indexed task notification only as a wake edge. Removing that one-entry
event queue saves another 96-byte persistent heap_4 block without growing any
TCB because the second notification index was already configured.
Removing selected-protocol state keeps persistent allocation unchanged: the
probe slot and per-device transport object retain their aligned sizes, while
the lifecycle-local probe token shrinks from 16 B to 12 B.

Queued SET snapshots and logical request nodes are freed after completion or
fenced cancellation. Repeated equal-size traffic must return to the same total
free-heap plateau. If total `free` returns but `largest` remains lower, mixed
allocation sizes have left temporary holes; that is fragmentation, not by
itself a leak.

These are reasonable low-risk reductions for direct one-device testing, but
they do not recover the full 13-14 KiB needed to keep a 232 KiB heap.

## Pico-PIO-USB Config

Current host-oriented config:

```c
#define PIO_USB_EP_POOL_CNT      8
#define PIO_USB_DEV_EP_CNT       16
#define PIO_USB_DEVICE_CNT       4
#define PIO_USB_HUB_PORT_CNT     8
#define PIO_USB_ROOT_PORT_CNT    1
#define PIO_USB_EP_SIZE          64
```

Direct one-port config:

```c
#define PIO_USB_EP_POOL_CNT      8
#define PIO_USB_DEVICE_CNT       1
#define PIO_USB_ROOT_PORT_CNT    1
```

Tradeoffs:

- `PIO_USB_DEVICE_CNT=1`: saves roughly 200-300 B versus 4. Only one low-level USB device record exists, so hub children/multiple devices do not fit.
- `PIO_USB_ROOT_PORT_CNT=1`: saves roughly 50 B versus 2. Only one D+/D- PIO root port exists.
- `PIO_USB_DEV_EP_CNT`: endpoint-id entries inside each `pio_usb_device`; one byte per entry per device. Low memory impact.
- `PIO_USB_HUB_PORT_CNT`: child-device slots inside each `pio_usb_device`; only useful with hub support. Low direct memory impact.
- `PIO_USB_EP_SIZE=64`: do not reduce for normal HID host. Full-speed control and HID paths can legitimately use 64-byte packets. Reducing it saves per endpoint but risks breaking ordinary devices.

## Safe Versus Risky Reductions

Relatively safe under the narrow test scope "one direct ErgoType-like USB
device, no hub":

```text
CFG_TUH_HUB              1 -> 0       ~100-200 B
CFG_TUH_DEVICE_MAX       4 -> 1       ~250-350 B
CFG_TUH_HID              4 -> 3       ~100-150 B
CFG_TUH_ENUMERATION_BUFSIZE 512 -> 256 256 B
CFG_TUH_HID_EPIN_BUFSIZE  64 -> 1    ~250 B, only with direct-IN transport
CFG_TUH_HID_EPOUT_BUFSIZE 64 -> 1    ~250 B
PIO_USB_DEVICE_CNT       4 -> 1       ~200-300 B
PIO_USB_ROOT_PORT_CNT    2 -> 1       ~50 B
```

Already done:

```text
PIO_USB_EP_POOL_CNT      32 -> 8      ~4512 B
```

Do not reduce blindly:

```text
PIO_USB_EP_SIZE          64
HID_MAX_BUFFER_SIZE      16384
port IN backing          max(64, round_up(input size, endpoint packet))
TUD_STACK_SIZE           4096
```

Risky experiments that may save much more RAM:

```text
Move selected Pico-PIO-USB .time_critical functions/tables from RAM to flash.
```

This is the only obvious place with enough bytes to matter for a 232 KiB heap,
but it is not a safe config change. It can break USB timing. It must be tested
on hardware with attach/enumeration/report traffic.

Potential candidates to investigate, not change blindly:

```text
pio_usb_ll_encode_tx_data  ~4.1 KiB
initialize_host_programs   ~1.4 KiB
crc16_tbl                  512 B
configuration/init helpers  several hundred bytes
```

Keep in RAM unless proven otherwise:

```text
IRQ handlers
usb_in_transaction()
usb_out_transaction()
usb_setup_transaction()
pio_usb_host_frame()
PIO/FIFO tight polling paths
handshake/receive paths
```

## Practical Conclusion

The host side eats RAM for three reasons:

1. TinyUSB host adds logical host state and descriptor buffers.
2. Pico-PIO-USB adds low-level endpoint/device/root-port transport state.
3. Pico-PIO-USB puts timing-critical host code into SRAM.

The endpoint pool was the biggest simple static buffer and is already reduced
from 32 to 8. That saved about 4.5 KiB.

The remaining large cost is the RAM-resident time-critical code. Keeping a
232 KiB FreeRTOS heap while enabling this host stack requires either:

- more aggressive, risky Pico-PIO-USB flash/RAM placement experiments, or
- lowering `configTOTAL_HEAP_SIZE` to around 218 KiB, or
- removing/reducing unrelated device-side buffers/features such as CDC log
  buffering, MSC buffering, or TinyUSB device queue depth.

For bring-up, the least risky path is:

```text
keep Pico-PIO-USB timing code unchanged
use narrow one-device/no-hub host config
build with heap around 218 KiB
measure runtime heap and stack watermarks on hardware
then decide whether risky RAM-code experiments are worth it
```
