/* usb_descriptors.h */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

enum
{
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_MOUSE = 2,
};

enum mode {
    HID = 0,
    MSC = 1,
};

#endif /* USB_DESCRIPTORS_H_ */
