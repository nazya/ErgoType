#ifndef USB_HOST_HID_TRANSPORT_SYNC_H
#define USB_HOST_HID_TRANSPORT_SYNC_H

#include <stdbool.h>

/*
 * Upstream Linux: no equivalent common lock. Linux uses a per-interface mutex
 * for blocking lifecycle operations and a short IRQ-side FIFO spinlock.
 * TinyUSB callbacks enter this port from the dedicated host task, not from an
 * ISR, so this FreeRTOS task mutex keeps the cross-module generation, lifetime,
 * async-capacity, and report-recovery tables atomic without masking interrupts.
 * Fixed callback slots plus task notifications already provide asynchronous
 * handoff; split this state domain only with a complete ownership/lock-order
 * replacement, not by adding another mutex to the constrained heap.
 */
bool hid_transport_sync_init(void);
void hid_transport_lock(void);
void hid_transport_unlock(void);

#endif
