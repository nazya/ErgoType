#include "platform/cdc.h"

#include <stdbool.h>
#include <stdint.h>

#include "device/usbd_pvt.h"
#include "platform/os.h"
#include "tusb.h"
#include "ui/ui.h"

#define CDC_SETTLE_MS 50u
#define POLL_TMP_BUFSIZE 256u

#define THROTTLE_FIRST_WAIT_TICKS (pdMS_TO_TICKS(1024u))
#define THROTTLE_MAX_WAIT_TICKS 16u
#define THROTTLE_WAIT_TICKS 2u

static SemaphoreHandle_t cdc_mutex;
static uint8_t pending_buf[PLATFORM_CDC_BUFFER_SIZE];
static uint32_t pending_wr;
static uint32_t pending_rd;
static uint32_t pending_count;

static bool cdc_was_connected;
static bool cdc_settled;
static bool cdc_settled_once;
static TimerHandle_t cdc_settle_timer;

static void platform_cdc_kick_cb(void *);

void platform_cdc_init(void)
{
    cdc_mutex = xSemaphoreCreateMutex();
}

static void platform_cdc_throttle_until_free(size_t free_target)
{
    static bool first_throttle = true;
    TickType_t max_wait_ticks = THROTTLE_MAX_WAIT_TICKS;

    if (!cdc_mutex)
        return;

    if (first_throttle) {
        first_throttle = false;
        max_wait_ticks = THROTTLE_FIRST_WAIT_TICKS;
    } else if (!tud_cdc_connected()) {
        return;
    }

    TickType_t waited = 0;

    while (1) {
        xSemaphoreTake(cdc_mutex, portMAX_DELAY);
        size_t free_bytes = PLATFORM_CDC_BUFFER_SIZE - pending_count;
        xSemaphoreGive(cdc_mutex);

        if (free_bytes >= free_target)
            return;

        if (waited >= max_wait_ticks)
            break;

        usbd_defer_func(platform_cdc_kick_cb, NULL, false);
        vTaskDelay(THROTTLE_WAIT_TICKS);
        waited += THROTTLE_WAIT_TICKS;
    }

    usbd_defer_func(platform_cdc_kick_cb, NULL, false);
}

static void platform_cdc_settle_timer_cb(TimerHandle_t timer)
{
    (void)timer;

    if (!tud_cdc_connected())
        return;

    cdc_settled = true;
    cdc_settled_once = true;

    usbd_defer_func(platform_cdc_kick_cb, NULL, false);
}

static void platform_cdc_kick_cb(void *arg)
{
    (void)arg;
}

void platform_cdc_write(const void *buf, size_t length)
{
    const uint8_t *src = (const uint8_t *)buf;

    if (!cdc_mutex)
        return;

    if (length > PLATFORM_CDC_BUFFER_SIZE)
        length = PLATFORM_CDC_BUFFER_SIZE;

    if ((PLATFORM_CDC_BUFFER_SIZE - pending_count) < length) {
        platform_cdc_throttle_until_free(length);
    }

    if ((PLATFORM_CDC_BUFFER_SIZE - pending_count) < length) {
        ui_notify_cdc_drop(length);
        if (tud_cdc_connected())
            usbd_defer_func(platform_cdc_kick_cb, NULL, false);
        return;
    }

    xSemaphoreTake(cdc_mutex, portMAX_DELAY);
    for (size_t i = 0; i < length; ++i) {
        pending_buf[pending_wr] = src[i];
        pending_wr = (pending_wr + 1u) % PLATFORM_CDC_BUFFER_SIZE;
        if (pending_count < PLATFORM_CDC_BUFFER_SIZE)
            pending_count++;
    }
    xSemaphoreGive(cdc_mutex);

    if (tud_cdc_connected()) {
        usbd_defer_func(platform_cdc_kick_cb, NULL, false);
    }
}

void platform_cdc_poll(void)
{
    if (!tud_cdc_connected()) {
        cdc_was_connected = false;
        cdc_settled = false;
        if (cdc_settle_timer)
            (void)xTimerStop(cdc_settle_timer, 0);
        return;
    }

    if (!cdc_was_connected) {
        cdc_was_connected = true;
        cdc_settled = false;

        const TickType_t settle_ticks = pdMS_TO_TICKS(CDC_SETTLE_MS);
        if (!cdc_settle_timer) {
            cdc_settle_timer = xTimerCreate("cdc_settle",
                                            settle_ticks,
                                            pdFALSE,
                                            NULL,
                                            platform_cdc_settle_timer_cb);
        }

        if (cdc_settle_timer) {
            (void)xTimerChangePeriod(cdc_settle_timer, settle_ticks, 0);
            (void)xTimerReset(cdc_settle_timer, 0);
        }

        return;
    }

    if (!cdc_settled || !cdc_mutex)
        return;

    uint32_t wrote_bytes = 0;
    bool still_pending = false;
    while (1) {
        uint32_t n = tud_cdc_write_available();
        if (!n)
            break;

        xSemaphoreTake(cdc_mutex, portMAX_DELAY);

        if (!pending_count) {
            xSemaphoreGive(cdc_mutex);
            return;
        }

        uint8_t tmp[POLL_TMP_BUFSIZE];
        if (n > POLL_TMP_BUFSIZE)
            n = POLL_TMP_BUFSIZE;

        if (n > pending_count)
            n = pending_count;

        uint32_t rd = pending_rd;
        for (uint32_t i = 0; i < n; ++i) {
            tmp[i] = pending_buf[rd];
            rd = (rd + 1u) % PLATFORM_CDC_BUFFER_SIZE;
        }

        const uint32_t written = tud_cdc_write(tmp, n);
        wrote_bytes += written;

        pending_rd = (pending_rd + written) % PLATFORM_CDC_BUFFER_SIZE;
        pending_count -= written;
        still_pending = pending_count != 0;

        xSemaphoreGive(cdc_mutex);

        tud_cdc_write_flush();

        if (written < n)
            break;
    }

    if (wrote_bytes && still_pending)
        usbd_defer_func(platform_cdc_kick_cb, NULL, false);
}
