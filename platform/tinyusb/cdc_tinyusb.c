#include "platform/cdc.h"

#include <stdbool.h>
#include <stdint.h>

#include "device/usbd_pvt.h"
#include "platform/os.h"
#include "tusb.h"
#include "ui/ui.h"

#define CDC_SETTLE_MS 50u
#define POLL_WRITE_MAX 256u

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

typedef struct {
    uint32_t cursor;
    size_t capacity;
    size_t length;
} ring_write_t;

static void platform_cdc_kick_cb(void *);

void platform_cdc_init(void)
{
    cdc_mutex = xSemaphoreCreateMutex();
}

static void ring_write_character(char character, void *context)
{
    ring_write_t *write = context;

    if (write->length < write->capacity) {
        pending_buf[write->cursor] = (uint8_t)character;
        write->cursor = (write->cursor + 1u) % PLATFORM_CDC_BUFFER_SIZE;
    }

    write->length++;
}

static void platform_cdc_drop(size_t length)
{
    ui_notify_cdc_drop(length);
    if (tud_cdc_connected())
        usbd_defer_func(platform_cdc_kick_cb, NULL, false);
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

void platform_cdc_write_rendered(platform_cdc_render_fn render, void *context)
{
    if (!cdc_mutex)
        return;

    xSemaphoreTake(cdc_mutex, portMAX_DELAY);
    ring_write_t write = {
        .cursor = pending_wr,
        .capacity = PLATFORM_CDC_BUFFER_SIZE - pending_count,
    };
    render(ring_write_character, &write, context);

    if (write.length <= write.capacity) {
        pending_wr = write.cursor;
        pending_count += write.length;
        xSemaphoreGive(cdc_mutex);
        if (tud_cdc_connected())
            usbd_defer_func(platform_cdc_kick_cb, NULL, false);
        return;
    }

    xSemaphoreGive(cdc_mutex);

    const size_t length = write.length;
    if (length > PLATFORM_CDC_BUFFER_SIZE) {
        platform_cdc_drop(length);
        return;
    }

    platform_cdc_throttle_until_free(length);

    xSemaphoreTake(cdc_mutex, portMAX_DELAY);
    if ((PLATFORM_CDC_BUFFER_SIZE - pending_count) < length) {
        xSemaphoreGive(cdc_mutex);
        platform_cdc_drop(length);
        return;
    }

    write = (ring_write_t) {
        .cursor = pending_wr,
        .capacity = length,
    };
    render(ring_write_character, &write, context);
    if (write.length != length) {
        xSemaphoreGive(cdc_mutex);
        platform_cdc_drop(write.length);
        return;
    }

    pending_wr = write.cursor;
    pending_count += write.length;
    xSemaphoreGive(cdc_mutex);

    if (tud_cdc_connected())
        usbd_defer_func(platform_cdc_kick_cb, NULL, false);
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

        if (n > POLL_WRITE_MAX)
            n = POLL_WRITE_MAX;

        if (n > pending_count)
            n = pending_count;

        const uint32_t contiguous = PLATFORM_CDC_BUFFER_SIZE - pending_rd;
        if (n > contiguous)
            n = contiguous;

        const uint32_t written = tud_cdc_write(pending_buf + pending_rd, n);
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
