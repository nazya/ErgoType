# Async HID Progress

## First Slice: Resolution Multiplier

- `usb_host/hid_async.c` adds a bounded FreeRTOS HID control task.
- TinyUSB GET/SET completion callbacks only enqueue a small completion record.
- The active GET/SET report buffer lives in the HID async task, not in the
  callback stack.
- `hid-input.c` keeps the upstream `hid_hw_request()` / `hid_hw_wait()` /
  `__hid_request()` lines commented next to the async replacement.
- The integration was applied manually instead of restoring `stash@{0}`;
  that stash also contains unrelated mouse16 descriptor/output work.
- Current continuation target is resolution multiplier setup:
  - queue GET_REPORT when upstream needs `hid_hw_wait()`;
  - parse GET_REPORT completion through `hid_input_report()`;
  - queue SET_REPORT when multiplier values changed;
  - run `hid_setup_resolution_multiplier()` after SET completion.
- The HID async task waits for completion without a timeout in this first
  slice. That keeps TinyUSB's active transfer buffer live until completion;
  callback code still only enqueues a small completion record.

## Hardware Verification

- 2026-07-10: Razer BlackWidow async raw SET_REPORT path verified on hardware.
  The fixture is `../ErgoType-hid-devices` branch
  `device/razer-blackwidow`, commit `4b2fc8f`, emulating VID:PID
  `1532:010e` as `Razer BlackWidow Test`.
- Laptop Linux check: the emulated Razer device enumerates under the Linux
  `razer` HID driver and emits macro usage `0x68`, which keyd reports as
  unsupported evdev code `0x290`.
- ErgoType Pico host check: after enumeration the host logs
  `DBG: HID_RAW_SET_OK`, then the same macro usage reaches the keyd port as
  unsupported evdev code `0x290`.
- This verifies the async raw SET_REPORT transport boundary used by
  `hid-razer.c`. The remaining `0x290` handling is consumer/keyd-side work,
  not an async HID transport failure.

## Remaining Work

- `tuh_ll_driver.c` now wires `hid_hw_request()` / `.request` into the same
  async queue as the resolution-multiplier slice. This covers the simple
  upstream pattern where drivers submit `HID_REQ_SET_REPORT` and do not call
  `hid_hw_wait()`. Generic GET completion without a custom continuation is
  routed through `hid_input_report()`, matching upstream `hid_ctrl()`.
  Queue/submit failure logs `ERR: HID_ASYNC_REQ_FAIL`; successful requests stay
  silent.
- `tuh_ll_driver.c` now wires `hid_hw_output_report()` / `.output_report` into
  the same HID async task through `hid_async_queue_output_report()`. This covers
  Linux's synchronous `usbhid_output_report()` interrupt OUT path without
  blocking: the queue stores report bytes, `tuh_hid_send_report()` runs in the
  HID async task, and `tuh_hid_report_sent_cb()` only enqueues completion.
- `tuh_ll_driver.c` now handles raw SET_REPORT by queuing the raw buffer through
  `hid_async_queue_raw_set_report()`. Raw GET_REPORT still returns `-ENOSYS`
  because callers need explicit async continuations for returned data. The
  queue buffer is currently 96 bytes so the Razer Blackwidow 91-byte
  SET_FEATURE init report fits.
- `hid_async_queue_raw_get_report()` adds the first raw GET_REPORT async
  building block. It mirrors Linux `usbhid_get_raw_report()` for unnumbered
  reports by keeping report ID 0 in byte 0 while TinyUSB receives payload at
  byte 1, and completion counts that byte in `actual_len`.
- `hid_async_queue_raw_get_report_id()` adds the matching raw GET_REPORT
  building block for upstream sites that only have a report id/type/length
  tuple, such as `hid_hw_raw_request(hdev, 0xBF, buf, len,
  HID_FEATURE_REPORT, HID_REQ_GET_REPORT)`. It still requires an explicit
  continuation and does not make `.raw_request(GET_REPORT)` synchronous.
- `hid_async.c` now filters TinyUSB completion callbacks against the current
  active transfer before queueing them to the HID async task. Before each new
  submit, the task drops stale completion wakeups that cannot belong to the new
  request. This keeps the one-active-transfer async model from hanging if an
  old cancel/late completion leaves data in the internal completion queue.
- LED output moved one step forward: `hidinput_input_event()` still keeps the
  upstream `schedule_work(&hid->led_work)` line commented, but calls the
  upstream worker body directly because that worker now reaches the nonblocking
  `.request` queue path.
- `feature_mapping` is re-enabled in `hid-input.c` for async-converted users.
  `hid-vivaldi.c` / `hid-vivaldi-common.c` are imported as the first concrete
  case: upstream's synchronous `hid_hw_raw_request()` feature fetch is kept
  commented next to a port replacement that queues one async raw GET_REPORT for
  the whole feature report, reports the returned data through
  `hid_report_raw_event()`, then fills the function-row physmap from completion
  in the HID async task.
- `hid-kye.c` is imported as the first driver that actively exercises the
  simple async SET_REPORT path from probe. Upstream `hid_hw_request()` remains
  in place; the TinyUSB ll-driver queues it through `hid_async_queue_report()`
  and does not emulate `hid_hw_wait()`.
- `hid-razer.c` is imported as the first driver that exercises raw SET_REPORT
  through the async transport boundary. Its upstream `hid_hw_raw_request(...
  HID_FEATURE_REPORT, HID_REQ_SET_REPORT)` call remains active; `tuh_ll_driver.c`
  queues it without blocking.
- `hid-cmedia.c` is imported only for the independent HS100B descriptor
  `report_fixup` slice. The upstream CM6533 jack-control half remains visible
  but disabled because it needs `.raw_event` and `HID_CONNECT_HIDDEV_FORCE`.
- `hid-primax.c` is imported as the first callback-safe `.raw_event` slice.
  The port adds an explicit `raw_event_callback_safe` marker to `struct
  hid_driver`; `hid-core.c` only calls `.raw_event` when that marker is set.
  This keeps the upstream raw-event branch visible while preventing unaudited
  driver callbacks from running in the TinyUSB receive callback path. Primax
  only rewrites the input report buffer and re-enters `hid_report_raw_event()`,
  with no wait/work/mutex/control request path.
- `hid-pxrc.c` is imported as the second callback-safe `.raw_event` slice. Its
  probe uses the existing `devm_kzalloc()`/`hid_set_drvdata()` compat pattern,
  then the raw-event hook only shuffles already received axis bytes in the
  report buffer. It uses the same `raw_event_callback_safe` gate.
- `hid-saitek.c` is imported as another callback-safe `.raw_event` slice. Its
  probe uses the existing drvdata compat pattern; the raw-event hook only clears
  mode bits in the report buffer, and the `event` hook emits the missing release
  event through the input core. It uses no HID request/wait/work path.
- `hid-chicony.c` is imported as another callback-safe `.raw_event` slice. Its
  raw-event hook only emits RFKILL press/release events through the input core
  for wireless-radio reports, and the rest of the driver stays in the existing
  report-fixup/input-mapping/probe pattern.
- `hid-zydacron.c` is imported as another callback-safe `.raw_event` slice. Its
  raw-event hook only emits key release/press events through the input core for
  already received remote-control reports, with no HID request/wait/work path.
- `hid-rapoo.c` is imported as another callback-safe `.raw_event` slice. Its
  probe creates the upstream extra input device for back/forward mouse buttons,
  and the raw-event hook only emits those key events from the received report.
- `hid-creative-sb0540.c` is imported as another callback-safe `.raw_event`
  slice. Its probe/input_configured path builds the upstream remote keymap, and
  the raw-event hook only decodes already received IR reports into input-core
  key events.
- `hid-waltop.c` is imported as another callback-safe `.raw_event` slice. It
  keeps the upstream descriptor fixups and raw-event report byte rewrites for
  Waltop tablets; it does not call HID requests, wait, workqueue, mutex, or any
  class subsystem from the callback path.
- `tuh_hid_umount_cb()` now calls `hid_async_cancel_device()` before destroying
  the matching `hid_device`. The cancel path drops queued control requests for
  that TinyUSB `dev_addr/instance` and wakes the active request wait with a
  cancel completion. This is required because TinyUSB's unplug path calls
  `tuh_hid_umount_cb()` from `hidh_close()` and then marks an active EP0
  control transfer idle without issuing the HID get/set report completion
  callback. Canceled requests intentionally do not run continuations: the
  continuation context may belong to the HID device that is being removed.
- `hid-accutouch.c`, `hid-a4tech.c`, `hid-aureal.c`, `hid-belkin.c`,
  `hid-cherry.c`, `hid-chicony.c`, `hid-cmedia.c` HS100B,
  `hid-creative-sb0540.c`, `hid-cypress.c`, `hid-elecom.c`, `hid-evision.c`,
  `hid-ezkey.c`, `hid-gembird.c`, `hid-glorious.c`, `hid-gyration.c`,
  `hid-holtek-kbd.c`, `hid-holtek-mouse.c`, `hid-huawei.c`, `hid-icade.c`,
  `hid-ite.c`, `hid-jabra.c`, `hid-kensington.c`, `hid-keytouch.c`,
  `hid-kye.c`, `hid-lcpower.c`, `hid-macally.c`, `hid-maltron.c`,
  `hid-monterey.c`, `hid-nti.c`, `hid-ortek.c`, `hid-penmount.c`,
  `hid-petalynx.c`, `hid-plantronics.c`, `hid-primax.c`, `hid-pxrc.c`,
  `hid-rapoo.c`, `hid-razer.c`, `hid-redragon.c`, `hid-retrode.c`,
  `hid-saitek.c`, `hid-semitek.c`, `hid-sigmamicro.c`, `hid-speedlink.c`,
  `hid-sunplus.c`, `hid-tivo.c`, `hid-topre.c`, `hid-topseed.c`,
  `hid-twinhan.c`, `hid-viewsonic.c`, `hid-waltop.c`, `hid-vivaldi.c`,
  `hid-vrc2.c`, `hid-xiaomi.c`, `hid-xinmo.c`, and `hid-zydacron.c` are imported as
  upstream-shaped driver slices. Outside the async-converted Vivaldi
  `feature_mapping`, Kye SET_REPORT path, Razer raw SET_REPORT path, and
  audited Primax/PXRC/Saitek/Chicony/Zydacron/Rapoo/Creative raw-event paths,
  they use only `report_fixup`, `usage_table`, `input_mapping`,
  `input_mapped`, `input_configured`, `event`, and simple `probe` hooks, with
  no `hid_hw_wait()`, raw control request, workqueue, or semaphore path. This
  keeps driver-event handling exercised without adding callback blocking.
- Decide whether completion timeout should log through `async_msg()` or stay
  silent until a concrete device needs diagnostics.
- Keep driver feature/raw hooks disabled until each blocking call site has an
  explicit async state machine.

## Deferred Driver Groups

- Request-based driver audit: after the imported Kye SET_REPORT path, Razer raw
  SET_REPORT path, and Vivaldi raw GET_REPORT continuation, the only upstream
  `drivers/hid/hid-*.c` file found with HID request calls and without obvious
  Linux class/work/wait/lock dependencies is `hid-alps.c`. ALPS is still not a
  small next patch: `alps_input_configured()` must run a multi-step register
  init sequence (`SET_REPORT` command, optional `GET_REPORT` reply, validate,
  then continue) before it knows the input capabilities. Porting that means an
  explicit async state machine for `t4_read_write_register()` and
  `u1_read_write_register()`, not a direct import.
- Linux class-backed request users are separate packages, not transport-only
  async patches. Apple/Kysona/LED/backlight/battery drivers need active
  `leds`, `backlight`, or `power_supply` class shims before their HID request
  paths are useful. FF drivers need `ff-memless.c` plus timer/effect ownership
  before their queue-only `hid_hw_request(... SET_REPORT)` play paths are
  meaningful.
- Drivers with `.raw_event` stay deferred unless they are explicitly audited and
  marked `raw_event_callback_safe`. Primax, PXRC, Saitek, Chicony, Zydacron,
  Rapoo, Creative SB0540, and Waltop are the first enabled cases.
- Force-feedback drivers stay deferred. They commonly call `hid_hw_request()`
  from FF play paths and need a separate FF/output state-machine pass.
- Drivers with workqueue, delayed work, timers, mutexes, or synchronous raw
  requests stay deferred.
- Other `feature_mapping` users remain deferred until each one has an explicit
  async state machine. Vivaldi is intentionally driver-specific: it batches the
  per-usage upstream hook into one async GET_REPORT per feature report instead
  of exposing a fake synchronous `.raw_request`.
- `input_configured` is re-enabled for the upstream input setup flow. The
  simple `hid-retrode.c` user is imported; remaining worktree users also need
  raw-event or more complex ownership work.
- `hid-samsung.c` remains deferred even though most of it is simple
  `report_fixup`/`input_mapping`: the IR remote probe can force
  `HID_CONNECT_HIDDEV_FORCE`, and hiddev is not wired in this port.
- Roccat KonePure/Ryos remain deferred: their raw-event path is simple, but the
  probe depends on the roccat char/sysfs helper layer rather than just HID
  input.
- `hid-letsketch.c` remains deferred: raw-event itself is mostly input events,
  but probe uses USB string-descriptor handshakes and the input path uses a
  timer/HIDRAW connection shape that needs its own async pass.
- `hid-gfrm.c` is small and callback-safe, but its id table is Bluetooth-only;
  it is not useful for this TinyUSB USB host slice until the transport scope
  changes.
- `hid-led.c`, `hid-lenovo.c`, and `hid-steelseries.c` remain deferred because
  their useful paths depend on LED/power-supply classes, workqueue or delayed
  work, mutex/spinlock state, and raw GET/SET transport ownership.
- `hid-appletb-bl.c` remains deferred for now. It is a small SET_REPORT user,
  but importing it usefully requires a backlight class implementation, not just
  HID async transport.
- `hid-apple.c` backlight config is a future raw GET_REPORT candidate for
  `hid_async_queue_raw_get_report_id()`, but the useful path also needs the
  LED/backlight class ownership decision first.
- `hid-multitouch.c` `mt_get_feature()` is a future `struct hid_report *`
  raw GETREPORT candidate, but the full driver pulls in the multitouch/haptic
  input lifecycle, so it should be converted as its own package rather than as
  another transport-only slice.
- Simple FF drivers such as `hid-megaworld.c`, `hid-zpff.c`, and `hid-lg2ff.c`
  remain deferred as a group. Their HID transport side is queue-only
  SET_REPORT, but enabling them requires the memless FF path and its timer/
  effect ownership lifecycle, not just another driver file.
