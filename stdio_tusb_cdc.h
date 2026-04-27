#ifndef ERGOTYPE_STDIO_TUSB_CDC_H
#define ERGOTYPE_STDIO_TUSB_CDC_H

// TinyUSB CDC log buffer (no pico stdio / no newlib hooks).
//
// Write path (any task): buffer bytes only, no tud_* calls.
// Read/flush path (USB task): stdio_tusb_cdc_poll() pumps to tud_cdc_write*.
//
// Buffer bytes for later transmit over CDC. Safe to call from any task.
void stdio_tusb_cdc_write(const void *buf, int length);

// Pump buffered output to CDC. Call this frequently from the TinyUSB task.
void stdio_tusb_cdc_poll(void);

#endif
