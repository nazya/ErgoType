#!/usr/bin/env python3
"""Compile the real PM fixture with task/transport stubs; no USB timing model."""

from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[2]
source = (root / "usb_host/usbhid_pm_fixture.c").read_text()
source = re.sub(r"^#include[^\n]*\n", "", source, flags=re.MULTILINE)

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <setjmp.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define HID_KEY_F1 0x3a
#define HID_KEY_A 0x04
#define HID_KEY_Z 0x1d
#define HID_NKRO_KEY_BYTES 32
#define REPORT_ID_KEYBOARD 1
#define USB_VID 0xcafe
#define USB_PID_HID 0x1001
#define USB_PID_HID_NKRO 0x1003
struct hid_device { unsigned int vendor, product; };
enum usbhid_pm_state { USBHID_PM_ACTIVE, USBHID_PM_SUSPENDED };
typedef void (*usbhid_pm_complete_t)(enum usbhid_pm_state, int, void *);
void hid_transport_lock(void);
void hid_transport_unlock(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear, TickType_t wait);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
void vTaskSuspend(TaskHandle_t task);
void async_msg(const char *message);
void usbhid_pm_set_target(enum usbhid_pm_state state, bool remote_wakeup);
int usbhid_pm_request(enum usbhid_pm_state state,
                      usbhid_pm_complete_t complete, void *context);
'''

suffix = r'''
static unsigned lock_depth, notices, waits, requests, targets, remote_step;
static TickType_t ticks;
static bool inject_input, remote_case, early_input, resume_error, wrong_generation;
static bool missing_wake;
static unsigned remote_ok, remote_failed;
static jmp_buf request_seen;
static struct hid_device device = {0xcafe, 0x11f0};
static const u8 down[8] = {0, 0, HID_KEY_F1, 0, 0, 0, 0, 0};
static const u8 up[8];
static struct hid_device real_device;
static u8 real_down[33], real_up[33], real_letter[33];
static unsigned real_len;

static void deliver_remote_input(void) {
    u32 generation = wrong_generation ? 43 : 42;
    usbhid_pm_fixture_input(&real_device, generation, real_letter, real_len);
    usbhid_pm_fixture_input(&real_device, generation, real_up, real_len);
}

void hid_transport_lock(void) { assert(lock_depth == 0); lock_depth++; }
void hid_transport_unlock(void) { lock_depth--; assert(lock_depth == 0); }
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (TaskHandle_t)1; }
TickType_t xTaskGetTickCount(void) { return ticks; }
void xTaskNotifyGive(TaskHandle_t task) {
    assert(task == (TaskHandle_t)1 && lock_depth == 0);
    notices++;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t wait) {
    if (!notices && wait) {
        if (remote_case) {
            if (remote_step == 0) {
                assert(wait == portMAX_DELAY);
                if (missing_wake) {
                    usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, 0);
                    deliver_remote_input();
                } else {
                    usbhid_pm_wakeup_cb();
                }
                remote_step++;
            } else if (missing_wake) {
                assert(wait == portMAX_DELAY && !pm_fixture.wake_requested);
                assert(!remote_ok && !pm_fixture.wake_input_returned);
                longjmp(request_seen, 1);
            } else if (remote_step == 1) {
                assert(wait == PM_FIXTURE_CALLBACK_TIMEOUT_MS);
                usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE,
                                           resume_error ? -EIO : 0);
                if (!resume_error) deliver_remote_input();
                remote_step++;
            } else {
                assert(wrong_generation && wait != portMAX_DELAY);
                ticks += wait;
            }
        } else {
            assert(wait == portMAX_DELAY && inject_input && requests == 0);
            waits++;
            ticks = 30000;
            inject_input = false;
            usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
            usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
        }
    }
    uint32_t result = notices;
    if (clear) notices = 0;
    return result;
}
void vTaskDelay(TickType_t delay) { (void)delay; abort(); }
void vTaskDelete(TaskHandle_t task) { (void)task; abort(); }
void vTaskSuspend(TaskHandle_t task) {
    assert(!task && remote_case && targets == 2);
    longjmp(request_seen, 1);
}
void async_msg(const char *message) {
    if (!strcmp(message, "INFO: HID_PM_TEST_WAIT_KEY") ||
        !strcmp(message, "INFO: HID_PM_TEST_PRESS_KEY")) return;
    if (!strcmp(message, "INFO: HID_PM_REMOTE_OK")) { remote_ok++; return; }
    if (!strcmp(message, "ERR: HID_PM_REMOTE_RESUME")) {
        assert(resume_error); remote_failed++; return;
    }
    if (!strcmp(message, "ERR: HID_PM_REMOTE_INPUT")) {
        assert(wrong_generation); remote_failed++; return;
    }
    fputs(message, stderr); abort();
}
void usbhid_pm_set_target(enum usbhid_pm_state state, bool remote_wakeup) {
    assert(remote_case && !lock_depth && pm_fixture.input_ready);
    targets++;
    if (state == USBHID_PM_SUSPENDED) {
        assert(targets == 1 && remote_wakeup);
        usbhid_pm_state_changed_cb(USBHID_PM_SUSPENDED, 0);
        if (early_input) {
            usbhid_pm_wakeup_cb();
            usbhid_pm_state_changed_cb(USBHID_PM_ACTIVE, 0);
            deliver_remote_input();
        }
    } else {
        assert(targets == 2 && !remote_wakeup);
    }
}
int usbhid_pm_request(enum usbhid_pm_state state,
                      usbhid_pm_complete_t complete, void *context) {
    (void)complete;
    (void)context;
    assert(state == USBHID_PM_SUSPENDED && pm_fixture.input_ready);
    assert(lock_depth == 0 && notices == 0);
    requests++;
    longjmp(request_seen, 1);
}

static void reset(void) {
    memset(&pm_fixture, 0, sizeof(pm_fixture));
    lock_depth = notices = waits = requests = targets = remote_step = 0;
    remote_ok = remote_failed = 0;
    ticks = 0;
    inject_input = false;
    remote_case = early_input = resume_error = wrong_generation = false;
    missing_wake = false;
}

static void check_reports(void) {
    struct hid_device wrong_vendor = {0x1234, 0x11f0};
    struct hid_device wrong_product = {0xcafe, 0x11f1};
    const u8 other[8] = {0, 0, 4, 0, 0, 0, 0, 0};

    reset();
    pm_fixture.task = (TaskHandle_t)1;
    usbhid_pm_fixture_input(&wrong_vendor, 42, down, sizeof(down));
    usbhid_pm_fixture_input(&wrong_product, 42, down, sizeof(down));
    usbhid_pm_fixture_input(&device, 42, down, sizeof(down) - 1);
    usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
    assert(!pm_fixture.input_ready && notices == 0);

    usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
    assert(!pm_fixture.input_ready && notices == 0);
    usbhid_pm_fixture_input(&device, 43, up, sizeof(up));
    usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
    assert(!pm_fixture.input_ready && notices == 0);

    usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
    usbhid_pm_fixture_input(&device, 42, other, sizeof(other));
    usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
    assert(!pm_fixture.input_ready && notices == 0);

    usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
    usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
    assert(pm_fixture.input_ready && notices == 1);
    pm_fixture.task = (TaskHandle_t)2;
    usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
    usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
    assert(notices == 1);
}

static void check_start(bool input_before_task, bool unrelated_notice) {
    reset();
    if (input_before_task) {
        usbhid_pm_fixture_input(&device, 42, down, sizeof(down));
        usbhid_pm_fixture_input(&device, 42, up, sizeof(up));
        assert(pm_fixture.input_ready && notices == 0);
    }
    inject_input = !input_before_task;
    notices = unrelated_notice ? 1 : 0;
    if (!setjmp(request_seen))
        usbhid_pm_fixture_task(NULL);
    assert(requests == 1 && waits == (input_before_task ? 0u : 1u));
    assert(input_before_task || ticks == 30000);
}

static void check_remote(bool nkro, bool early, bool fail_resume, bool stale,
                         bool no_wake) {
    reset();
    remote_case = true;
    early_input = early;
    resume_error = fail_resume;
    wrong_generation = stale;
    missing_wake = no_wake;
    real_device = (struct hid_device){USB_VID,
        nkro ? USB_PID_HID_NKRO : USB_PID_HID};
    memset(real_down, 0, sizeof(real_down));
    memset(real_up, 0, sizeof(real_up));
    memset(real_letter, 0, sizeof(real_letter));
    real_len = nkro ? sizeof(real_down) : 8;
    if (nkro) {
        real_down[0] = real_up[0] = real_letter[0] = REPORT_ID_KEYBOARD;
        real_down[1 + HID_KEY_A / 8] = 1u << (HID_KEY_A % 8);
        real_letter[1 + 0x0f / 8] = 1u << (0x0f % 8);
    } else {
        real_down[2] = HID_KEY_A;
        real_letter[2] = 0x0f;
    }
    usbhid_pm_fixture_input(&real_device, 42, real_down, real_len);
    usbhid_pm_fixture_input(&real_device, 42, real_up, real_len);
    assert(pm_fixture.input_ready && pm_fixture.remote);
    if (!setjmp(request_seen)) usbhid_pm_fixture_task(NULL);
    if (no_wake) {
        assert(!requests && targets == 1 && !remote_ok && !remote_failed);
        return;
    }
    assert(!requests && targets == 2 && pm_fixture.wake_requested);
    assert(remote_ok == (fail_resume || stale ? 0u : 1u));
    assert(remote_failed == (fail_resume || stale ? 1u : 0u));
    assert(pm_fixture.wake_input_returned == (!fail_resume && !stale));
}

static void check_letter_trigger(bool nkro) {
    struct hid_device keyboard = {USB_VID,
        nkro ? USB_PID_HID_NKRO : USB_PID_HID};
    const u8 usages[] = {HID_KEY_A, HID_KEY_Z, 0x0f, 0x1e};
    for (unsigned i = 0; i < sizeof(usages); i++) {
        u8 press[33] = {0}, release[33] = {0};
        unsigned len = nkro ? sizeof(press) : 8;
        reset();
        if (nkro) {
            press[0] = release[0] = REPORT_ID_KEYBOARD;
            press[1 + usages[i] / 8] = 1u << (usages[i] % 8);
        } else {
            press[2] = usages[i];
        }
        usbhid_pm_fixture_input(&keyboard, 42, press, len);
        assert(!pm_fixture.input_ready);
        usbhid_pm_fixture_input(&keyboard, 42, release, len);
        assert(pm_fixture.input_ready == (usages[i] != 0x1e));
    }
}

int main(void) {
    check_reports();
    check_start(false, false);
    check_start(true, false);
    check_start(false, true);
    check_start(true, true);
    check_letter_trigger(false);
    check_letter_trigger(true);
    check_remote(false, false, false, false, false);
    check_remote(true, false, false, false, false);
    check_remote(true, true, false, false, false);
    check_remote(true, false, true, false, false);
    check_remote(true, false, false, true, false);
    check_remote(true, false, false, false, true);
    puts("PASS: real PM fixture waits for same-generation F1 down/up; "
         "late/pre-task input, wrong report; boot/NKRO letter trigger and remote cycle, "
         "early completion, resume error, stale generation and "
         "resume/input without wake callback cannot pass");
}
'''

binary = root / "build/pm-fixture-start-check"
if not binary.parent.is_dir():
    raise SystemExit("Configure the existing main build/ first")
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c",
                "-", "-o", str(binary)],
               input=prefix + source + suffix, text=True, check=True)
subprocess.run([str(binary)], check=True, timeout=5)
