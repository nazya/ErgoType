/* Firmware-only public lifecycle API for the TinyUSB-to-Linux HID adapter. */
#ifndef USB_HOST_USBHID_H
#define USB_HOST_USBHID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct hid_device;
struct work_struct;
struct usb_device;

int usbhid_lifecycle_init(void);
void usbhid_lifecycle_task(void *pvParameters);
void usbhid_lifecycle_schedule_work(struct work_struct *work);
bool usbhid_lifecycle_cancel_work(struct work_struct *work);
bool usbhid_lifecycle_generation_snapshot(struct hid_device *hid,
					  uint32_t *physical_generation);
bool usbhid_lifecycle_generation_current(struct hid_device *hid,
					 uint32_t physical_generation);
bool usbhid_lifecycle_quiesce_rebuild(struct hid_device *hid,
				      uint32_t physical_generation);
struct hid_device *usbhid_lifecycle_find_hid(const struct usb_device *dev,
					      unsigned int ifnum);
struct hid_device *usbhid_ifnum_io_get(const struct usb_device *dev,
					       unsigned int ifnum);
void usbhid_ifnum_io_put(struct hid_device *hid);

#if defined(WACOM_MODE_CHANGE_TEST)
enum usbhid_wacom_test_receiver_class {
	USBHID_WACOM_TEST_RX_UNKNOWN,
	USBHID_WACOM_TEST_RX_GATED,
	USBHID_WACOM_TEST_RX_SELECTED,
};

void usbhid_wacom_test_input_note(struct hid_device *hid,
		unsigned int report_id, const uint8_t *data, size_t size);
void usbhid_wacom_test_receiver_note(
		uint16_t pid, enum usbhid_wacom_test_receiver_class classification);
#endif

#endif
