/* Firmware-only public lifecycle API for the TinyUSB-to-Linux HID adapter. */
#ifndef USB_HOST_USBHID_H
#define USB_HOST_USBHID_H

struct hid_device;
struct work_struct;
struct usb_device;

int usbhid_lifecycle_init(void);
void usbhid_lifecycle_task(void *pvParameters);
void usbhid_lifecycle_schedule_work(struct work_struct *work);
void usbhid_lifecycle_cancel_work(struct work_struct *work);
struct hid_device *usbhid_lifecycle_find_hid(const struct usb_device *dev,
					      unsigned int ifnum);
struct hid_device *usbhid_ifnum_io_get(const struct usb_device *dev,
					       unsigned int ifnum);
void usbhid_ifnum_io_put(struct hid_device *hid);

#endif
