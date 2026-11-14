#ifndef ERGOTYPE_TUSB_DEVICE_H
#define ERGOTYPE_TUSB_DEVICE_H

#include <stdbool.h>

/* Called only from TinyUSB's device task; state follows delivered callbacks. */
void usb_device_on_suspend(void);
void usb_device_on_resume(void);
bool usb_device_is_suspended(void);

/* Call while suspended. At most one accepted wakeup request per suspend. */
void usb_device_request_wakeup(void);

#endif /* ERGOTYPE_TUSB_DEVICE_H */
