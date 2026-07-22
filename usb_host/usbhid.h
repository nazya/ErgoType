/* Firmware-only public lifecycle API for the TinyUSB-to-Linux HID adapter. */
#ifndef USB_HOST_USBHID_H
#define USB_HOST_USBHID_H

struct hid_device;
struct usb_device;

int usbhid_lifecycle_init(void);
void usbhid_lifecycle_task(void *pvParameters);
struct hid_device *usbhid_ifnum_io_get(const struct usb_device *dev,
					       unsigned int ifnum);
void usbhid_ifnum_io_put(struct hid_device *hid);

#endif
