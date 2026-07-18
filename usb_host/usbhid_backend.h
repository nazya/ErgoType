#ifndef USB_HOST_USBHID_BACKEND_H
#define USB_HOST_USBHID_BACKEND_H

#include <stdint.h>

#include "host/usbh_pvt.h"

/*
 * Boundary between TinyUSB host callbacks and the Linux-style USB HID
 * backend. These functions intentionally retain the baseline's synchronous
 * behavior; the facade exists so execution context can change independently.
 */
void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      const uint8_t *desc_report, uint16_t desc_len);
void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance);
void usbhid_backend_device_mount(uint8_t dev_addr);
void usbhid_backend_device_umount(uint8_t dev_addr);
void usbhid_backend_report_received(uint8_t dev_addr, uint8_t instance,
				    const uint8_t *report, uint16_t len);
usbh_class_driver_t const *usbhid_backend_app_driver_get(
	uint8_t *driver_count);

#endif
