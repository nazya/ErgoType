# TinyUSB Host Port Contract

This document is the maintenance contract for the build-local TinyUSB host
port. It explains what is changed, why the public TinyUSB API is insufficient
for this firmware, and how to update the pinned TinyUSB snapshot without
silently changing USB lifecycle semantics.

The port is intentional. It is not a general TinyUSB fork and it does not make
Pico-PIO-USB part of the Linux HID port. Its boundary is the minimum host-core,
HID-class, and PIO-HCD behavior needed to place Linux-shaped HID parsing and
driver lifecycle in FreeRTOS task context instead of TinyUSB callbacks.

## Architectural Boundary

Linux HID expects USB core to provide durable device/interface objects,
synchronous-looking control and interrupt requests, exact completion status,
and lifecycle barriers such as `usb_kill_urb()`. TinyUSB instead owns compact
private host state and invokes class/application callbacks from its one host
task. Those callbacks cannot wait for work whose completion also requires that
same host task.

The firmware therefore divides ownership as follows:

```text
PIO HCD completion
  -> TinyUSB host task: sole owner of TinyUSB internals and wire submission
  -> fixed callback publication: metadata and wake edge only
  -> async/report/lifecycle task: blocking policy and Linux-shaped continuation
  -> Linux HID parser and drivers
  -> evdev / KeyD
```

The current task-side glue lives primarily in `usb_host/usbhid.c`
(lifecycle), `usb_host/hid_async.c`, `usb_host/usbhid_report.c`, and
`usb_host/hid_transport_sync.c`; `usb_host/tinyusb_hid.c` is the callback
boundary. Linux-derived files remain governed by
[`upstream-porting-rules.md`](upstream-porting-rules.md). TinyUSB is changed
only where the glue cannot recover an equivalent contract through a public
API.

In particular, the pinned TinyUSB snapshot does not expose all of these
contracts:

- a post-enumeration mount fence for hubs;
- a direct host-owner enumeration/re-enumeration entry which does not enqueue
  work back into TinyUSB's only host consumer;
- the exact enumeration terminal, provisional address owner, and global EP0
  idle transition needed by task-side reset coordination;
- exact retirement of an EP0 owner after a PIO/HCD completion event is lost;
- a nonblocking failed-control retry whose continuation outlives TinyUSB's
  callback-local transfer object;
- a full configuration-descriptor buffer larger than the permanent 512-byte
  enumeration scratch without allocating in a control-completion callback;
- Linux-compatible HID descriptor discovery independent of descriptor order;
- task-side ownership of `SET_IDLE` and the report-descriptor request, including
  descriptors larger than TinyUSB's 512-byte enumeration buffer.

Device and string descriptors are reconstructed by the lifecycle task through
the generic asynchronous EP0 bridge. That part is glue, not a separate TinyUSB
source patch. The host-core patch supplies the lifecycle fences which make
those task-side requests and address generations safe.

## Active Build Substitutions

The current checkpoint uses Pico SDK 2.1.1 and TinyUSB commit
`86ad6e56c1700e85f1c5678607a762cfe3aa2f47`.

| Pinned TinyUSB input | Required SHA-256 | Generated build source | Purpose |
| --- | --- | --- | --- |
| `${PICO_TINYUSB_PATH}/src/host/usbh.c` | `eeb692225f34e3ea9fbd195e0b4b80a5fb5f1d36b092a42a78d6b88a37574ec0` | `${CMAKE_CURRENT_BINARY_DIR}/tinyusb-usbh-port.c` | host-owner lifecycle, enumeration terminals, bounded recovery |
| `${PICO_TINYUSB_PATH}/src/class/hid/hid_host.c` | `c4f1125c2235dc870b280343df5d4c088824c6d40430ea65cd21cf56f84ddea9` | `${CMAKE_CURRENT_BINARY_DIR}/tinyusb-hid-host-port.c` | descriptor-order support and Linux-owned HID setup/report-descriptor policy |
| `${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c` | `310269b29d200bae9b2eadf4ba93a1f76d41e2a9181fe743625ca85137ec4508` | `${CMAKE_CURRENT_BINARY_DIR}/tinyusb-hcd-pio-port.c` | complete the HCD clear-stall contract by resetting the local endpoint toggle |

Only these three TinyUSB entries are replaced in `tinyusb_host_base`. CMake reads
each original, removes that source entry exactly once, and compiles the
generated copy instead. This mechanism never writes any pinned TinyUSB
input. The top-level build separately normalizes TinyUSB FatFS `ffconf.h`; that
unrelated SDK-side configuration write is outside this host-port mechanism.

The active PIO path compiles the generated TinyUSB PIO-HCD source together with
this repository's vendored Pico-PIO-USB sources. The vendored PIO implementation
itself is not transformed by this mechanism.

There is one separate, narrow HCD exception: checkpoint `hid: restore asynchronous usbhid transport ownership` added
`pio_usb_host_endpoint_reset_data_toggle()` to the vendored
`Pico-PIO-USB/src/pio_usb.h` and `pio_usb_host.c`. The pinned TinyUSB PIO HCD's
`hcd_edpt_clear_stall()` returns success without resetting the local endpoint
toggle. The one PIO-HCD replacement now implements that existing HCD API by
calling the helper with `RHPORT_PIO(rhport)`. After the standard remote
`CLEAR_FEATURE(HALT)` succeeds, HID recovery calls only
`hcd_edpt_clear_stall()` on the idle endpoint before interrupt-IN is rearmed.
Abort drain timing likewise uses TinyUSB's standard `hcd_frame_number()`.
Consequently, Linux-shaped HID transport no longer knows PIO root numbering or
calls Pico-PIO-USB directly. Remove the HCD replacement only when upstream's
active PIO HCD implements equivalent clear-stall semantics, and gate that
change with the STALL recovery hardware test below.

At the current checkpoint the port has 40 exact replacement anchors:

- three host-core replacements in `CMakeLists.txt`;
- 34 enumeration/lifecycle replacements in
  `usb_host/tinyusb-usbh-enum-port.cmake`;
- two HID-class replacements in `CMakeLists.txt`;
- one PIO-HCD replacement in `CMakeLists.txt`.

The 34 enumeration/lifecycle anchors currently divide into four owner/hook
primitives, four EP0 submission/progress fixes, eight attach/remove/event-loop
fences, three terminal/retry/watchdog pieces, and 15 guarded enumeration-state
transitions. These counts are an audit index, not a stable API; update them when
an anchor is added or removed.

For scale, the generated host-core source grows from 1,780 to 3,050 lines and
its review diff is 1,400 insertions and 130 deletions. The generated HID-class
source grows from 764 to 862 lines and its review diff is 140 insertions and 42
deletions. The generated PIO-HCD source grows from 210 to 215 lines and its
review diff is 10 insertions and five deletions. Much of the insertion count is
the required adjacent copy of replaced upstream code and comments, but this is
still a real internal TinyUSB port with a real upgrade cost.

Not all 40 anchors are required merely to call the Linux HID parser. The two
HID-class replacements and the hub post-enumeration publication are the direct
HID/lifecycle adapter. Most host-core replacements implement reset,
re-enumeration, failure terminals, and lost-completion recovery discovered while
making that lifecycle asynchronous. In maintenance terms, this part is a
partial fork of TinyUSB's enumeration core even though it is packaged as a
build-time transformation.

The former `blocking SETUP submission` replacement was removed after a complete
call/link audit proved that branch unreachable: all 22 linked call sites pass a
completion callback, while TinyUSB's null-callback synchronous descriptor
wrappers are discarded by `--gc-sections`. The upstream null-callback branch
still exists inside the live `tuh_control_xfer()` function; adding any TinyUSB
`*_sync()` caller requires a fresh audit because SETUP rejection there does not
restore the global control owner and its nested `tuh_task()` would also invalidate
the current shallow-entry logging guarantee. The callback-backed SETUP rollback
remains active because it is on the real asynchronous path.

The inactive `tuh_deinit()` owner reset is also left upstream-exact because the
firmware never calls or links that function. Two SET_ADDRESS-path replacements
were removed as control-flow no-ops in this Release/RP2040 build: TinyUSB's
one-argument `TU_ASSERT(expr)` evaluates `expr` and returns `false`, so its
existing source has the same success/failure flow as the former explicit
`if (!expr) return false` spelling. A build which enables different TinyUSB
assert diagnostics must recheck that assumption.
The two second-reset hub states are likewise upstream-exact: their sole producer
is TinyUSB's existing `ENUM_RESET_2` block under `#if 0`, so neither state is
reachable in this pinned build.

`tuh_reenumerate_begin_cb()`, `tuh_port_enum_state_cb()`,
`tuh_port_control_idle_cb()`, `tuh_port_enum_event_cb()`,
`tuh_port_enum_buffer_alloc_on_host()`, and
`tuh_port_enum_buffer_free_on_host()` are mandatory strong firmware boundary
functions.
The generated core declares them but provides no weak fallback, so missing glue
fails at link time. TinyUSB's unrelated upstream weak `tuh_event_hook_cb()`
remains untouched. This also removes a former patch-on-patch anchor: every
remaining macro replacement now refers directly to pinned upstream text.

## How Generation Fails Closed

Configuration performs these checks before compiling firmware:

1. CMake hashes the complete pinned input and requires the exact SHA above.
2. Every replacement searches for the complete expected upstream block.
3. The block must occur exactly once; a missing or duplicate anchor is fatal.
4. The generated replacement retains every displaced or changed upstream line
   commented immediately beside the port-specific code and states the reason;
   unchanged anchor lines may remain live.
5. The original SDK source must occur exactly once in `tinyusb_host_base` before
   CMake replaces it with the generated source.
6. `CMAKE_CONFIGURE_DEPENDS` makes a later input-source change rerun these
   checks instead of continuing with an old generated copy.

Consequently, changing any of these three selected TinyUSB inputs cannot
silently apply the old port to a new implementation. Configuration stops with
`re-audit the port`. These hashes do not pin TinyUSB headers, `hub.c`, the rest
of Pico SDK, or the vendored Pico-PIO-USB tree; an SDK update still requires the
complete procedure below even if the three bytestrings happen to match. A SHA
is a compatibility assertion, not evidence that the port automatically remains
correct.

Generated sources live under the ignored build directory and are not committed.
The maintained generator inputs are `CMakeLists.txt` and
`usb_host/tinyusb-usbh-enum-port.cmake`.

To inspect what the compiler receives, configure first and then compare all
three generated files with the active SDK inputs:

```sh
git diff --no-index \
  /path/to/pico-sdk/lib/tinyusb/src/host/usbh.c \
  build/tinyusb-usbh-port.c

git diff --no-index \
  /path/to/pico-sdk/lib/tinyusb/src/class/hid/hid_host.c \
  build/tinyusb-hid-host-port.c

git diff --no-index \
  /path/to/pico-sdk/lib/tinyusb/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c \
  build/tinyusb-hcd-pio-port.c
```

`git diff --no-index` returns status 1 when the expected differences exist. To
inspect the exact macro-managed anchor index and active build inputs:

```sh
rg -n '^ergotype_tinyusb_usbh_replace_unique\(' \
  usb_host/tinyusb-usbh-enum-port.cmake

rg -n '"file": ".*(tinyusb-(usbh|hid-host|hcd-pio)-port\.c|/hcd_pio_usb\.c|Pico-PIO-USB/src/pio_usb_host\.c)"' \
  build/compile_commands.json

rg -o 'CMakeFiles/ErgoType\.dir/[^ ]*(tinyusb-(usbh|hid-host|hcd-pio)-port\.c\.o|hcd_pio_usb\.c\.o|Pico-PIO-USB/src/pio_usb_host\.c\.o)' \
  build/CMakeFiles/ErgoType.dir/link.txt
```

A successful configure proves that the pinned bytes and unique anchors match.
It does not prove that the port is semantically correct.

## Host-Core Change Groups

The many host-core anchors implement a small number of semantic groups. Review
and port them by group; never treat the port as a flat list of textual
substitutions.

### Post-enumeration publication

Pinned TinyUSB calls `tuh_mount_cb()` for ordinary devices but omits it for hub
addresses. The port publishes the callback only after the exact enumeration
terminal accepts success, including hubs. Lifecycle therefore receives one
post-enumeration fence rather than guessing from `tuh_mounted()` while class
configuration may still be running.

### Direct host-owner re-enumeration

Reset recovery already runs in TinyUSB's host task. Sending another attach event
to the same task and waiting for it would recurse through, or block, its sole
event consumer. The port adds direct attach/re-enumerate entries which call the
existing `enum_new_device()` path while the host owner is current. A mandatory
generation callback lets firmware reject a stale physical epoch before removing
the old device graph.

### Exact EP0 owner recovery

Pico-PIO-USB can lose the completion event used to retire TinyUSB's private
control transfer. The port may synthesize TinyUSB's ordinary terminal TIMEOUT
only after three SETUP/DATA/ACK drain fences and only when device address,
completion callback, and `user_data` still match the old owner. It cannot abort
a replacement request after address reuse.

### Observable global control and enumeration state

Task-side reset/re-enumeration must wait for durable conditions, not poll or
read TinyUSB private fields from another core. Mandatory publications expose
only the required edges: global EP0 becoming idle, enumeration becoming active,
and the exact success/failure terminal. Private TinyUSB structures remain owned
by the host task.

### Bounded enumeration and cleanup

The enumeration patch turns TinyUSB's single `_dev0`/global-control state into
an explicitly bounded host-owner lifecycle. Its anchors cover:

- per-epoch owner reset and provisional USB address tracking;
- SETUP, DATA, and ACK submission failure and progress accounting;
- root and hub reset/status/descriptor/configuration retries;
- non-blocking root reset, root/hub connection debounce, and address recovery;
- a durable 100-ms failed-control retry continuation;
- exact-size full configuration descriptors from 513 bytes through 4 KiB;
- bounded deferred-attach ownership outside the sole host event queue;
- duplicate attach, remove, retired-event, and address-reuse fences;
- a finite host-event budget and dequeue-generation fence;
- class set-config ownership and topology publication;
- terminal success/failure plus a no-progress watchdog and bounded drain.

This does not move enumeration into firmware tasks. TinyUSB still enumerates the
device in its host task. The additions make its one active enumeration epoch
observable and guaranteed to reach a terminal so the external lifecycle owner
cannot wait forever or start a competing epoch.

The pinned core's 50-ms root reset, 450-ms root/hub connection debounce, 2-ms
post-SET_ADDRESS recovery, and 100-ms failed-control retry waits used to block
that sole host event pump.
The port now records the exact continuation and deadline, returns to the pump,
and resumes the corresponding upstream tail from the host-owner service. The
firmware loop remains literally `while (1) tuh_task();`. At shallow entry,
generated `tuh_task_ext()` services due state and reduces its caller-supplied
queue timeout to the nearest host-owned deadline. HCD/deferred work wakes the
same queue earlier; with no deadline the ordinary `tuh_task()` wait remains
indefinite. This follows current TinyUSB's internal call-after model without an
external timer, extra queue, finite periodic tick, or firmware-visible
`wait_ms`. Root reset is ended
exactly once on normal continuation or terminal cancellation. REMOVE, duplicate
ATTACH, timeout, and terminal cleanup therefore remain observable during every
normal enumeration delay. A failed control completion copies its setup packet,
data-buffer pointer, device address, and `user_data` into the current enum
epoch, arms the original 100-ms deadline, and returns. The shallow host-owner
service rebuilds a temporary `tuh_xfer_t` and resubmits only after the callback
has unwound, the deadline is due, and global EP0 is idle. The buffer itself
remains owned by the enum epoch. TinyUSB's existing
`ATTEMPT_COUNT_MAX == 3` delayed-retry bound is unchanged; there is no
callback-stack reference, blocking delay, periodic poll, Pico
timer, FreeRTOS timer, or second wake queue.

### Full configuration-descriptor ownership

Pinned TinyUSB stores every full configuration descriptor in its permanent
`CFG_TUH_ENUMERATION_BUFSIZE` scratch and rejects a larger `wTotalLength`. The
port deliberately keeps that scratch at 512 bytes: ordinary devices add no
heap allocation and static RP2040 RAM does not grow by the 4 KiB worst case.
After the nine-byte header is validated, lengths from 513 through
`ERGOTYPE_TUH_ENUMERATION_MAX_BUFSIZE` (currently 4096) become a durable
host-owner pending state. The completion callback records only the length and
returns. At the next shallow `tuh_task_ext()` entry, with global EP0 idle,
the host owner calls `tuh_port_enum_buffer_alloc_on_host()`, submits the full
GET_DESCRIPTOR into that exact-size buffer, and retains it through any
failed-control retry.

On RP2040, `usb_host/task.c` implements the allocator boundary with
`pvPortMalloc()`/`vPortFree()`. The hook is intentionally outside the generated
TinyUSB source so an ESP host can later require DMA-capable internal memory
without changing the pinned enumeration state machine. Allocation runs only at
shallow host-owner service after the short-GET completion has unwound. On the
success path, the internal `process_enumeration()` continuation frees the buffer
immediately after synchronous class-open parsing; terminal cleanup frees it
after EP0 drain on failure. TinyUSB application callbacks never allocate or
free this buffer.

TinyUSB class `driver_open()` consumes the configuration bytes synchronously
and retains copied interface state, not pointers into the descriptor. The port
therefore frees a long buffer immediately after successful configuration parse,
before class `set_config` and Linux HID lifecycle/probe allocations. Failure,
REMOVE, timeout, address reuse, and watchdog terminal paths drain the matching
EP0 owner first and then release the same buffer exactly once. Invalid headers,
short/mismatched full reads, allocation failure, and lengths above 4 KiB fail
the enumeration epoch instead of exposing a partial descriptor to a class
driver. The 512-byte static scratch remains live as before; a maximum long
descriptor adds one transient 4096-byte request (about a 4104-byte heap_4
block), not another permanent buffer.

Before `SET_CONFIGURATION`, a port preflight walks the exact returned extent
and bounds every device-supplied `bLength`. It also validates the standard
configuration/interface/IAD/endpoint headers, proves every IAD or implicit
CDC/MIDI group contains the base interfaces TinyUSB will traverse, and rejects
an interface number outside TinyUSB's fixed `itf2drv[]` array. The current
`CFG_TUH_INTERFACE_MAX` is eight: a descriptor may be larger than 512 bytes,
but configurations advertising more than eight base interfaces fail with
`ERR: HID_ENUM_CONFIG_INVALID` instead of corrupting host state. The companion
600-byte fixture has four interfaces and puts its useful HID interface at byte
575, so it remains inside that explicit firmware capacity.

Pinned TinyUSB also deferred a foreign ATTACH by sending it back into its main
queue with an infinite task wait. Because the sender is that queue's sole
consumer, an ISR refill could leave it blocked on itself. The port instead keeps
one deduplicated `{rhport, hub_addr, hub_port}` record per possible device in a
compact host-owned FIFO. REMOVE prunes the matching topology and descendants
before TinyUSB clears their parent links; the next host-service iteration after
enumeration terminal and mount-callback unwind starts the oldest surviving
topology directly. No extra RTOS queue or heap allocation is involved.
Exhaustion drops the excess event and publishes
`ERR: HID_ATTACH_OVERFLOW` through the ordinary asynchronous diagnostic path.

One newer-upstream behavior is deliberately still pending: current TinyUSB
removes an already mounted device at the same topology before accepting every
new ATTACH. The pinned path handles a duplicate during active enumeration but
still assumes that an idle mounted topology receives REMOVE before a later
ATTACH. Porting the newer rule requires coordination with the firmware's
asynchronous lifecycle retirement; calling `process_removing_device()` and
immediately reusing the address would reintroduce the lifetime race this layer
exists to prevent.

The deliberately small host-core interface consumed by firmware is:

- `usbh_port_attach_on_host()` and `usbh_port_reenumerate_on_host()`;
- `usbh_port_control_recover_on_host()`;
- `tuh_reenumerate_begin_cb()`;
- `tuh_port_enum_state_cb()` and `tuh_port_control_idle_cb()`;
- `tuh_port_enum_event_cb()`;
- `tuh_port_enum_buffer_alloc_on_host()` and
  `tuh_port_enum_buffer_free_on_host()`.

The `*_on_host()` entries may be called only by the registered TinyUSB host
owner. USB/timer callbacks publish bounded identity/result state and wake edges
only; they do not allocate, log, parse, run Linux driver policy, or wait for
USB, lifecycle, queue, or parser progress. The enum-buffer allocator and
`tuh_port_enum_event_cb()` run at shallow `tuh_task()` entry after the previous
callback unwound. The allocator may therefore use the platform heap, and the
event hook may publish an `async_msg()` diagnostic. The matching free hook is
also used by TinyUSB's internal enumeration continuation immediately after its
synchronous class scan; it is never reached from an application callback.
Publication may briefly take the shared
priority-inheritance transport mutex. That mutex is never held across a
TinyUSB/HCD call, host-task handoff, parser, heap operation, logger, or
condition wait, so its release never depends on further TinyUSB progress.

## HID-Class Change Groups

The two `hid_host.c` replacements preserve TinyUSB's complete upstream blocks
beside the local implementation.

### Interface descriptor scan

Pinned TinyUSB assumes the strict order interface -> HID -> endpoints. Real HID
interfaces may put class-specific descriptors after an endpoint or include
additional descriptors. The port performs a bounded scan of the current
interface, accepts the HID descriptor in Linux-supported positions, opens no
more than `bNumEndpoints`, and publishes the TinyUSB class slot only after all
required endpoint opens succeed.

### Class setup policy

TinyUSB's class configuration normally sends `SET_IDLE`, optionally sends
`SET_PROTOCOL`, and then fetches the report descriptor. The port leaves these
out of enumeration:

- Linux-shaped `usbhid_parse()` sends `SET_IDLE` and applies Linux's ignored
  status behavior;
- Linux relies on USB reset selecting Report protocol, so no generic
  `SET_PROTOCOL` is added;
- lifecycle owns the authoritative report-descriptor request and its retries.

### Report-descriptor ownership

TinyUSB's normal HID path borrows the shared 512-byte enumeration buffer for an
early report-descriptor read. The port mounts the class with a `NULL` report
descriptor. Later, task-side `usbhid_parse()` validates the retained HID class
descriptor and reads the exact report length, capped at Linux's 4 KiB HID
descriptor limit, through the asynchronous EP0 bridge with four wire attempts.
This removes both the 512-byte limit and the duplicate read. The upstream
`CONFIG_GET_REPORT_DESC` switch branch remains byte-identical in the generated
source but is unreachable because the replacement `hidh_set_config()` completes
the class mount directly and never produces that state.

## PIO-HCD Change Group

The one `hcd_pio_usb.c` replacement preserves the complete upstream function
beside its local implementation. TinyUSB's HCD contract requires
`hcd_edpt_clear_stall()` to reset the controller-side endpoint state after the
remote halt has been cleared. Other TinyUSB HCDs perform this reset; the pinned
PIO adapter instead discards every argument and returns success.

The generated adapter converts TinyUSB's root-port number with its existing
`RHPORT_PIO()` macro and calls the vendored
`pio_usb_host_endpoint_reset_data_toggle()`. It owns no recovery policy and
does not send the remote request. The report task still sends standard
`CLEAR_FEATURE(HALT)`, and only its successful completion schedules the host-
owner call to the generic `hcd_edpt_clear_stall()` API. The endpoint is idle at
that point and interrupt-IN is rearmed only after this call succeeds.

## Low-Priority HCD Follow-up

This is future robustness work, not part of the current async-lifecycle
checkpoint. TinyUSB's fixed host-event queue can reject an ISR publication
when it is full, while the pinned PIO HCD clears the corresponding endpoint or
topology source after its void event call. Exact-owner EP0 watchdog/drain
recovery prevents that case from permanently occupying the global control
lane, but a genuinely saturated queue can still lose a non-EP0 completion.
The present eight-endpoint PIO pool, 16-entry TinyUSB queue, and high-priority
host owner make this unlikely during normal HID traffic; they are not a formal
completion-durability contract.

Do not restore discarded change `hid: retain PIO events under host
backpressure`. Its retained-event ring, complete IRQ rewrite, and
`pio_usb_host_frame()` producer fence turned queue backpressure into a stop of
PIO SOF and endpoint scheduling. It also depended on the already-rejected
topology-ordering changes in `hid: order asynchronous host event ingress` and `hid: preserve asynchronous topology ordering`.

Before changing the HCD, reproduce the condition with isolated fault injection
and verify continued input, output, detach/replug, heap, and stack watermarks.
If the remaining risk is worth closing, prefer a small acceptance/retry
contract: clear a PIO source only after the TinyUSB host queue durably accepts
its event, or fail closed through a controlled host reset. Do not add a second
event ring, stop SOF, add periodic polling, or introduce Pico timer APIs. Keep
the contract backend-capable so a future ESP HCD can provide its own
cancel/giveback semantics without inheriting PIO-specific drain behavior.

## What Must Stay Outside This Port

Do not add another TinyUSB anchor when the behavior can live in task-side glue.
The following remain outside the generated TinyUSB sources:

- Linux HID parsing, report mapping, driver matching, haptic, and multitouch;
- KeyD/evdev policy;
- FreeRTOS wait queues, workqueues, timers, logging, and heap policy;
- ordinary control/interrupt request queues and their caller-facing completion;
- Pico-PIO-USB endpoint implementation and timing code.

A change to Pico-PIO-USB is a separate HCD-level decision and needs its own
hardware checkpoint and documentation. It must not be smuggled into a TinyUSB
host-core maintenance change.

## TinyUSB/Pico SDK Upgrade Procedure

Never update only the three SHA strings. Rebase semantics in a separate branch or
dirty checkpoint, one group at a time:

1. Record the old and new Pico SDK versions and TinyUSB commits.
2. Point the build at the new SDK and confirm that the existing SHA guard fails.
3. Compare new upstream `usbh.c`, `hid_host.c`, and `hcd_pio_usb.c` with both
   old upstream sources and the generated port sources.
4. Check whether the new TinyUSB release already supplies each required public
   hook or behavior. Delete a local group when upstream now provides the same
   contract.
5. Rebase each semantic group above onto the new upstream control flow. Keep
   every replaced upstream block adjacent and update its reason.
6. Preserve exact unique-anchor checks. Do not weaken a failed anchor into a
   broad regular expression or blind patch.
7. Regenerate in a clean build directory and review the three full generated
   diffs with `git diff --no-index`; confirm that no unrelated upstream code was
   copied, dropped, or reordered.
8. Confirm the original SDK sources are absent from compile/link inputs, all
   three generated sources are present once, and Pico-PIO-USB still comes directly
   from its intended vendored source.
9. Build with repository policy (`TMPDIR=$HOME/tmp`, never the
   system `/tmp`) and inspect code/RAM size plus all task stack watermarks.
10. Run the hardware matrix below. Only after it passes should the new SHA,
    version note, and verified firmware checkpoint be committed together.

Required hardware coverage for a TinyUSB port update:

- root keyboard and mouse input;
- composite keyboard/mouse/haptic-touchpad interfaces;
- the emulator descriptor layouts which place HID/extras around endpoints;
- an authoritative report descriptor larger than 512 bytes;
- a configuration descriptor larger than 512 bytes, with a useful HID
  interface located beyond byte 512;
- injected enumeration-control failure followed by the 100-ms retry, including
  REMOVE during the armed deadline;
- hub child enumeration and repeated unplug/replug;
- fast address reuse and composite-interface remove ordering;
- output/feature requests and haptic response;
- stalled/failed interrupt IN, clear-halt, and rearm;
- reset/re-enumeration and lost-completion recovery fixtures where available;
- repeated add/remove with stable heap and safe stack high-water marks.

## When to Replace This Mechanism

The build-local generator is reasonable while the port remains limited to a
small, pinned set of TinyUSB sources and Pico SDK owns the TinyUSB checkout. It
has two useful properties: the pinned inputs are not overwritten and source
drift is fatal.

The third pinned source is currently a single HCD function with one exact
anchor. If the port grows beyond these three TinyUSB sources, the HCD change
stops being that narrow adapter, updates become frequent, or semantic review of
the generated sources becomes harder than reviewing normal commits, move the
same groups into an explicitly pinned TinyUSB fork or an ordered patch series.
That would change packaging, not the architectural contract: upstream code
must remain visible, every delta must have one reason, and each TinyUSB update
still requires the same hardware gate.
