#ifndef ERGOTYPE_STDIO_TUSB_CDC_H
#define ERGOTYPE_STDIO_TUSB_CDC_H

#include <stddef.h>

// Buffer early/bursty output (e.g. dbg3config) until CDC is actually open (DTR=1).
// This avoids truncation due to tiny TinyUSB CDC TX FIFO and bus resets during enumeration.
#define BUFSIZE 2048u
// #define BUFSIZE 16384u


// TinyUSB CDC log buffer (no pico stdio / no newlib hooks).
//
// Write path (any task): buffer bytes only, no tud_* calls.
// Read/flush path (USB task): stdio_tusb_cdc_poll() pumps to tud_cdc_write*.
//
// Buffer bytes for later transmit over CDC. Safe to call from any task.
void stdio_tusb_cdc_write(const void *buf, size_t length);

// Pump buffered output to CDC. Call this frequently from the TinyUSB task.
void stdio_tusb_cdc_poll(void);

#endif
