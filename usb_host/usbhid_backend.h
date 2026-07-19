#ifndef USB_HOST_USBHID_BACKEND_H
#define USB_HOST_USBHID_BACKEND_H

#include <stdint.h>

#include "host/usbh_pvt.h"

/*
 * Boundary between TinyUSB host callbacks and the Linux-style USB HID
 * transport. Callback entry points may only copy bounded ingress data, rotate
 * lifecycle state, and wake a task; they must not allocate, wait, or log.
 */
void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      const uint8_t *desc_report, uint16_t desc_len);
void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance);
void usbhid_backend_device_mount(uint8_t dev_addr);
void usbhid_backend_device_umount(uint8_t dev_addr);
void usbhid_backend_report_completed(uint8_t dev_addr, uint8_t instance,
				     uint32_t generation,
				     const uint8_t *report, uint16_t bufsize,
				     uint32_t len, uint8_t xfer_result);
/* Host-owner fault ingress; lifecycle task performs the actual logging. */
void usbhid_backend_rx_rearm_failed(void);
void usbhid_backend_rx_transfer_failed(uint8_t xfer_result);
usbh_class_driver_t const *usbhid_backend_app_driver_get(
	uint8_t *driver_count);

#endif
