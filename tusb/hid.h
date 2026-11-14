#ifndef ERGOTYPE_TUSB_HID_H
#define ERGOTYPE_TUSB_HID_H

/* Called only from TinyUSB's device task, after tud_task(). */
void hid_handle(void);

#endif /* ERGOTYPE_TUSB_HID_H */
