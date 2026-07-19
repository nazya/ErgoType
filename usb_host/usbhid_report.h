#ifndef USB_HOST_USBHID_REPORT_H
#define USB_HOST_USBHID_REPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

struct hid_device;

typedef void (*usbhid_control_report_done_t)(struct hid_device *hid,
					      void *context, int status);

int usbhid_report_init(void);
int usbhid_report_submit(struct hid_device *hid, const uint8_t *report,
			 uint16_t bufsize, uint32_t len, uint8_t xfer_result,
			 bool parse);
/* On success, done() receives ownership of report/context after parsing. */
int usbhid_control_report_submit(struct hid_device *hid, uint8_t report_type,
				 uint8_t *report, uint16_t bufsize,
				 uint16_t len, TaskHandle_t parser_owner,
				 usbhid_control_report_done_t done,
				 void *context);
bool usbhid_control_report_process_owned(struct hid_device *hid);
/* Control-wait glue: wake the report task after publishing control_waiter. */
void usbhid_control_report_owner_ready(void);
int usbhid_report_start(struct hid_device *hid);
void usbhid_report_close(struct hid_device *hid);
void usbhid_report_stop(struct hid_device *hid);
void usbhid_report_unplug(struct hid_device *hid);
void usbhid_report_release(struct hid_device *hid);
bool usbhid_report_is_stopping(struct hid_device *hid);
void usbhid_report_wait_idle(struct hid_device *hid);
void usbhid_report_task(void *pvParameters);

#endif
