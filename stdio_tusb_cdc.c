#include "stdio_tusb_cdc.h"

#include "tusb.h"

#include "device/usbd_pvt.h"

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

// Some host terminals/drivers drop the first bytes right after they open the CDC port
// (DTR goes high). If the drop happens mid ANSI escape sequence, the remainder shows
// up as visible "artifacts" like `4mErgoType/...` (tail of "\033[34m").
//
// Delay the first transmit a little after a connect edge so the host has time to settle.
#ifndef CDC_SETTLE_MS
#define CDC_SETTLE_MS 50u
#endif

#ifndef STDIO_TUSB_CDC_PENDING_BUFSIZE
// Buffer early/bursty output (e.g. dbg3config) until CDC is actually open (DTR=1).
// This avoids truncation due to tiny TinyUSB CDC TX FIFO and bus resets during enumeration.
#define STDIO_TUSB_CDC_PENDING_BUFSIZE 16384u
#endif

static uint8_t pending_buf[STDIO_TUSB_CDC_PENDING_BUFSIZE];
static const uint32_t pending_size = (uint32_t)sizeof(pending_buf);
static uint32_t pending_wr;
static uint32_t pending_rd;
static uint32_t pending_count;

static bool kick_queued;

static bool cdc_was_connected;
static bool cdc_settled;
static TimerHandle_t cdc_settle_timer;

static void stdio_tusb_cdc_kick_cb(void *);

static void stdio_tusb_cdc_settle_timer_cb(TimerHandle_t)
{
    // Wake the TinyUSB device task so it can re-run stdio_tusb_cdc_poll()
    // after the settle delay, even if there are no new USB events.
    if (!tud_inited())
        return;

    cdc_settled = true;

    // This is a rare wakeup path; don't depend on kick_queued state.
    kick_queued = true;
    usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
}

static void stdio_tusb_cdc_kick_cb(void *)
{
    kick_queued = false;
    stdio_tusb_cdc_poll();
}

void stdio_tusb_cdc_write(const void *buf, int length)
{
    const uint8_t *src = (const uint8_t *)buf;

    // NOTE: Caller must serialize access to stdio_tusb_cdc_write().
    // This module does NOT lock on write, to avoid taking a second mutex in the
    // logging path (log.c already serializes calls).
    for (int i = 0; i < length; ++i) {
        // If the ring is full, drop the oldest byte to make room.
        if (pending_count == pending_size) {
            pending_rd = (pending_rd + 1u) % pending_size;
            pending_count--;
        }

        pending_buf[pending_wr] = src[i];
        pending_wr = (pending_wr + 1u) % pending_size;
        pending_count++;
    }

    // Wake the TinyUSB device task (blocked in `tud_task()`) so it can run stdio_tusb_cdc_poll().
    // Do this through TinyUSB's own event queue (USBD_EVENT_FUNC_CALL) to avoid RTOS cross-signals here.
    if (tud_inited() && !kick_queued) {
        kick_queued = true;
        usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
    }
}

void stdio_tusb_cdc_poll(void)
{
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
    while (1) {
        uint32_t n = tud_cdc_write_available();
        if (!n || !pending_count)
            break;

        uint8_t tmp[256];
        if (n > 256u)
            n = 256u;

        if (n > pending_count)
            n = pending_count;

        for (uint32_t i = 0; i < n; ++i) {
            tmp[i] = pending_buf[pending_rd];
            pending_rd = (pending_rd + 1u) % pending_size;
        }
        pending_count -= n;

        tud_cdc_write(tmp, n);
        tud_cdc_write_flush();
    }
}

void tud_cdc_tx_complete_cb(uint8_t)
{
    if (!pending_count)
        return;

    if (!kick_queued) {
        kick_queued = true;
        usbd_defer_func(stdio_tusb_cdc_kick_cb, NULL, false);
    }
}
