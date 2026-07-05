#ifndef USB_HOST_HID_PORT_H
#define USB_HOST_HID_PORT_H

struct hid_device;

int hid_port_register_device(struct hid_device *hid);
void hid_port_unregister_device(struct hid_device *hid);

#endif
