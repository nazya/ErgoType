#ifndef USB_HOST_USBHID_REPORT_H
#define USB_HOST_USBHID_REPORT_H

#include <stdbool.h>
#include <stdint.h>

struct hid_device;

int usbhid_report_init(void);
int usbhid_report_submit(struct hid_device *hid, const uint8_t *report,
			 uint16_t len, bool parse);
int usbhid_report_start(struct hid_device *hid);
void usbhid_report_close(struct hid_device *hid);
void usbhid_report_stop(struct hid_device *hid);
void usbhid_report_unplug(struct hid_device *hid);
bool usbhid_report_is_stopping(struct hid_device *hid);
void usbhid_report_wait_idle(struct hid_device *hid);
void usbhid_report_task(void *pvParameters);

#endif
