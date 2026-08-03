/* Firmware-only Linux input to TinyUSB device proxy boundary. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct hid_device;
struct input_dev;

int hid_proxy_runtime_init(void);
void hid_proxy_input_init(void);
void hid_proxy_activate_hid(struct hid_device *hid);
void hid_proxy_activate_input(struct input_dev *dev);
void hid_proxy_reconcile_input_capabilities_locked(struct input_dev *dev,
						   bool activate);

void hid_proxy_tud_mount(void);
void hid_proxy_tud_umount(void);
void hid_proxy_tud_poll(void);
void hid_proxy_tud_report_complete(uint8_t instance, bool success);
uint16_t hid_proxy_tud_get_report(uint8_t instance, uint8_t report_id,
				  uint8_t report_type, uint8_t *buffer,
				  uint16_t reqlen);
