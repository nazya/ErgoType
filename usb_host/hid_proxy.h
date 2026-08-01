/* Firmware-only Linux input to TinyUSB device proxy boundary. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct hid_device;

int hid_proxy_runtime_init(void);
void hid_proxy_input_init(void);
void hid_proxy_activate_hid(struct hid_device *hid);

void hid_proxy_tud_mount(void);
void hid_proxy_tud_umount(void);
void hid_proxy_tud_poll(void);
void hid_proxy_tud_report_complete(bool success);
uint16_t hid_proxy_tud_get_report(uint8_t report_id, uint8_t *buffer,
				  uint16_t reqlen);
