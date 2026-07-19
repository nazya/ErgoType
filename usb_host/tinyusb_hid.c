#include <stdint.h>

#include "tusb.h"

#include "hid_async.h"
#include "usbhid_backend.h"

/*
 * TinyUSB application callback facade. Callbacks only publish bounded
 * transport state; Linux-shaped probe, remove, parsing, and request completion
 * run in their task owners.
 */
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
		      uint8_t const *desc_report, uint16_t desc_len)
{
	usbhid_backend_hid_mount(dev_addr, instance, desc_report, desc_len);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	usbhid_backend_hid_umount(dev_addr, instance);
}

void tuh_mount_cb(uint8_t dev_addr)
{
	usbhid_backend_device_mount(dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
	usbhid_backend_device_umount(dev_addr);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
				uint8_t const *report, uint16_t len)
{
	usbhid_backend_report_received(dev_addr, instance, report, len);
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t instance,
				    uint8_t report_id, uint8_t report_type,
				    uint16_t len)
{
	hid_async_backend_get_report_complete(dev_addr, instance, report_id,
					      report_type, len);
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t instance,
				    uint8_t report_id, uint8_t report_type,
				    uint16_t len)
{
	hid_async_backend_set_report_complete(dev_addr, instance, report_id,
					      report_type, len);
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t instance,
			    uint8_t const *report, uint16_t len)
{
	hid_async_backend_report_sent(dev_addr, instance, report, len);
}

usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
	return usbhid_backend_app_driver_get(driver_count);
}
