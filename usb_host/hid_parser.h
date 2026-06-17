#ifndef USB_HOST_HID_PARSER_H
#define USB_HOST_HID_PARSER_H

#include <stdint.h>

#define HOST_HID_MAX_FIELDS 32
#define HOST_HID_MAX_USAGES 8
#define HOST_HID_MAX_REPORTS 8
#define HOST_HID_GLOBAL_STACK 4
#define HOST_HID_REPORT_LEN CFG_TUH_HID_EPIN_BUFSIZE

struct hid_field {
	uint16_t bit_offset;
	uint16_t flags;
	uint16_t usage_page;
	uint16_t usage_min;
	uint16_t usage_max;
	uint16_t usages[HOST_HID_MAX_USAGES];
	uint16_t report_count;
	int32_t logical_min;
	int32_t logical_max;
	uint8_t report_id;
	uint8_t report_size;
	uint8_t usage_count;
	uint8_t has_usage_min;
	uint8_t has_usage_max;
};

void hid_parse_fields(struct hid_field *fields,
		      uint8_t *field_count,
		      uint8_t *has_report_id,
		      const uint8_t *desc,
		      uint16_t desc_len);
int32_t sign_extend(uint32_t value, uint8_t bits);

#endif
