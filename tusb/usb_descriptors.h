/* usb_descriptors.h */

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

enum mode {
    HID = 0,
    MSC = 1,
};

enum report_id {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_CONSUMER = 2,
};

enum hid_output_profile {
    HID_OUTPUT_PROFILE_BOOT_KB_MOUSE = 0,
    HID_OUTPUT_PROFILE_NKRO_KB_MOUSE = 1,
};

enum hid_instance {
    HID_KEYBOARD_INSTANCE = 0,
    HID_MOUSE_INSTANCE,
};

#define HID_NKRO_KEY_BYTES 32

extern uint8_t hid_output_profile;

// Descriptor constants
// Vendor/product IDs used in the device descriptor.
#define USB_VID 0xCafe
#define USB_PID_HID 0x1001
#define USB_PID_CDC_MSC 0x1002
#define USB_PID_HID_NKRO 0x1003

// USB spec release reported by the device (bcdUSB).
#define USB_BCD 0x0200

// Device release number (bcdDevice).
#define USB_DEVICE_BCD 0x0100

// String descriptor indices used by the device descriptor.
#define USB_STRID_MANUFACTURER 0x01
#define USB_STRID_PRODUCT 0x02
#define USB_STRID_SERIAL 0x03

// Configuration descriptor fields.
#define USB_CONFIG_NUMBER 1         // bConfigurationValue
#define USB_CONFIG_STRIDX 0         // iConfiguration (0 = no string)
#define USB_CONFIG_ATTR 0x80        // bmAttributes (0x80 = bus powered)
#define USB_CONFIG_POWER_MA 100     // bMaxPower in mA

// Endpoint packet sizes.
#define USB_CDC_NOTIF_EP_SIZE 8     // CDC ACM notification (interrupt) EP size
#define USB_CDC_BULK_MPS_FS 64      // Full-speed CDC bulk max packet size
#define USB_MSC_MPS_FS 64           // Full-speed MSC bulk max packet size

// USB interrupt IN poll interval for the HID endpoint (bInterval).
// Full-speed units are milliseconds. We keep this a compile-time constant since
// it's part of the descriptor and can't be changed at runtime.
#define USB_HID_POLL_INTERVAL_MS 1

#endif /* USB_DESCRIPTORS_H_ */
