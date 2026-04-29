#include "stdio_tusb_cdc.h"

#include "tusb.h"

#include "device/usbd_pvt.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

extern SemaphoreHandle_t stdio_tusb_cdc_mutex;

// Some host terminals/drivers drop the first bytes right after they open the CDC port
// (DTR goes high). If the drop happens mid ANSI escape sequence, the remainder shows
// up as visible "artifacts" like `4mErgoType/...` (tail of "\033[34m").
//
// Delay the first transmit a little after a connect edge so the host has time to settle.
#define CDC_SETTLE_MS 50u

#define POLL_TMP_BUFSIZE 256u

// Optional producer throttling when the pending ring is close to full.
// Helps slow producer a bit so USB task can drain, reducing drops.
// All values in ticks/bytes (no ms conversion inside).
#define THROTTLE_MIN_FREE_BUFSIZE 512u
#define THROTTLE_WAIT_TICKS 1u
#define THROTTLE_MAX_WAIT_TICKS 99u

static uint8_t pending_buf[BUFSIZE];
static uint32_t pending_wr;
static uint32_t pending_rd;
static uint32_t pending_count;

static bool cdc_was_connected;
static bool cdc_settled;
static bool cdc_settled_once;
static TimerHandle_t cdc_settle_timer;

static void stdio_tusb_cdc_kick_cb(void *);

static void stdio_tusb_cdc_throttle_until_free(size_t free_target)
{
    // No blocking/yielding from ISRs or when scheduler isn't running.
    if (portCHECK_IF_IN_ISR())
        return;
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
        return;

	if (!tud_inited())
		return;
	if (!tud_cdc_connected() && cdc_settled_once)
		return;

    TickType_t waited = 0;

	usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);

    while (1) {
        size_t free_bytes = BUFSIZE - pending_count;
		if (stdio_tusb_cdc_mutex) {
			xSemaphoreTake(stdio_tusb_cdc_mutex, portMAX_DELAY);
			free_bytes = BUFSIZE - pending_count;
			xSemaphoreGive(stdio_tusb_cdc_mutex);
		}
		if (free_bytes >= free_target)
			break;

        if (waited >= THROTTLE_MAX_WAIT_TICKS)
            break;

        vTaskDelay(THROTTLE_WAIT_TICKS);
        waited += THROTTLE_WAIT_TICKS;
    }
}

static void stdio_tusb_cdc_settle_timer_cb(TimerHandle_t)
{
    // Wake the TinyUSB device task so it can re-run stdio_tusb_cdc_poll()
    // after the settle delay, even if there are no new USB events.
	if (!tud_inited())
		return;
	if (!tud_cdc_connected())
		return;

	cdc_settled = true;
	cdc_settled_once = true;

	usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
}

static void stdio_tusb_cdc_kick_cb(void *arg)
{
	(void)arg;
	// No-op: waking `tud_task()` is enough. The USB task calls `stdio_tusb_cdc_poll()`
	// immediately after `tud_task()` returns (see `tusb_device_task.c`).
}

void stdio_tusb_cdc_write(const void *buf, size_t length)
{
    const uint8_t *src = (const uint8_t *)buf;

    const bool lock_safe = (stdio_tusb_cdc_mutex != NULL) &&
                            !portCHECK_IF_IN_ISR() &&
                            (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    if (!lock_safe)
        return;

    if (length > BUFSIZE)
        length = BUFSIZE;


    if ((BUFSIZE - pending_count) < THROTTLE_MIN_FREE_BUFSIZE) {
        stdio_tusb_cdc_throttle_until_free(THROTTLE_MIN_FREE_BUFSIZE);
    }
    if ((BUFSIZE - pending_count) < length) {
        stdio_tusb_cdc_throttle_until_free(length);
    }

    // If chunk still doesn't fit, drop the freshest bytes (the incoming chunk) and emit a DROP marker.
    // To make room for the marker we may trim newest buffered bytes back to a newline boundary.
    if ((BUFSIZE - pending_count) < length) {
        if (tud_inited())
            usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
        return;
    }

    xSemaphoreTake(stdio_tusb_cdc_mutex, portMAX_DELAY);
    for (size_t i = 0; i < length; ++i) {
        // if ((BUFSIZE - pending_count) < THROTTLE_MIN_FREE_BUFSIZE) {
        //     xSemaphoreGive(stdio_tusb_cdc_mutex);
        //     stdio_tusb_cdc_throttle_until_free(THROTTLE_MIN_FREE_BUFSIZE);
        //     xSemaphoreTake(stdio_tusb_cdc_mutex, portMAX_DELAY);
        // }
        pending_buf[pending_wr] = src[i];
        pending_wr = (pending_wr + 1u) % BUFSIZE;
        if (pending_count < BUFSIZE)
            pending_count++;
    }
    xSemaphoreGive(stdio_tusb_cdc_mutex);

    // Wake the TinyUSB device task (blocked in `tud_task()`) so it can run stdio_tusb_cdc_poll().
    // Do this through TinyUSB's own event queue (USBD_EVENT_FUNC_CALL) to avoid RTOS cross-signals here.
    if (tud_inited()) {
        usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
    }
}

void stdio_tusb_cdc_poll(void)
{
    const bool lock_safe = (stdio_tusb_cdc_mutex != NULL) &&
                          !portCHECK_IF_IN_ISR() &&
                          (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);

    // Only send when host has opened the port (DTR=1).
    // Until then we keep buffering (no truncation, no overwritable TinyUSB FIFO).
    if (!tud_cdc_connected()) {
        cdc_was_connected = false;
        cdc_settled = false;
        if (cdc_settle_timer)
            (void)xTimerStop(cdc_settle_timer, 0);
        return;
    }

    // Edge detect "port opened" and wait a bit before the first flush.
    if (!cdc_was_connected) {
        cdc_was_connected = true;
        cdc_settled = false;

        const TickType_t settle_ticks = pdMS_TO_TICKS(CDC_SETTLE_MS);
        if (!cdc_settle_timer) {
            cdc_settle_timer = xTimerCreate("cdc_settle",
                                            settle_ticks,
                                            pdFALSE,
                                            NULL,
                                            stdio_tusb_cdc_settle_timer_cb);
        }

        if (cdc_settle_timer) {
            // (Re)arm one-shot kick so we flush even if tud_task() blocks with no events.
            (void)xTimerChangePeriod(cdc_settle_timer, settle_ticks, 0);
            (void)xTimerReset(cdc_settle_timer, 0);
        }

        return;
    }

    if (!cdc_settled)
        return;

    // Move data from pending ring to TinyUSB CDC FIFO.
    // Keep chunks small to avoid long critical sections and to coexist with other USB classes.
    uint32_t wrote_bytes = 0;
    while (1) {
        uint32_t n = tud_cdc_write_available();
        if (!n)
            break;

        if (lock_safe)
            xSemaphoreTake(stdio_tusb_cdc_mutex, portMAX_DELAY);

        if (!pending_count) {
            if (lock_safe)
                xSemaphoreGive(stdio_tusb_cdc_mutex);
            break;
        }

        uint8_t tmp[POLL_TMP_BUFSIZE];
        if (n > POLL_TMP_BUFSIZE)
            n = POLL_TMP_BUFSIZE;

        if (n > pending_count)
            n = pending_count;

        uint32_t rd = pending_rd;
        for (uint32_t i = 0; i < n; ++i) {
            tmp[i] = pending_buf[rd];
            rd = (rd + 1u) % BUFSIZE;
        }

        const uint32_t written = tud_cdc_write(tmp, n);
        wrote_bytes += written;

        pending_rd = (pending_rd + written) % BUFSIZE;
        pending_count -= written;

        if (lock_safe)
            xSemaphoreGive(stdio_tusb_cdc_mutex);

        tud_cdc_write_flush();

        // If tud_cdc_write() wrote fewer bytes than requested, stop and wait for TX completion to free more room.
        if (written < n)
            break;
    }

	// If we made progress and still have buffered bytes, queue a follow-up poll.
	// This helps drain quickly when TinyUSB keeps some write space available without waiting for TX complete.
	if (wrote_bytes && tud_inited()) {
		usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
	}
}

void tud_cdc_tx_complete_cb(uint8_t)
{
	usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
}
