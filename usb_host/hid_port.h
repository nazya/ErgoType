#ifndef USB_HOST_HID_PORT_H
#define USB_HOST_HID_PORT_H

#include <stdint.h>

#include "linux/include/linux/hid.h"

// Upstream Linux: no equivalent. Port-only firmware consumer boundary.
int hid_port_register_device(struct hid_device *hid);
void hid_port_unregister_device(struct hid_device *hid);
void hid_host_trace_input_event(struct hid_device *hid, unsigned int type, unsigned int code, int value);
void hid_host_trace_input_caps(struct hid_device *hid, uint8_t caps, uint8_t rel_flags,
                               uint8_t abs_flags, uint8_t has_key);
void hid_host_trace_input_state(struct hid_device *hid, uint8_t state, uint16_t detail, int result);

#endif
