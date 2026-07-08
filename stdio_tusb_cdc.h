#ifndef ERGOTYPE_STDIO_TUSB_CDC_H
#define ERGOTYPE_STDIO_TUSB_CDC_H

#include <stddef.h>

// Buffer early/bursty output (e.g. dbg3config) until CDC is actually open (DTR=1).
// This avoids truncation due to tiny TinyUSB CDC TX FIFO and bus resets during enumeration.
#define BUFSIZE 2048u
// #define BUFSIZE 16384u

#define ASYNC_MSG_BUFSIZE 32u
#define ASYNC_MSG_TEXT_MAX (ASYNC_MSG_BUFSIZE - 2u)

// TinyUSB CDC log buffer (no pico stdio / no newlib hooks).
//
// Write path (any task): buffer bytes only, no tud_* calls.
// Read/flush path (USB task): stdio_tusb_cdc_poll() pumps to tud_cdc_write*.
//
// Buffer bytes for later transmit over CDC. Safe to call from any task.
void stdio_tusb_cdc_write(const void *buf, size_t length);
void _async_msg(const char *s);
#define async_msg(s) do { \
	_Static_assert(sizeof(s) - 1u <= ASYNC_MSG_TEXT_MAX, "async_msg too long"); \
	_async_msg(s); \
} while (0)

// Pump buffered output to CDC. Call this frequently from the TinyUSB task.
void stdio_tusb_cdc_poll(void);

#endif
