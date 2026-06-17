#include <string.h>

#include "tusb.h"

#include "hid_parser.h"

struct hid_global {
	uint16_t usage_page;
	uint16_t report_size;
	uint16_t report_count;
	int32_t logical_min;
	int32_t logical_max;
	uint8_t report_id;
};

struct hid_local {
	uint16_t usages[HOST_HID_MAX_USAGES];
	uint16_t usage_min;
	uint16_t usage_max;
	uint8_t usage_count;
	uint8_t has_usage_min;
	uint8_t has_usage_max;
};

struct hid_report_offset {
	uint8_t used;
	uint8_t report_id;
	uint16_t bits;
};

static uint32_t read_u32_le(const uint8_t *data, uint8_t len)
{
	uint32_t value = 0;

	for (uint8_t i = 0; i < len; i++)
		value |= (uint32_t)data[i] << (i * 8u);

	return value;
}

int32_t sign_extend(uint32_t value, uint8_t bits)
{
	uint32_t sign;

	if (!bits || bits >= 32)
		return (int32_t)value;

	sign = 1u << (bits - 1u);
	return (int32_t)((value ^ sign) - sign);
}

void hid_parse_fields(struct hid_field *fields,
		      uint8_t *field_count,
		      uint8_t *has_report_id,
		      const uint8_t *desc,
		      uint16_t desc_len)
{
	struct hid_global global = {0};
	struct hid_global stack[HOST_HID_GLOBAL_STACK];
	struct hid_local local = {0};
	struct hid_report_offset offsets[HOST_HID_MAX_REPORTS] = {0};
	uint8_t stack_count = 0;
	uint8_t offset_count = 0;

	while (desc_len) {
		uint8_t prefix = *desc++;
		uint8_t size;
		uint8_t type;
		uint8_t tag;
		uint32_t value;

		desc_len--;
		if (prefix == 0xfe) {
			uint8_t long_size;

			if (desc_len < 2)
				break;

			long_size = desc[0];
			if (desc_len < (uint16_t)(long_size + 2u))
				break;

			desc += long_size + 2u;
			desc_len -= long_size + 2u;
			continue;
		}

		size = prefix & 0x3u;
		if (size == 3)
			size = 4;
		if (desc_len < size)
			break;

		type = (prefix >> 2u) & 0x3u;
		tag = (prefix >> 4u) & 0xfu;
		value = read_u32_le(desc, size);

		switch (type) {
		case RI_TYPE_MAIN:
			switch (tag) {
			case RI_MAIN_INPUT: {
				uint16_t *bit_offset = NULL;
				uint16_t flags = value & 0x1ffu;
				uint32_t bits = (uint32_t)global.report_size * global.report_count;

				for (uint8_t i = 0; i < offset_count; i++)
					if (offsets[i].used && offsets[i].report_id == global.report_id) {
						bit_offset = &offsets[i].bits;
						break;
					}

				if (!bit_offset && offset_count < HOST_HID_MAX_REPORTS) {
					offsets[offset_count].used = 1;
					offsets[offset_count].report_id = global.report_id;
					offsets[offset_count].bits = 0;
					bit_offset = &offsets[offset_count++].bits;
				}

				if (bit_offset &&
				    !(flags & HID_CONSTANT) &&
				    global.report_size &&
				    global.report_size <= 32 &&
				    global.report_count &&
				    *field_count < HOST_HID_MAX_FIELDS) {
					struct hid_field *field = &fields[(*field_count)++];

					memset(field, 0, sizeof(*field));
					field->bit_offset = *bit_offset;
					field->flags = flags;
					field->usage_page = global.usage_page;
					field->usage_min = local.usage_min;
					field->usage_max = local.usage_max;
					field->report_count = global.report_count;
					field->logical_min = global.logical_min;
					field->logical_max = global.logical_max;
					field->report_id = global.report_id;
					field->report_size = global.report_size;
					field->usage_count = local.usage_count;
					field->has_usage_min = local.has_usage_min;
					field->has_usage_max = local.has_usage_max;
					memcpy(field->usages, local.usages, sizeof(field->usages));
				}

				if (bit_offset) {
					if (bits > UINT16_MAX - *bit_offset)
						*bit_offset = UINT16_MAX;
					else
						*bit_offset += bits;
				}

				memset(&local, 0, sizeof(local));
				break;
			}
			case RI_MAIN_OUTPUT:
			case RI_MAIN_FEATURE:
			case RI_MAIN_COLLECTION:
			case RI_MAIN_COLLECTION_END:
				memset(&local, 0, sizeof(local));
				break;
			default:
				break;
			}
			break;
		case RI_TYPE_GLOBAL:
			switch (tag) {
			case RI_GLOBAL_USAGE_PAGE:
				global.usage_page = value;
				break;
			case RI_GLOBAL_LOGICAL_MIN:
				global.logical_min = sign_extend(read_u32_le(desc, size), size * 8u);
				break;
			case RI_GLOBAL_LOGICAL_MAX:
				global.logical_max = sign_extend(read_u32_le(desc, size), size * 8u);
				break;
			case RI_GLOBAL_REPORT_SIZE:
				global.report_size = value;
				break;
			case RI_GLOBAL_REPORT_ID:
				global.report_id = value;
				*has_report_id = 1;
				break;
			case RI_GLOBAL_REPORT_COUNT:
				global.report_count = value;
				break;
			case RI_GLOBAL_PUSH:
				if (stack_count < HOST_HID_GLOBAL_STACK)
					stack[stack_count++] = global;
				break;
			case RI_GLOBAL_POP:
				if (stack_count)
					global = stack[--stack_count];
				break;
			default:
				break;
			}
			break;
		case RI_TYPE_LOCAL:
			switch (tag) {
			case RI_LOCAL_USAGE:
				if (local.usage_count < HOST_HID_MAX_USAGES)
					local.usages[local.usage_count++] = value;
				break;
			case RI_LOCAL_USAGE_MIN:
				local.usage_min = value;
				local.has_usage_min = 1;
				break;
			case RI_LOCAL_USAGE_MAX:
				local.usage_max = value;
				local.has_usage_max = 1;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}

		desc += size;
		desc_len -= size;
	}
}
