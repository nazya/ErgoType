#ifndef USB_DEVICE_H_
#define USB_DEVICE_H_

#include <stdbool.h>

/* Called only from TinyUSB's device task; state follows delivered callbacks. */
bool usb_device_is_suspended(void);

/* Call while suspended. At most one accepted wakeup request per suspend. */
void usb_device_request_wakeup(void);

#endif /* USB_DEVICE_H_ */
