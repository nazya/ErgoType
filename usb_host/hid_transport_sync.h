#ifndef USB_HOST_HID_TRANSPORT_SYNC_H
#define USB_HOST_HID_TRANSPORT_SYNC_H

#include <stdbool.h>

/*
 * Upstream Linux: no equivalent common lock. Linux uses a per-interface mutex
 * for blocking lifecycle operations and a short IRQ-side FIFO spinlock.
 * TinyUSB callbacks enter this port from the dedicated host task, not from an
 * ISR, so this transitional FreeRTOS task mutex keeps the current cross-module
 * transport tables atomic without masking interrupts. Lifecycle, report, and
 * async ownership will be split further into message queues.
 */
bool hid_transport_sync_init(void);
void hid_transport_lock(void);
void hid_transport_unlock(void);

#endif
