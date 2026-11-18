# PIO USB resume checks

Use the main firmware and its existing `build/` directory. There is no
standalone host image or extra CMake project.

## Source-level and native checks

```sh
python3 tests/pio-usb-pm/check_eop.py
python3 tests/pio-usb-pm/check_fixture.py
python3 tests/pio-usb-pm/check_remote.py
```

The script follows the current EOP byte through the TX program and confirms
that pin release, unlike `PC > 2`, remains observable at the final FIFO stall.
It then compiles the real `pio_usb_host_resume_signal_end()` body with an MMIO
model and checks normal completion, late CPU observation, DMA/IRQ/pin-release
timeouts, the shared 100 us budget, timer wrapping, both GPIO orders, and a
nonzero RP2350 PIO GPIO base. The native executable is placed directly in the
existing `build/`; no new build directory is created.

These checks exercise the C control flow. They do not measure the USB waveform
or qualify real RP2350 hardware.

The fixture check compiles the real test caller with task and transport stubs.
It checks input arriving before the test task starts, input arriving after
30 seconds, unrelated notifications, wrong VID/PID or report, mismatched
generations, and one-shot notification. It stops at the first PM request;
the actual suspend/resume cycle still requires the two-board hardware test.
It also checks the real-keyboard fixture's boot/NKRO trigger, early completion,
resume failure, and rejection of input from a replacement device generation.

The remote check compiles the real PIO wake IRQ, port suspend/resume state
machine, and desired-state mailbox with native stubs. It checks FS/LS K
polarity, both pin orders, the wake permission, duplicate IRQs, invalid line
state, active transfers, EOP failure, wrapping timers, overwritten targets,
resume arriving during suspend, late host readiness, and the remote-wake latch.
It checks that the wake-request callback runs outside the transport lock,
before the resume result, and that the firmware defers only wake requests,
not ACTIVE/error completions, to the USB-device task.
It does not measure IRQ latency or the electrical waveform.

## Hardware test

The main host uses **D+ GPIO21, D- GPIO20** (`DMDP`). Use the existing host-port
wiring, common ground, and power arrangement. Do not join independently powered
5 V outputs. The emulator Pico connects through its native USB port.

Enable the existing test-only PM caller in the main build:

```sh
cmake -S . -B build -DUSBHID_PM_FIXTURE=ON
cmake --build build -j4
```

Flash `build/ErgoType.uf2` onto the host. Flash the current `Generic HID PM Test`
from the adjacent `ErgoType-hid-devices` checkout onto the second Pico. Restart
both boards with the emulator connected directly, without a hub.

The host fixture waits for F1 press and release from `cafe:11f0` to reach the
active input parser in the same device generation. It then suspends the generic
HID device, keeps it asleep for 250 ms, resumes it, and checks the async
completion and duplicate-request results. There is no boot-relative start
deadline, so connecting the emulator after host startup does not skip the
cycle. Expect F1 before sleep, F2 after resumed IN, F10
from the emulator, and `INFO: HID_PM_TEST_OK` from the host. The key reports
are normal test markers and may reach the attached computer through keyd.

Both the markers and `HID_PM_TEST_OK` are required; one alone is not a pass.
The F1 hook is compiled only with `USBHID_PM_FIXTURE`; it does not change the
production input path.
Unexpected disconnect, reset, missing resume or report completion, callback
timeout, or any `ERR: HID_PM_*` is a failure. These are ordinary central async
messages in the host CDC log; the fixture does not log from UI or PIO IRQ.

The native fault checks replace the discarded standalone image's deliberate
IRQ-hang modes: failures must now return from the real helper, not leave a
second core detecting an intentional hang. Electrical fault injection and
disconnect-during-sleep hardware qualification remain separate work.

The production resume path keeps SOF and endpoint traffic fenced after an EOP
timeout, disables TX, releases D+/D-, and posts a nonblocking DMA abort. It
does not report ACTIVE. The PM completion returns `-ETIMEDOUT`, and further PM
requests return the same error until the physical device disconnects. Once
DMA BUSY and ABORT have retired, removal can restore the transmitter for a
fresh connection. If DMA itself cannot retire, the port stays fenced rather
than blocking an IRQ.

The first hardware test covers direct full-speed generic HID only. It does not
qualify low-speed devices, hubs, remote wakeup, vendor-driver PM callbacks, or
the precise K/SE0/J timing. Leave `USBHID_PM_FIXTURE=OFF` in production builds.

## Real keyboard: host-only remote-wakeup test

This test keeps the remapper's USB connection to the laptop active. Only the
downstream keyboard sleeps; the laptop's wakeup behavior is not a pass/fail
condition. Use the same fixture option and existing build directory:

```sh
cmake -S . -B build -DUSBHID_PM_FIXTURE=ON
cmake --build build -j4
```

Flash `build/ErgoType.uf2` onto the remapper. The keyboard keeps its existing
firmware; no emulator change is needed. Connect it directly to D+ GPIO21,
D- GPIO20. This fixture accepts ErgoType `cafe:1001` boot and `cafe:1003` NKRO
keyboard reports. A press and release of any letter key starts the test.
The old `cafe:11f0` emulator still uses F1 for the earlier host-initiated test.

Run this complete block on the laptop. It holds the upstream USB device active
until you press Enter after the test, then restores its previous setting:
Use the laptop keyboard for the terminal commands so that typing them does
not start the downstream keyboard's test early.

```sh
bash <<'SH'
set -eu
devices=()
for candidate in /sys/bus/usb/devices/*; do
    [ -r "$candidate/idVendor" ] || continue
    [ "$(<"$candidate/idVendor")" = cafe ] || continue
    case "$(<"$candidate/idProduct")" in
        1001|1003) devices+=("$candidate") ;;
    esac
done
if [ "${#devices[@]}" -ne 1 ]; then
    echo "Expected one ErgoType remapper; found ${#devices[@]}"
    exit 1
fi
usbdev=${devices[0]}
sudo -v
old_control=$(<"$usbdev/power/control")
restore() {
    printf '%s\n' "$old_control" | sudo tee "$usbdev/power/control" >/dev/null
}
trap restore EXIT
printf 'on\n' | sudo tee "$usbdev/power/control" >/dev/null
printf '%s upstream USB: %s\n' "${usbdev##*/}" "$(<"$usbdev/power/runtime_status")"
echo "Open CDC in another terminal. Tap any letter on the downstream keyboard."
echo "Wait for its sleep LED, then tap and release one ordinary letter."
echo "After reading the result in CDC, press Enter here to restore USB policy."
read -r _ </dev/tty
SH
```

Open the remapper's usual CDC console and keep it open. The fixture's fixed
async messages do not depend on `log_level`:

1. `INFO: HID_PM_TEST_WAIT_KEY`: tap and release any letter on the downstream
   keyboard, not the laptop keyboard. This first letter can reach the laptop.
2. `INFO: HID_PM_TEST_PRESS_KEY`: the host completed suspend with remote
   wakeup enabled. Confirm the keyboard's sleep animation stays on before
   you press anything. There is no deadline for this human step.
3. Tap and release one ordinary letter. Its sleep animation must stop and
   typing must return. Expect `INFO: HID_PM_REMOTE_OK`.

The final marker requires all three: the HID-host wake-request callback,
successful downstream resume, and a nonempty keyboard report followed by an
empty report from the same interface and generation. There is no automatic
resume timer. Resume completion and restored input each have a five-second
timeout after the wake request; release the key instead of holding it.
`ERR: HID_PM_REMOTE_SUSPEND`, `ERR: HID_PM_REMOTE_RESUME`, or
`ERR: HID_PM_REMOTE_INPUT` identifies the failed stage. A warning that remote
wakeup is unsupported, spontaneous wake before the key, a reset/disconnect,
or a missing final marker is not a pass. The fixture runs once per boot.

This image implements the firmware boundary callbacks in the fixture itself;
it does not forward the request to the laptop. This qualifies only the
HID-host side, not the complete two-link wakeup chain. Rebuild with
`USBHID_PM_FIXTURE=OFF` for production forwarding and the Android test below.

## Android: complete wakeup chain without CDC

Build the normal image with `USBHID_PM_FIXTURE=OFF` in the existing `build/`
directory and flash `build/ErgoType.uf2` onto the remapper. Connect Android to
its native USB port and the real keyboard to its PIO USB port. The keyboard
keeps its existing firmware. The host-only keyboard test above has passed;
this test adds upstream suspend and remote wakeup.

1. Confirm ordinary typing works and record any existing errors.
2. Put Android to sleep. Wait for the remapper and keyboard to show their
   suspend LED patterns. One remapper pulse per period means Android allowed
   remote wakeup; two consecutive pulses mean it did not. Do not wake Android
   manually during this cycle.
3. Tap and release one ordinary letter on the downstream keyboard.
4. A pass requires Android to wake, both suspend patterns to stop, no new
   error, and ordinary typing to work again. Manual recovery is not a pass.

The complete chain passed on hardware: Android woke on a keyboard press and
both devices stopped their suspend indication. The temporary success-warning
marker has been removed; successful wakeup does not increment warnings.
PM transition failures still increment the error counter without CDC.
Transition messages are skipped while CDC is disconnected, avoiding warnings
caused solely by an undrained PM log message.

The LED step is 100 ms and the period is 32 steps. Allowed wakeup uses mask
`0x1`. Denied wakeup uses `0x5`: on, off, on, then off for the rest of the
period. This changes only the indication, not the host's permission.

## Real keyboard runtime-suspend test

This is the separate end-to-end upstream test. It has timed out on the laptop;
that result does not identify which downstream stage failed. Use the host-only
test above first. Do not use this test's `runtime_status` as the host-only verdict.

Use the normal main image, not the automatic two-board fixture:

```sh
cmake -S . -B build -DUSBHID_PM_FIXTURE=OFF
cmake --build build -j4
```

Flash `build/ErgoType.uf2`. Connect the real keyboard directly to the PIO USB
port (D+ GPIO21, D- GPIO20). The host currently accepts PM only when all bound
HID interfaces use `hid-generic`. A keyboard using a vendor driver returns
`ERR: HID_PM_SUSPEND e-95`; this is not a generic-PM test.

Keep the laptop awake. Close minicom and any other process holding the
ErgoType CDC port open: Linux may otherwise keep the whole composite device
active. Run this complete block on the laptop, not on the build server:

```sh
bash <<'SH'
set -eu
devices=()
for candidate in /sys/bus/usb/devices/*; do
    [ -r "$candidate/idVendor" ] || continue
    [ "$(<"$candidate/idVendor")" = cafe ] || continue
    case "$(<"$candidate/idProduct")" in
        1001|1003) devices+=("$candidate") ;;
    esac
done
if [ "${#devices[@]}" -ne 1 ]; then
    echo "Expected one directly connected ErgoType HID device; found ${#devices[@]}"
    exit 1
fi
usbdev=${devices[0]}
if [ ! -r "$usbdev/power/wakeup" ]; then
    echo "No remote-wakeup attribute; reconnect the newly flashed board"
    exit 1
fi
sudo -v
old_control=$(<"$usbdev/power/control")
old_delay=$(<"$usbdev/power/autosuspend_delay_ms")
old_wakeup=$(<"$usbdev/power/wakeup")
case "$old_wakeup" in
    enabled|disabled) ;;
    *) echo "Device does not advertise remote wakeup"; exit 1 ;;
esac
restore() {
    printf 'on\n' | sudo tee "$usbdev/power/control" >/dev/null
    printf '%s\n' "$old_delay" | sudo tee "$usbdev/power/autosuspend_delay_ms" >/dev/null
    printf '%s\n' "$old_wakeup" | sudo tee "$usbdev/power/wakeup" >/dev/null
    printf '%s\n' "$old_control" | sudo tee "$usbdev/power/control" >/dev/null
}
trap restore EXIT
printf 'on\n' | sudo tee "$usbdev/power/control" >/dev/null
printf 'enabled\n' | sudo tee "$usbdev/power/wakeup" >/dev/null
printf '0\n' | sudo tee "$usbdev/power/autosuspend_delay_ms" >/dev/null
printf 'auto\n' | sudo tee "$usbdev/power/control" >/dev/null
printf 'Waiting for %s to suspend...\n' "${usbdev##*/}"
for ((attempt=0; attempt<150; attempt++)); do
    [ "$(<"$usbdev/power/runtime_status")" = suspended ] && break
    sleep 0.1
done
if [ "$(<"$usbdev/power/runtime_status")" != suspended ]; then
    echo "Did not suspend; check for an open CDC port"
    exit 1
fi
echo "SUSPENDED. Wait for the downstream keyboard's sleep LED, then press ONE key."
for ((attempt=0; attempt<60; attempt++)); do
    if [ "$(<"$usbdev/power/runtime_status")" = active ]; then
        echo "ACTIVE before manual recovery; check that the key caused it"
        exit 0
    fi
    sleep 0.5
done
echo "No wake within 30 s. Restoring active state manually; this is NOT a pass."
exit 1
SH
```

Expected: the downstream keyboard starts its suspend LED pattern, then one
ordinary letter wakes the upstream USB link, its LED stops blinking, and
normal typing works. The test does not suspend the laptop or depend on its
deep-sleep firmware settings. Afterward reopen the CDC console. The transition
messages are `INFO: HID_PM_SUSPENDED` and `INFO: HID_PM_ACTIVE`, but the logger
has one async slot: while CDC is unavailable, a later message can be dropped.
Their absence cannot diagnose the failure, and their presence alone is not a pass.
CDC cannot deliver logs while its USB link is suspended. The displayed
`runtime_status` alone is not proof that the downstream keyboard slept or
that a key woke it: observe both LED changes and repeat the cycle.

If the keyboard cannot advertise remote wakeup, the host logs
`WARN: HID_PM_NO_REMOTE_WAKEUP`; ordinary suspend/manual resume still works,
but wake-on-key is unavailable. A failed cleanup CLEAR_FEATURE is reported as
`WARN: HID_PM_WAKEUP_CLEAR`; the next sleep explicitly configures the feature
again. Do not count either warning as a successful wake-on-key qualification.
