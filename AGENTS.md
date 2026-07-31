# ErgoType Agent Notes

## Change Policy

- Do not change runtime logic without explicit user confirmation.
- Before a logic change, describe the current failure, proposed behavior, and risk.
- Do not add checks for hypothetical callers, future edits, or states excluded by current code and configuration.
- Do not write set-then-check control flow such as `x.type = FOO; if (x.type == FOO)`.
- Do not modify comments unless comment changes are explicitly requested.

## Upstream Porting

- Preserve upstream structure, names, branches, and inline logic where practical.
- Do not replace upstream HID logic with reduced local parsers, mappers, or helper abstractions.
- Keep temporarily unsupported or Linux-specific upstream code commented next to the active port code.
- When a port-specific replacement is required, retain the upstream line or block beside it and add a short reason for the difference.
- Keep upstream switch branches inline; adapt only the immediate platform/interface operations.
- HID-host work must move toward the HID proxy rather than the current remapper subset.

## Code Organization

- Prefer simple, direct code and avoid unnecessary wrappers, headers, and duplicated declarations.
- Declare local variables near first use, especially large stack objects such as `FATFS`, `FIL`, `DIR`, and `FILINFO`.
- Keep shared runtime objects in `main.c` unless ownership is explicitly changed.
- Keep logging centralized in root `log.c` and `log.h`.
- Commit messages use the existing short form `area: what changed`.

## Current Architecture

- Platform-neutral interfaces live under `platform/include/platform/`; RP2 and ESP-IDF implementations live under `platform/rp2/` and `platform/esp-idf/common/`.
- `mode`, `hid_output_profile`, `vkbd_event_queue`, `input_event_queue`, `log_mutex`, and `fatfs_mutex` are defined in `main.c`.
- The motion IRQ task handle is private to `pointing/pointer.c` and is bound by `pointing_motion_irq_init(...)`.
- `keyd/port/daemon.c` owns `active_kbd` and its `vkbd` pointer; `keyd/port/vkbd/vkbd.c` owns the static virtual-device object.
- Shared storage logic lives under `storage/`; platform backends and FatFs disk I/O live in the platform directories.
- TinyUSB device tasks call `tud_task()` and `platform_cdc_poll()` from the platform USB implementations.
- Consumer Page usages use the separate `REPORT_ID_CONSUMER`; Keyboard/Keypad usages remain in the keyboard report.
- Logging is serialized and written through buffered platform CDC output.

## Build

- Configure RP2: `cmake -S . -B build`
- Build RP2: `cmake --build build -j4`

## Codex Environment

- If a Git operation fails because `.git` is read-only or `index.lock` cannot be created, stop and do not modify working files as a workaround.
- In Codex, treat that failure as a possible sandbox restriction rather than evidence that the repository mount or permissions are broken.
- Request escalation for the exact Git command that failed. If escalation is unavailable or declined, ask the user to run that command locally.
- If the same `index.lock` or sandbox write restriction happens again, explicitly identify it as a repeat of the same issue.
- Do not use `/tmp` for backups, patches, conflict resolutions, worktrees, or state that must survive Git, rebase, or merge operations. Use the repository or a clear persistent directory under `/home/user`, then clean it up when finished.

## Open Work

### P0

- Fix `command(...)` parsing in `keyd/port/config.c`: it can return success without initializing the descriptor.
- Map MSC READ10/WRITE10 backend failures to appropriate SCSI sense data.

### P1

- Move `last_wake_time` initialization outside the keyscan loop so `vTaskDelayUntil()` maintains a stable period.
- Resolve blocking `xQueueSendToBack(..., portMAX_DELAY)` calls in the scan path.
- Enforce the results of task creation calls that are currently ignored.
- Emit layer-change callbacks only for real inactive/active transitions, not every reference-count change.

### P2

- Harden JSON/keyd parsing for present failure modes: missing values, descriptor argument overflow, malformed sections or lines, unchecked second-pass failures, and unsafe numeric conversion.
- Recheck macro hold/release semantics and malformed `+` groups.

### P3

- Add WebHID editor Tab/Shift+Tab indentation, then find/go-to-line if still needed.
- Add a Web Bluetooth configuration/log transport using a dedicated BLE GATT protocol with fragmentation and retry.

## Working Agreement

- Keep this file limited to current constraints, architecture, and unfinished work; completed work belongs in Git history.
- Update or remove an open-work item when its implementation is completed.
