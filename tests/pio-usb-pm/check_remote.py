#!/usr/bin/env python3
"""Compile production PM/IRQ functions with native stubs, not USB hardware."""

from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
host = (root / "Pico-PIO-USB/src/pio_usb_host.c").read_text()
hid = (root / "usb_host/usbhid.c").read_text()


def function(source, name):
    pattern = (r"^(?:static )?(?:void|bool|int|uint8_t) "
               r"(?:__no_inline_not_in_flash_func\(" + name + r"\)|" + name +
               r")\([^;{}]*\)\s*\{")
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise RuntimeError(f"Cannot extract production function {name}")
    cursor, depth = match.end(), 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor] + "\n"


def run(name, source):
    binary = root / "build" / name
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-", "-o", str(binary)],
                   input=source, text=True, check=True)
    subprocess.run([str(binary)], check=True)


pio_types = host[host.index("enum pio_usb_host_port_pm_state {"):
                 host.index("static void pio_usb_host_wakeup_irq(void);")]
pio_constants = re.search(r"enum \{\s*PIO_USB_HOST_RESUME_SIGNAL_US.*?\};",
                          host, re.DOTALL)[0]
pio_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef unsigned uint;
typedef uint8_t port_pin_status_t;
typedef struct { bool initialized, connected, suspended, is_fullspeed;
                 uint8_t pin_dp, pin_dm; } root_port_t;
typedef struct { int unused; } pio_port_t;
typedef struct { uint8_t root_idx; unsigned size;
                 bool has_transfer, transfer_started; } endpoint_t;
#define __no_inline_not_in_flash_func(name) name
#define PIO_USB_ROOT_PORT_CNT 1
#define PIO_USB_EP_POOL_CNT 1
#define GPIO_IRQ_EDGE_RISE 8u
#define PORT_PIN_FS_IDLE 1
#define PORT_PIN_LS_IDLE 2
static root_port_t roots[1];
static pio_port_t pio;
static endpoint_t endpoints[1];
#define PIO_USB_ROOT_PORT(i) (&roots[i])
#define PIO_USB_PIO_PORT(i) (&pio)
#define PIO_USB_ENDPOINT(i) (&endpoints[i])
static uint32_t now, enabled[48], pending[48];
static unsigned starts, ends, remote_edges, completions;
static bool end_ok;
static port_pin_status_t line;
static uint32_t get_time_us_32(void) { return now; }
static uint32_t save_and_disable_interrupts(void) { return 0; }
static void restore_interrupts(uint32_t state) { (void)state; }
static void gpio_set_irq_enabled(uint pin, uint32_t mask, bool on) {
    pending[pin] &= ~mask;
    if (on) enabled[pin] |= mask; else enabled[pin] &= ~mask;
}
static uint32_t gpio_get_irq_event_mask(uint pin) {
    return pending[pin] & enabled[pin];
}
static void gpio_acknowledge_irq(uint pin, uint32_t mask) { pending[pin] &= ~mask; }
static port_pin_status_t pio_usb_bus_get_line_state(root_port_t *root) {
    (void)root; return line;
}
static bool connection_check(root_port_t *root) { return root->connected; }
static void pio_usb_host_resume_signal_start(pio_port_t *port, root_port_t *root) {
    (void)port; (void)root; starts++;
}
static bool pio_usb_host_resume_signal_end(pio_port_t *port, root_port_t *root) {
    (void)port; (void)root; ends++; return end_ok;
}
static void pio_usb_host_port_pm_changed_cb(uint8_t index, bool remote) {
    assert(index == 0);
    if (remote) remote_edges++; else completions++;
}
'''
pio_code = "".join(function(host, name) for name in (
    "pio_usb_host_resume_begin", "pio_usb_host_wakeup_irq",
    "pio_usb_host_port_suspend", "pio_usb_host_port_resume",
    "pio_usb_host_port_pm_step"))
pio_checks = r'''
static void reset(bool fullspeed, bool reversed) {
    memset(roots, 0, sizeof(roots));
    memset(endpoints, 0, sizeof(endpoints));
    memset(enabled, 0, sizeof(enabled));
    memset(pending, 0, sizeof(pending));
    roots[0] = (root_port_t){.initialized=true, .connected=true,
        .is_fullspeed=fullspeed, .pin_dp=reversed ? 21 : 20,
        .pin_dm=reversed ? 20 : 21};
    port_pm_state[0] = PIO_USB_HOST_PORT_ACTIVE;
    port_pm_remote_enabled[0] = false;
    now = starts = ends = remote_edges = completions = 0;
    line = fullspeed ? PORT_PIN_FS_IDLE : PORT_PIN_LS_IDLE;
    end_ok = true;
}
static void device_k(void) {
    uint pin = roots[0].is_fullspeed ? roots[0].pin_dm : roots[0].pin_dp;
    line = roots[0].is_fullspeed ? PORT_PIN_LS_IDLE : PORT_PIN_FS_IDLE;
    pending[pin] |= GPIO_IRQ_EDGE_RISE;
    pio_usb_host_wakeup_irq();
}
static void check_cycle(bool fullspeed, bool reversed, bool wrapping) {
    reset(fullspeed, reversed);
    if (wrapping) now = UINT32_MAX - 100;
    bool accepted = pio_usb_host_port_suspend(0, true);
    assert(accepted);
    uint const k_pin = fullspeed ? roots[0].pin_dm : roots[0].pin_dp;
    uint const j_pin = fullspeed ? roots[0].pin_dp : roots[0].pin_dm;
    assert(enabled[k_pin] && !enabled[j_pin]);
    device_k();
    assert(starts == 1 && remote_edges == 1);
    assert(port_pm_state[0] == PIO_USB_HOST_PORT_RESUME_SIGNAL);
    assert(!enabled[k_pin] && !enabled[j_pin]);
    device_k();
    assert(starts == 1 && remote_edges == 1);
    accepted = pio_usb_host_port_resume(0);
    assert(accepted && starts == 1);
    now += 39999;
    uint8_t state = pio_usb_host_port_pm_step(&pio, roots, 0);
    assert(state == PIO_USB_HOST_PORT_RESUME_SIGNAL && !ends);
    now++;
    state = pio_usb_host_port_pm_step(&pio, roots, 0);
    assert(state == PIO_USB_HOST_PORT_RESUME_RECOVERY && ends == 1);
    assert(completions == 0);
    now += 9999;
    state = pio_usb_host_port_pm_step(&pio, roots, 0);
    assert(state == PIO_USB_HOST_PORT_RESUME_RECOVERY);
    now++;
    state = pio_usb_host_port_pm_step(&pio, roots, 0);
    assert(state == PIO_USB_HOST_PORT_ACTIVE && completions == 1);
    accepted = pio_usb_host_port_resume(0);
    assert(accepted && starts == 1);
}
int main(void) {
    for (unsigned full = 0; full < 2; full++)
        for (unsigned reversed = 0; reversed < 2; reversed++)
            check_cycle(full, reversed, true);
    reset(true, false);
    bool accepted = pio_usb_host_port_suspend(0, false);
    assert(accepted);
    device_k();
    assert(!starts && !remote_edges);
    accepted = pio_usb_host_port_resume(0);
    assert(accepted && starts == 1 && !remote_edges);
    reset(true, true);
    endpoints[0].size = 8;
    endpoints[0].has_transfer = true;
    accepted = pio_usb_host_port_suspend(0, true);
    assert(!accepted && !enabled[20] && !enabled[21]);
    reset(false, false);
    accepted = pio_usb_host_port_suspend(0, true);
    assert(accepted);
    pending[20] = GPIO_IRQ_EDGE_RISE;
    line = 3;
    pio_usb_host_wakeup_irq();
    assert(!starts && !remote_edges);
    device_k();
    end_ok = false;
    now += 40000;
    uint8_t state = pio_usb_host_port_pm_step(&pio, roots, 0);
    assert(state == PIO_USB_HOST_PORT_RESUME_FAILED && completions == 1);
    accepted = pio_usb_host_port_resume(0);
    assert(!accepted);
    puts("PASS: real PIO IRQ/PM: FS/LS, both pin orders, one-shot K takeover, "
         "permission, active-I/O rejection, invalid line, timeout, timer wrap");
}
'''
run("pm-remote-irq-check", pio_prefix + pio_types + pio_constants +
    pio_code + pio_checks)

policy_types = hid[hid.index("enum usbhid_pm_phase {"):
                   hid.index("struct usbhid_usb_device {")]
policy_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
typedef uint32_t u32;
#define HID_HOST_MAX_DEVICES 2
enum usbhid_pm_state { USBHID_PM_ACTIVE, USBHID_PM_SUSPENDED };
typedef void (*usbhid_pm_complete_t)(enum usbhid_pm_state, int, void *);
struct hid_device { int unused; };
static unsigned locked, kicks, callbacks, wake_requests;
static int collect_status, observed_status;
static enum usbhid_pm_state observed_state;
static void hid_transport_lock(void) { assert(!locked); locked++; }
static void hid_transport_unlock(void) { locked--; assert(!locked); }
static void usbhid_lifecycle_kick(void) { kicks++; }
static int usbhid_pm_collect_generic_locked(struct hid_device **devices,
                                           size_t *count) {
    (void)devices;
    assert(locked); *count = collect_status ? 0 : 1; return collect_status;
}
static void usbhid_pm_state_changed_cb(enum usbhid_pm_state state, int status) {
    assert(!locked); callbacks++; observed_state=state; observed_status=status;
}
static void usbhid_pm_wakeup_cb(void) {
    assert(!locked); wake_requests++;
}
'''
policy_storage = r'''
static struct usbhid_pm_coordinator coordinator;
#define usbhid_pm (&coordinator)
'''
policy_code = "".join(function(hid, name) for name in (
    "usbhid_pm_set_target", "usbhid_pm_reconcile_target", "usbhid_pm_finish"))
policy_checks = r'''
static void reset(void) {
    memset(&coordinator, 0, sizeof(coordinator));
    atomic_store(&usbhid_pm_target, 0);
    atomic_store(&usbhid_pm_remote_wakeup, false);
    locked=kicks=callbacks=wake_requests=0; collect_status=0;
}
int main(void) {
    reset();
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE && !callbacks);

    reset();
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPEND_PENDING);
    assert(coordinator.target & USBHID_PM_TARGET_REMOTE);
    coordinator.phase = USBHID_PM_PHASE_SUSPENDING;
    usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPENDING);
    usbhid_pm_finish(USBHID_PM_SUSPENDED, 0, USBHID_PM_PHASE_SUSPENDED);
    assert(observed_state == USBHID_PM_SUSPENDED && !observed_status);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_RESUME_PENDING);

    reset();
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    usbhid_pm_finish(USBHID_PM_SUSPENDED, 0, USBHID_PM_PHASE_SUSPENDED);
    atomic_store(&usbhid_pm_remote_wakeup, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_RESUME_PENDING);
    assert(wake_requests == 1);
    usbhid_pm_finish(USBHID_PM_ACTIVE, 0, USBHID_PM_PHASE_ACTIVE);
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE);
    usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPEND_PENDING);

    reset();
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    usbhid_pm_finish(USBHID_PM_SUSPENDED, 0, USBHID_PM_PHASE_SUSPENDED);
    atomic_store(&usbhid_pm_remote_wakeup, true);
    usbhid_pm_set_target(USBHID_PM_ACTIVE, false);
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_RESUME_PENDING);
    usbhid_pm_finish(USBHID_PM_ACTIVE, 0, USBHID_PM_PHASE_ACTIVE);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPEND_PENDING);

    reset();
    collect_status = -ENODEV;
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE && !callbacks);
    collect_status = -EBUSY;
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE && !callbacks);
    collect_status = 0;
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPEND_PENDING);
    usbhid_pm_finish(USBHID_PM_SUSPENDED, -EBUSY, USBHID_PM_PHASE_ACTIVE);
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE);
    coordinator.policy_retry = true;
    usbhid_pm_reconcile_target();
    assert(coordinator.phase == USBHID_PM_PHASE_SUSPEND_PENDING);

    reset();
    collect_status = -EOPNOTSUPP;
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    usbhid_pm_reconcile_target();
    coordinator.policy_retry = true;
    usbhid_pm_reconcile_target();
    assert(callbacks == 1 && observed_status == -EOPNOTSUPP);
    assert(coordinator.phase == USBHID_PM_PHASE_ACTIVE);
    assert(!wake_requests);

    reset();
    usbhid_pm_set_target(USBHID_PM_SUSPENDED, true);
    usbhid_pm_reconcile_target();
    usbhid_pm_finish(USBHID_PM_SUSPENDED, 0, USBHID_PM_PHASE_SUSPENDED);
    atomic_store(&usbhid_pm_remote_wakeup, true);
    usbhid_pm_reconcile_target();
    assert(wake_requests == 1 && callbacks == 1);
    usbhid_pm_finish(USBHID_PM_ACTIVE, -EIO, USBHID_PM_PHASE_ACTIVE);
    usbhid_pm_reconcile_target();
    assert(wake_requests == 1 && observed_status == -EIO);
    puts("PASS: real desired-state mailbox: overwrite, in-flight resume, "
         "remote-wake latch, next sleep episode, late host readiness, "
         "event-only busy retry, permanent error coalescing, "
         "wake callback before resume result and outside transport lock");
}
'''
run("pm-target-check", policy_prefix + policy_types + policy_storage +
    policy_code + policy_checks)

device = (root / "tusb/device_callbacks.c").read_text()
owner = (root / "tusb/device.c").read_text()
bridge_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
enum usbhid_pm_state { USBHID_PM_ACTIVE, USBHID_PM_SUSPENDED };
#define ASYNC_MSG_BUFSIZE 32
#define HID 1
#define MSC 2
#define LED_PATTERN_FAST 1
static uint8_t mode = HID;
static void (*deferred)(void *);
static unsigned attempts, queued, errors, messages, led_pattern;
static bool suspended, permission, on_device_task, cdc_connected;
static bool tud_cdc_connected(void) { return cdc_connected; }
static bool tud_remote_wakeup(void) {
    assert(on_device_task); attempts++; return suspended && permission;
}
static void usbd_defer_func(void (*func)(void *), void *context, bool in_isr) {
    assert(!context && !in_isr && !on_device_task);
    deferred = func; queued++;
}
static void _async_msg(const char *message) { (void)message; messages++; }
#define async_msg(message) _async_msg(message)
static void ui_notify_err(void) { errors++; }
static void ui_led_set_pattern(unsigned led, unsigned pattern, bool loop) {
    assert(!led && loop); led_pattern = pattern;
}
static void webhid_reset(void) { }
static void usbhid_pm_set_target(enum usbhid_pm_state state, bool remote) {
    (void)state; (void)remote;
}
'''
bridge_storage = "\n".join(re.findall(
    r"^static bool (?:bus_suspended|wakeup_sent);", owner, re.MULTILINE)) + "\n"
bridge_code = "".join(function(owner, name) for name in (
    "usb_device_on_suspend", "usb_device_on_resume",
    "usb_device_is_suspended", "usb_device_request_wakeup"))
bridge_code += "".join(function(device, name) for name in (
    "usb_device_wakeup", "usbhid_pm_wakeup_cb", "usbhid_pm_state_changed_cb",
    "tud_mount_cb", "tud_umount_cb", "tud_suspend_cb", "tud_resume_cb"))
bridge_checks = r'''
static void dispatch(void) {
    assert(deferred);
    on_device_task = true;
    deferred(NULL);
    on_device_task = false;
}
static void suspend_link(bool allow) {
    suspended = true;
    permission = allow;
    on_device_task = true;
    tud_suspend_cb(allow);
    on_device_task = false;
}
static void resume_link(void) {
    suspended = false;
    on_device_task = true;
    tud_resume_cb();
    on_device_task = false;
}
int main(void) {
    usbhid_pm_state_changed_cb(USBHID_PM_SUSPENDED, 0);
    usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, 0);
    usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, -EIO);
    assert(!queued && !attempts && errors == 1 && !messages);

    suspended = permission = true;
    usbhid_pm_wakeup_cb();
    assert(queued == 1 && !attempts);
    dispatch();
    assert(!attempts);
    suspend_link(true);
    assert(led_pattern == 1);
    dispatch();
    assert(attempts == 1 && wakeup_sent);
    dispatch();
    assert(attempts == 1);
    resume_link();
    assert(!bus_suspended && !led_pattern);
    dispatch();
    assert(attempts == 1);

    suspend_link(false);
    assert(led_pattern == 5);
    dispatch();
    assert(attempts == 2 && !wakeup_sent);
    resume_link();

    suspend_link(true);
    dispatch();
    assert(attempts == 3 && wakeup_sent);
    resume_link();

    cdc_connected = true;
    usbhid_pm_state_changed_cb(USBHID_PM_SUSPENDED, 0);
    usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, 0);
    usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, -EIO);
    assert(messages == 3 && errors == 2);
    puts("PASS: real firmware bridge: wake deferred to TUD and gated by "
         "delivered suspend; one accepted request per sleep; "
         "allowed/denied LED masks 1/5; PM errors count without CDC");
}
'''
run("pm-wakeup-bridge-check", bridge_prefix + bridge_storage + bridge_code +
    bridge_checks)

control_prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
struct usb_device { int unused; };
typedef struct __attribute__((packed)) {
    uint8_t bLength, bDescriptorType; uint16_t wTotalLength;
    uint8_t bNumInterfaces, bConfigurationValue, iConfiguration;
    uint8_t bmAttributes, bMaxPower;
} tusb_desc_configuration_t;
struct { bool wakeup_armed; } coordinator;
#define usbhid_pm (&coordinator)
#define USB_DIR_IN 0x80
#define USB_DIR_OUT 0
#define USB_TYPE_STANDARD 0
#define USB_RECIP_DEVICE 0
#define USB_REQ_GET_DESCRIPTOR 6
#define TUSB_REQ_SET_FEATURE 3
#define TUSB_REQ_CLEAR_FEATURE 1
#define TUSB_REQ_FEATURE_REMOTE_WAKEUP 1
#define TUSB_DESC_CONFIGURATION 2
#define TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP 0x20
#define USB_CTRL_SET_TIMEOUT 5000
#define USB_CTRL_GET_TIMEOUT 5000
static unsigned requests, warnings;
static bool capable, malformed, short_read;
static int control_status, header_status;
static int usb_sndctrlpipe(struct usb_device *dev, int ep) {
    (void)dev; (void)ep; return 0;
}
static int usb_rcvctrlpipe(struct usb_device *dev, int ep) {
    (void)dev; (void)ep; return USB_DIR_IN;
}
static void async_msg(const char *message) { (void)message; warnings++; }
static int usb_control_msg(struct usb_device *dev, unsigned pipe,
                           uint8_t request, uint8_t type, uint16_t value,
                           uint16_t index, void *data, uint16_t len, int timeout) {
    (void)dev;
    assert(index == 0 && timeout == 5000);
    if (request == USB_REQ_GET_DESCRIPTOR) {
        assert(type == USB_DIR_IN && pipe == USB_DIR_IN);
        assert(value == 0x0200 && len == 9 && data);
        if (header_status) return header_status;
        *(tusb_desc_configuration_t *)data = (tusb_desc_configuration_t){
            .bLength=malformed ? 8 : 9, .bDescriptorType=2,
            .bmAttributes=capable ? 0xa0 : 0x80};
        return short_read ? 8 : 9;
    }
    assert(type == 0 && pipe == 0 && value == 1 && !len && !data);
    assert(request == TUSB_REQ_SET_FEATURE || request == TUSB_REQ_CLEAR_FEATURE);
    requests = request;
    return control_status;
}
'''
control_code = "".join(function(hid, name) for name in (
    "usbhid_pm_set_remote_wakeup", "usbhid_pm_configure_remote_wakeup"))
control_checks = r'''
int main(void) {
    struct usb_device dev = {0};
    capable = true;
    int ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(!ret && coordinator.wakeup_armed && requests == TUSB_REQ_SET_FEATURE);
    ret = usbhid_pm_configure_remote_wakeup(&dev, false);
    assert(!ret && !coordinator.wakeup_armed && requests == TUSB_REQ_CLEAR_FEATURE);
    capable = false; requests = 0;
    ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(!ret && !coordinator.wakeup_armed && !requests && warnings == 1);
    ret = usbhid_pm_configure_remote_wakeup(&dev, false);
    assert(!ret && !requests && warnings == 1);
    capable = true; malformed = true;
    ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(ret == -EPROTO && !requests);
    malformed = false; short_read = true;
    ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(ret == -EPROTO && !requests);
    short_read = false; header_status = -ENODEV;
    ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(ret == -ENODEV && !requests);
    header_status = 0; control_status = -EPIPE;
    ret = usbhid_pm_configure_remote_wakeup(&dev, true);
    assert(ret == -EPIPE && !coordinator.wakeup_armed);
    puts("PASS: real remote-feature EP0: SET/CLEAR, unsupported device, "
         "short/malformed configuration, unplug, rejected feature");
}
'''
run("pm-remote-control-check", control_prefix + control_code + control_checks)
