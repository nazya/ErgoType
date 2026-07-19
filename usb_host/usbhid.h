#ifndef USB_HOST_USBHID_H
#define USB_HOST_USBHID_H

int usbhid_lifecycle_init(void);
void usbhid_lifecycle_task(void *pvParameters);

#endif
