/* Firmware-only Linux input to TinyUSB device proxy boundary. */
#include "hid_proxy.h"

#include <limits.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "tusb.h"

#include "linux/include/linux/hid.h"
#include "linux/include/linux/input.h"
#include "linux/include/linux/input/mt.h"
#include "stdio_tusb_cdc.h"
#include "tusb/usb_descriptors.h"

enum hid_proxy_inflight_kind {
	HID_PROXY_INFLIGHT_NONE = 0,
	HID_PROXY_INFLIGHT_SOURCE_PRESENT,
	HID_PROXY_INFLIGHT_SOURCE_REMOVED,
	HID_PROXY_INFLIGHT_STYLUS_FRAME,
	HID_PROXY_INFLIGHT_STYLUS_NEUTRAL,
};

enum hid_proxy_source_flags {
	HID_PROXY_SOURCE_PRESENT = 1u << 0,
	HID_PROXY_SOURCE_STYLUS3 = 1u << 1,
	HID_PROXY_SOURCE_DIRECT = 1u << 2,
	HID_PROXY_SOURCE_TILT_NORMALIZED = 1u << 3,
};

enum hid_proxy_stylus_switches {
	HID_PROXY_STYLUS_TIP = 1u << 0,
	HID_PROXY_STYLUS_IN_RANGE = 1u << 1,
	HID_PROXY_STYLUS_BARREL = 1u << 2,
	HID_PROXY_STYLUS_INVERT = 1u << 3,
	HID_PROXY_STYLUS_ERASER = 1u << 4,
	HID_PROXY_STYLUS_SECONDARY_BARREL = 1u << 5,
};

enum hid_proxy_touch_inflight_kind {
	HID_PROXY_TOUCH_INFLIGHT_NONE = 0,
	HID_PROXY_TOUCH_INFLIGHT_SOURCE_PRESENT,
	HID_PROXY_TOUCH_INFLIGHT_SOURCE_REMOVED,
	HID_PROXY_TOUCH_INFLIGHT_CONTACT,
};

enum hid_proxy_touch_source_flags {
	HID_PROXY_TOUCH_SOURCE_PRESENT = 1u << 0,
	HID_PROXY_TOUCH_SOURCE_DIRECT = 1u << 1,
	HID_PROXY_TOUCH_SOURCE_POINTER = 1u << 2,
	HID_PROXY_TOUCH_SOURCE_SEMI_MT = 1u << 3,
	HID_PROXY_TOUCH_SOURCE_BUTTONPAD = 1u << 4,
	HID_PROXY_TOUCH_SOURCE_PRESSUREPAD = 1u << 5,
};

enum hid_proxy_touch_supported_fields {
	HID_PROXY_TOUCH_POSITION_X = 1u << 0,
	HID_PROXY_TOUCH_POSITION_Y = 1u << 1,
	HID_PROXY_TOUCH_PRESSURE = 1u << 2,
	HID_PROXY_TOUCH_MAJOR = 1u << 3,
	HID_PROXY_TOUCH_MINOR = 1u << 4,
	HID_PROXY_TOUCH_WIDTH_MAJOR = 1u << 5,
	HID_PROXY_TOUCH_WIDTH_MINOR = 1u << 6,
	HID_PROXY_TOUCH_ORIENTATION = 1u << 7,
	HID_PROXY_TOUCH_DISTANCE = 1u << 8,
	HID_PROXY_TOUCH_TOOL_X = 1u << 9,
	HID_PROXY_TOUCH_TOOL_Y = 1u << 10,
	HID_PROXY_TOUCH_TOOL_TYPE = 1u << 11,
	HID_PROXY_TOUCH_BUTTONS = 1u << 12,
};

enum hid_proxy_touch_switches {
	HID_PROXY_TOUCH_TIP = 1u << 0,
	HID_PROXY_TOUCH_IN_RANGE = 1u << 1,
	HID_PROXY_TOUCH_CONFIDENCE = 1u << 2,
};

enum hid_proxy_touch_phase {
	HID_PROXY_TOUCH_PHASE_NONE = 0,
	HID_PROXY_TOUCH_PHASE_RELEASE,
	HID_PROXY_TOUCH_PHASE_ACTIVE,
	HID_PROXY_TOUCH_PHASE_DUMMY,
	HID_PROXY_TOUCH_PHASE_RESET_FLUSH,
};

#define HID_PROXY_FRAME_QUEUE_DEPTH 8
#define HID_PROXY_CENTIDEGREES_PER_RADIAN 5730

struct hid_proxy_stylus_report {
	u8 switches;
	u8 transducer_index;
	__le32 x;
	__le32 y;
	__le16 pressure;
	__le16 distance;
	__le16 tilt_x;
	__le16 tilt_y;
	__le32 serial;
} __packed;

struct hid_proxy_source_report {
	u8 version;
	u8 transducer_index;
	u8 flags;
	__le32 source_id;
	__le16 vendor;
	__le16 product;
	__le16 tool_code;
	__le32 tool_id;
} __packed;

struct hid_proxy_touch_report {
	u8 switches;
	__le32 contact_id;
	__le32 x;
	__le32 y;
	__le16 pressure;
	__le16 touch_major;
	__le16 touch_minor;
	u8 contact_count;
	u8 buttons;
	__le32 frame_sequence;
	__le16 slot;
	u8 tool_type;
	__le16 width_major;
	__le16 width_minor;
	__le16 orientation;
	__le16 distance;
	__le32 tool_x;
	__le32 tool_y;
	u8 source_buttons;
} __packed;

struct hid_proxy_touch_source_report {
	u8 version;
	u8 source_index;
	u8 flags;
	__le16 supported_fields;
	__le32 source_id;
	__le16 vendor;
	__le16 product;
	__le16 slot_count;
} __packed;

static_assert(sizeof(struct hid_proxy_stylus_report) == 22);
static_assert(sizeof(struct hid_proxy_source_report) == 17);
static_assert(sizeof(struct hid_proxy_touch_report) == 45);
static_assert(sizeof(struct hid_proxy_touch_source_report) == 15);

struct hid_proxy_axis {
	int value;
	int minimum;
	int maximum;
	int resolution;
};

struct hid_proxy_frame {
	struct hid_proxy_stylus_report stylus;
	struct hid_proxy_source_report source;
	bool source_pending;
};

struct hid_proxy_source {
	bool active;
	bool retiring;
	bool published;
	bool source_dirty;
	bool terminal_pending;
	bool drop_reported;
	bool direct;
	bool touch;
	bool barrel;
	bool secondary_barrel;
	bool stylus3;
	u8 transducer_index;
	u8 frame_head;
	u8 frame_count;
	u16 vendor;
	u16 product;
	u16 tool_code;
	u32 tool_id;
	u32 source_id;
	u32 serial;
	struct hid_proxy_axis x;
	struct hid_proxy_axis y;
	struct hid_proxy_axis pressure;
	struct hid_proxy_axis distance;
	struct hid_proxy_axis tilt_x;
	struct hid_proxy_axis tilt_y;
	struct hid_proxy_frame frames[HID_PROXY_FRAME_QUEUE_DEPTH];
};

struct hid_proxy_handle {
	struct input_handle handle;
	struct list_head runtime_node;
	struct hid_proxy_source source;
};

struct hid_proxy_touch_slot_state {
	s32 tracking_id;
	u32 x;
	u32 y;
	u32 tool_x;
	u32 tool_y;
	u16 pressure;
	u16 touch_major;
	u16 touch_minor;
	u16 width_major;
	u16 width_minor;
	s16 orientation;
	u16 distance;
	u8 tool_type;
	u8 reserved;
};

static_assert(sizeof(struct hid_proxy_touch_slot_state) == 36);

struct hid_proxy_touch_slot {
	struct hid_proxy_touch_slot_state latest;
	struct hid_proxy_touch_slot_state target;
	s32 published_tracking_id;
};

static_assert(sizeof(struct hid_proxy_touch_slot) == 76);

struct hid_proxy_touch_handle {
	struct input_handle handle;
	struct list_head runtime_node;
	bool active;
	bool retiring;
	bool disconnecting;
	bool source_published;
	bool source_pending;
	bool reset_flush_pending;
	bool target_pending;
	bool target_started;
	bool resync_pending;
	bool drop_reported;
	bool combined_phase;
	u8 source_index;
	u8 flags;
	u8 latest_buttons;
	u8 target_buttons;
	u8 published_buttons;
	u8 phase;
	u16 supported_fields;
	u16 slot_count;
	u16 active_count;
	u16 phase_count;
	u16 phase_done;
	u16 scan_slot;
	u16 vendor;
	u16 product;
	u32 source_id;
	u32 next_frame_sequence;
	u32 frame_sequence;
	struct hid_proxy_touch_slot slots[];
};

struct hid_proxy_inflight {
	u8 kind;
	u8 report_id;
	u8 length;
	struct hid_proxy_source *source;
	u8 data[sizeof(struct hid_proxy_stylus_report)];
};

struct hid_proxy_touch_inflight {
	u8 kind;
	u8 report_id;
	u8 length;
	struct hid_proxy_touch_handle *source;
	u16 slot;
	u8 phase;
	u8 data[sizeof(struct hid_proxy_touch_report)];
};

struct hid_proxy_runtime {
	SemaphoreHandle_t mutex;
	struct input_handler handler;
	struct input_handler touch_handler;
	struct list_head sources;
	struct list_head released;
	struct list_head touch_sources;
	struct list_head touch_released;
	struct hid_proxy_inflight inflight;
	struct hid_proxy_touch_inflight touch_inflight;
	struct hid_proxy_stylus_report last_direct;
	struct hid_proxy_stylus_report last_indirect;
	struct hid_proxy_source_report last_source;
	struct hid_proxy_touch_report last_touch_direct;
	struct hid_proxy_touch_report last_touch_indirect;
	struct hid_proxy_touch_source_report last_touch_source;
	bool touch_drop_warning_pending;
	bool mounted;
};

static struct hid_proxy_runtime *hid_proxy;

static const u16 hid_proxy_tool_codes[] = {
	BTN_TOOL_PEN,
	BTN_TOOL_RUBBER,
	BTN_TOOL_BRUSH,
	BTN_TOOL_PENCIL,
	BTN_TOOL_AIRBRUSH,
	BTN_TOOL_MOUSE,
	BTN_TOOL_LENS,
};

static bool hid_proxy_is_tool_code(unsigned int code)
{
	for (size_t i = 0; i < ARRAY_SIZE(hid_proxy_tool_codes); i++)
		if (code == hid_proxy_tool_codes[i])
			return true;

	return false;
}

static void hid_proxy_axis_seed(struct hid_proxy_axis *axis,
				const struct input_dev *dev, unsigned int code)
{
	if (!test_bit(code, dev->absbit)) {
		memset(axis, 0, sizeof(*axis));
		return;
	}

	axis->value = dev->absinfo[code].value;
	axis->minimum = dev->absinfo[code].minimum;
	axis->maximum = dev->absinfo[code].maximum;
	axis->resolution = dev->absinfo[code].resolution;
}

static void hid_proxy_source_seed(struct hid_proxy_source *source,
				  const struct input_dev *dev)
{
	source->direct = test_bit(INPUT_PROP_DIRECT, dev->propbit);
	source->vendor = dev->id.vendor;
	source->product = dev->id.product;
	source->source_id = dev->port_proxy_id;
	source->touch = test_bit(BTN_TOUCH, dev->key);
	source->barrel = test_bit(BTN_STYLUS, dev->key);
	source->secondary_barrel = test_bit(BTN_STYLUS2, dev->key);
	source->stylus3 = test_bit(BTN_STYLUS3, dev->key);
	source->tool_code = 0;
	for (size_t i = 0; i < ARRAY_SIZE(hid_proxy_tool_codes); i++)
		if (test_bit(hid_proxy_tool_codes[i], dev->key)) {
			source->tool_code = hid_proxy_tool_codes[i];
			break;
		}

	hid_proxy_axis_seed(&source->x, dev, ABS_X);
	hid_proxy_axis_seed(&source->y, dev, ABS_Y);
	hid_proxy_axis_seed(&source->pressure, dev, ABS_PRESSURE);
	hid_proxy_axis_seed(&source->distance, dev, ABS_DISTANCE);
	hid_proxy_axis_seed(&source->tilt_x, dev, ABS_TILT_X);
	hid_proxy_axis_seed(&source->tilt_y, dev, ABS_TILT_Y);
	source->tool_id = test_bit(ABS_MISC, dev->absbit) ?
		(u32)dev->absinfo[ABS_MISC].value : 0;
}

static u32 hid_proxy_scale_unsigned(const struct hid_proxy_axis *axis,
				    u32 output_max)
{
	if (axis->maximum <= axis->minimum)
		return 0;

	s64 value = clamp(axis->value, axis->minimum, axis->maximum);
	u64 numerator = (u64)(value - axis->minimum) * output_max;
	u64 denominator = (u64)((s64)axis->maximum - axis->minimum);

	return (u32)(numerator / denominator);
}

static s16 hid_proxy_scale_tilt(const struct hid_proxy_axis *axis)
{
	if (axis->maximum <= axis->minimum)
		return 0;
	if (axis->resolution <= 0) {
		/* Active unitless UGEE templates retain only their logical range. */
		s64 value = clamp(axis->value, axis->minimum, axis->maximum);

		if (value < 0) {
			u64 denominator = (u64)-(s64)axis->minimum;
			u64 numerator = (u64)-value * 9000;

			return -(s16)((numerator + denominator / 2) / denominator);
		}
		if (value > 0) {
			u64 denominator = axis->maximum;
			u64 numerator = (u64)value * 9000;

			return (s16)((numerator + denominator / 2) / denominator);
		}
		return 0;
	}

	s64 value = clamp(axis->value, axis->minimum, axis->maximum);
	s64 centidegrees = value * HID_PROXY_CENTIDEGREES_PER_RADIAN /
		axis->resolution;

	return (s16)clamp(centidegrees, (s64)-9000, (s64)9000);
}

static struct hid_proxy_stylus_report
hid_proxy_stylus_report(const struct hid_proxy_source *source, bool neutral)
{
	bool rubber = source->tool_code == BTN_TOOL_RUBBER;
	bool in_range = source->tool_code != 0;
	u8 switches = 0;

	if (source->touch && !rubber)
		switches |= HID_PROXY_STYLUS_TIP;
	if (in_range)
		switches |= HID_PROXY_STYLUS_IN_RANGE;
	if (source->barrel)
		switches |= HID_PROXY_STYLUS_BARREL;
	if (rubber)
		switches |= HID_PROXY_STYLUS_INVERT;
	if (source->touch && rubber)
		switches |= HID_PROXY_STYLUS_ERASER;
	if (source->secondary_barrel)
		switches |= HID_PROXY_STYLUS_SECONDARY_BARREL;

	struct hid_proxy_stylus_report report = {
		.switches = switches,
		.transducer_index = source->transducer_index,
		.x = cpu_to_le32(hid_proxy_scale_unsigned(&source->x, INT32_MAX)),
		.y = cpu_to_le32(hid_proxy_scale_unsigned(&source->y, INT32_MAX)),
		.pressure = cpu_to_le16((u16)hid_proxy_scale_unsigned(&source->pressure,
								 UINT16_MAX)),
		.distance = cpu_to_le16((u16)hid_proxy_scale_unsigned(&source->distance,
								 UINT16_MAX)),
		.tilt_x = cpu_to_le16((u16)hid_proxy_scale_tilt(&source->tilt_x)),
		.tilt_y = cpu_to_le16((u16)hid_proxy_scale_tilt(&source->tilt_y)),
		.serial = cpu_to_le32(source->serial),
	};
	if (neutral) {
		report.switches = 0;
		report.pressure = 0;
		report.distance = 0;
		report.tilt_x = 0;
		report.tilt_y = 0;
	}

	return report;
}

static struct hid_proxy_source_report
hid_proxy_source_report(const struct hid_proxy_source *source, bool present)
{
	u8 flags = 0;

	if (present)
		flags |= HID_PROXY_SOURCE_PRESENT;
	if (present && source->stylus3)
		flags |= HID_PROXY_SOURCE_STYLUS3;
	if (source->direct)
		flags |= HID_PROXY_SOURCE_DIRECT;
	if ((source->tilt_x.maximum > source->tilt_x.minimum &&
	     source->tilt_x.resolution <= 0) ||
	    (source->tilt_y.maximum > source->tilt_y.minimum &&
	     source->tilt_y.resolution <= 0))
		flags |= HID_PROXY_SOURCE_TILT_NORMALIZED;

	struct hid_proxy_source_report report = {
		.version = 1,
		.transducer_index = source->transducer_index,
		.flags = flags,
		.source_id = cpu_to_le32(source->source_id),
		.vendor = cpu_to_le16(source->vendor),
		.product = cpu_to_le16(source->product),
		.tool_code = cpu_to_le16(source->tool_code),
		.tool_id = cpu_to_le32(source->tool_id),
	};

	return report;
}

static u32 hid_proxy_touch_scale_unsigned(const struct input_dev *dev,
					  unsigned int code, int value,
					  u32 output_max)
{
	const struct input_absinfo *info = &dev->absinfo[code];

	if (info->maximum <= info->minimum)
		return 0;

	s64 clamped = clamp(value, info->minimum, info->maximum);
	u64 numerator = (u64)(clamped - info->minimum) * output_max;
	u64 denominator = (u64)((s64)info->maximum - info->minimum);

	return (u32)(numerator / denominator);
}

static s16 hid_proxy_touch_scale_orientation(const struct input_dev *dev,
					      int value)
{
	const struct input_absinfo *info = &dev->absinfo[ABS_MT_ORIENTATION];

	if (info->maximum <= info->minimum)
		return 0;

	s64 clamped = clamp(value, info->minimum, info->maximum);
	u64 numerator = (u64)(clamped - info->minimum) * UINT16_MAX;
	u64 denominator = (u64)((s64)info->maximum - info->minimum);
	u32 scaled = (u32)(numerator / denominator);

	return (s16)((s32)scaled + INT16_MIN);
}

static u8 hid_proxy_touch_buttons(const struct input_dev *dev)
{
	u8 buttons = 0;

	if (test_bit(BTN_LEFT, dev->key))
		buttons |= 1u << 0;
	if (test_bit(BTN_RIGHT, dev->key))
		buttons |= 1u << 1;
	if (test_bit(BTN_MIDDLE, dev->key))
		buttons |= 1u << 2;
	if (test_bit(BTN_SIDE, dev->key))
		buttons |= 1u << 3;
	if (test_bit(BTN_EXTRA, dev->key))
		buttons |= 1u << 4;

	return buttons;
}

static u16 hid_proxy_touch_supported_fields(const struct input_dev *dev)
{
	u16 supported = HID_PROXY_TOUCH_POSITION_X |
		HID_PROXY_TOUCH_POSITION_Y;

	if (test_bit(ABS_MT_PRESSURE, dev->absbit))
		supported |= HID_PROXY_TOUCH_PRESSURE;
	if (test_bit(ABS_MT_TOUCH_MAJOR, dev->absbit))
		supported |= HID_PROXY_TOUCH_MAJOR;
	if (test_bit(ABS_MT_TOUCH_MINOR, dev->absbit))
		supported |= HID_PROXY_TOUCH_MINOR;
	if (test_bit(ABS_MT_WIDTH_MAJOR, dev->absbit))
		supported |= HID_PROXY_TOUCH_WIDTH_MAJOR;
	if (test_bit(ABS_MT_WIDTH_MINOR, dev->absbit))
		supported |= HID_PROXY_TOUCH_WIDTH_MINOR;
	if (test_bit(ABS_MT_ORIENTATION, dev->absbit))
		supported |= HID_PROXY_TOUCH_ORIENTATION;
	if (test_bit(ABS_MT_DISTANCE, dev->absbit))
		supported |= HID_PROXY_TOUCH_DISTANCE;
	if (test_bit(ABS_MT_TOOL_X, dev->absbit))
		supported |= HID_PROXY_TOUCH_TOOL_X;
	if (test_bit(ABS_MT_TOOL_Y, dev->absbit))
		supported |= HID_PROXY_TOUCH_TOOL_Y;
	if (test_bit(ABS_MT_TOOL_TYPE, dev->absbit))
		supported |= HID_PROXY_TOUCH_TOOL_TYPE;
	if (test_bit(BTN_LEFT, dev->keybit) ||
	    test_bit(BTN_RIGHT, dev->keybit) ||
	    test_bit(BTN_MIDDLE, dev->keybit) ||
	    test_bit(BTN_SIDE, dev->keybit) ||
	    test_bit(BTN_EXTRA, dev->keybit))
		supported |= HID_PROXY_TOUCH_BUTTONS;

	return supported;
}

static struct hid_proxy_touch_slot_state
hid_proxy_touch_slot_state(const struct input_dev *dev,
			   const struct input_mt_slot *slot,
			   u16 supported)
{
	struct hid_proxy_touch_slot_state state = {
		.tracking_id = input_mt_get_value(slot, ABS_MT_TRACKING_ID),
	};

	if (state.tracking_id < 0)
		return state;

	state.x = hid_proxy_touch_scale_unsigned(dev, ABS_MT_POSITION_X,
		input_mt_get_value(slot, ABS_MT_POSITION_X), INT32_MAX);
	state.y = hid_proxy_touch_scale_unsigned(dev, ABS_MT_POSITION_Y,
		input_mt_get_value(slot, ABS_MT_POSITION_Y), INT32_MAX);
	if (supported & HID_PROXY_TOUCH_PRESSURE)
		state.pressure = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_PRESSURE,
			input_mt_get_value(slot, ABS_MT_PRESSURE), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_MAJOR)
		state.touch_major = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_TOUCH_MAJOR,
			input_mt_get_value(slot, ABS_MT_TOUCH_MAJOR), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_MINOR)
		state.touch_minor = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_TOUCH_MINOR,
			input_mt_get_value(slot, ABS_MT_TOUCH_MINOR), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_WIDTH_MAJOR)
		state.width_major = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_WIDTH_MAJOR,
			input_mt_get_value(slot, ABS_MT_WIDTH_MAJOR), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_WIDTH_MINOR)
		state.width_minor = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_WIDTH_MINOR,
			input_mt_get_value(slot, ABS_MT_WIDTH_MINOR), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_ORIENTATION)
		state.orientation = hid_proxy_touch_scale_orientation(dev,
			input_mt_get_value(slot, ABS_MT_ORIENTATION));
	if (supported & HID_PROXY_TOUCH_DISTANCE)
		state.distance = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_DISTANCE,
			input_mt_get_value(slot, ABS_MT_DISTANCE), UINT16_MAX);
	if (supported & HID_PROXY_TOUCH_TOOL_X)
		state.tool_x = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_TOOL_X,
			input_mt_get_value(slot, ABS_MT_TOOL_X), INT32_MAX);
	if (supported & HID_PROXY_TOUCH_TOOL_Y)
		state.tool_y = hid_proxy_touch_scale_unsigned(dev,
			ABS_MT_TOOL_Y,
			input_mt_get_value(slot, ABS_MT_TOOL_Y), INT32_MAX);
	if (supported & HID_PROXY_TOUCH_TOOL_TYPE)
		state.tool_type = clamp(input_mt_get_value(slot,
			ABS_MT_TOOL_TYPE), 0, UINT8_MAX);

	return state;
}

static struct hid_proxy_touch_source_report
hid_proxy_touch_source_report(const struct hid_proxy_touch_handle *source,
			      bool present)
{
	u8 flags = source->flags;

	if (present)
		flags |= HID_PROXY_TOUCH_SOURCE_PRESENT;

	struct hid_proxy_touch_source_report report = {
		.version = 1,
		.source_index = source->source_index,
		.flags = flags,
		.supported_fields = cpu_to_le16(source->supported_fields),
		.source_id = cpu_to_le32(source->source_id),
		.vendor = cpu_to_le16(source->vendor),
		.product = cpu_to_le16(source->product),
		.slot_count = cpu_to_le16(source->slot_count),
	};

	return report;
}

static bool hid_proxy_touch_release_needed(
		const struct hid_proxy_touch_slot *slot)
{
	return slot->published_tracking_id >= 0 &&
		slot->published_tracking_id != slot->target.tracking_id;
}

static void hid_proxy_touch_prepare_target_locked(
		struct hid_proxy_touch_handle *source)
{
	u16 release_count = 0;
	u16 total;

	source->active_count = 0;
	for (u16 i = 0; i < source->slot_count; i++) {
		source->slots[i].target = source->slots[i].latest;
		if (hid_proxy_touch_release_needed(&source->slots[i]))
			release_count++;
		if (source->slots[i].target.tracking_id >= 0)
			source->active_count++;
	}
	source->target_buttons = source->latest_buttons;
	source->target_pending = true;
	source->target_started = false;
	source->phase_done = 0;
	source->scan_slot = 0;
	total = release_count + source->active_count;

	if (!total) {
		if (source->target_buttons != source->published_buttons) {
			source->phase = HID_PROXY_TOUCH_PHASE_DUMMY;
			source->phase_count = 1;
			source->combined_phase = false;
		} else {
			source->target_pending = false;
			source->phase = HID_PROXY_TOUCH_PHASE_NONE;
		}
	} else if (total <= HID_PROXY_TOUCH_MAX_CONTACTS) {
		source->phase = release_count ?
			HID_PROXY_TOUCH_PHASE_RELEASE :
			HID_PROXY_TOUCH_PHASE_ACTIVE;
		source->phase_count = total;
		source->combined_phase = release_count &&
			source->active_count;
	} else {
		source->phase = release_count ?
			HID_PROXY_TOUCH_PHASE_RELEASE :
			HID_PROXY_TOUCH_PHASE_ACTIVE;
		source->phase_count = release_count ?
			release_count : source->active_count;
		source->combined_phase = false;
	}

	if (source->target_pending)
		source->frame_sequence = source->next_frame_sequence++;
}

static void hid_proxy_touch_complete_target_locked(
		struct hid_proxy_touch_handle *source)
{
	for (u16 i = 0; i < source->slot_count; i++)
		source->slots[i].published_tracking_id =
			source->slots[i].target.tracking_id;
	source->published_buttons = source->target_buttons;
	source->target_pending = false;
	source->target_started = false;
	source->phase = HID_PROXY_TOUCH_PHASE_NONE;

	if (source->resync_pending) {
		source->resync_pending = false;
		hid_proxy_touch_prepare_target_locked(source);
		if (!source->target_pending)
			source->drop_reported = false;
	} else {
		source->drop_reported = false;
	}
}

static bool hid_proxy_touch_snapshot_locked(
		struct hid_proxy_touch_handle *source,
		const struct input_dev *dev)
{
	bool changed = false;
	u8 buttons = hid_proxy_touch_buttons(dev);

	for (u16 i = 0; i < source->slot_count; i++) {
		struct hid_proxy_touch_slot_state state =
			hid_proxy_touch_slot_state(dev, &dev->mt->slots[i],
						   source->supported_fields);

		if (memcmp(&source->slots[i].latest, &state, sizeof(state))) {
			source->slots[i].latest = state;
			changed = true;
		}
	}
	if (source->latest_buttons != buttons) {
		source->latest_buttons = buttons;
		changed = true;
	}

	return changed;
}

static bool hid_proxy_touch_queue_latest_locked(
		struct hid_proxy_runtime *runtime,
		struct hid_proxy_touch_handle *source)
{
	bool warn = false;

	if (!runtime->mounted)
		return false;
	if (source->reset_flush_pending) {
		if (source->resync_pending && !source->drop_reported) {
			source->drop_reported = true;
			warn = true;
		}
		source->resync_pending = true;
		return warn;
	}
	if (!source->target_pending) {
		source->resync_pending = false;
		hid_proxy_touch_prepare_target_locked(source);
		return false;
	}

	if (source->resync_pending && !source->drop_reported) {
		source->drop_reported = true;
		warn = true;
	}
	source->resync_pending = true;

	return warn;
}

static u8 hid_proxy_touch_aggregate_buttons_locked(
		const struct hid_proxy_runtime *runtime,
		const struct hid_proxy_touch_handle *source)
{
	struct hid_proxy_touch_handle *other;
	u8 buttons = source->target_buttons;
	u8 topology = source->flags & HID_PROXY_TOUCH_SOURCE_DIRECT;

	list_for_each_entry(other, &runtime->touch_sources, runtime_node) {
		if (other == source)
			continue;
		if ((other->flags & HID_PROXY_TOUCH_SOURCE_DIRECT) != topology)
			continue;
		buttons |= other->published_buttons;
	}

	return buttons;
}

static struct hid_proxy_touch_report
hid_proxy_touch_contact_report(const struct hid_proxy_runtime *runtime,
			       const struct hid_proxy_touch_handle *source,
			       u16 slot_index, bool release, bool dummy)
{
	const struct hid_proxy_touch_slot_state *state =
		&source->slots[slot_index].target;
	u32 contact_id = 0;
	u8 switches = 0;

	if (release) {
		contact_id = ((u32)source->source_index << 16) |
			(u16)source->slots[slot_index].published_tracking_id;
	} else if (dummy) {
		contact_id = (u32)source->source_index << 16;
	} else {
		contact_id = ((u32)source->source_index << 16) |
			(u16)state->tracking_id;
		switches |= HID_PROXY_TOUCH_IN_RANGE;
		if (!(source->supported_fields & HID_PROXY_TOUCH_DISTANCE) ||
		    !state->distance)
			switches |= HID_PROXY_TOUCH_TIP;
		if (!(source->supported_fields & HID_PROXY_TOUCH_TOOL_TYPE) ||
		    state->tool_type != MT_TOOL_PALM)
			switches |= HID_PROXY_TOUCH_CONFIDENCE;
	}

	struct hid_proxy_touch_report report = {
		.switches = switches,
		.contact_id = cpu_to_le32(contact_id),
		.contact_count = source->phase_done ? 0 : source->phase_count,
		.buttons = hid_proxy_touch_aggregate_buttons_locked(runtime,
							       source),
		.frame_sequence = cpu_to_le32(source->frame_sequence),
		.slot = cpu_to_le16(dummy ? UINT16_MAX : slot_index),
		.source_buttons = source->target_buttons,
	};
	if (!release && !dummy) {
		report.x = cpu_to_le32(state->x);
		report.y = cpu_to_le32(state->y);
		report.pressure = cpu_to_le16(state->pressure);
		report.touch_major = cpu_to_le16(state->touch_major);
		report.touch_minor = cpu_to_le16(state->touch_minor);
		report.tool_type = state->tool_type;
		report.width_major = cpu_to_le16(state->width_major);
		report.width_minor = cpu_to_le16(state->width_minor);
		report.orientation = cpu_to_le16((u16)state->orientation);
		report.distance = cpu_to_le16(state->distance);
		report.tool_x = cpu_to_le32(state->tool_x);
		report.tool_y = cpu_to_le32(state->tool_y);
	}

	return report;
}

static bool hid_proxy_match(struct input_handler *handler,
			    struct input_dev *dev)
{
	(void)handler;

	return test_bit(BTN_TOOL_PEN, dev->keybit) ||
		test_bit(BTN_TOOL_RUBBER, dev->keybit) ||
		test_bit(BTN_TOOL_BRUSH, dev->keybit) ||
		test_bit(BTN_TOOL_PENCIL, dev->keybit) ||
		test_bit(BTN_TOOL_AIRBRUSH, dev->keybit);
}

static bool hid_proxy_touch_match(struct input_handler *handler,
				  struct input_dev *dev)
{
	(void)handler;

	return dev->mt != NULL;
}

static u8 hid_proxy_touch_claim_index_locked(
		struct hid_proxy_runtime *runtime)
{
	for (u16 index = 1; index <= UINT8_MAX; index++) {
		struct hid_proxy_touch_handle *source;
		bool used = false;

		list_for_each_entry(source, &runtime->touch_sources,
				    runtime_node) {
			if (source->source_index == index) {
				used = true;
				break;
			}
		}
		if (!used)
			return index;
	}

	__builtin_unreachable();
}

static int hid_proxy_touch_connect(struct input_handler *handler,
				   struct input_dev *dev,
				   const struct input_device_id *id)
{
	struct hid_proxy_runtime *runtime = handler->private;
	struct hid_proxy_touch_handle *source;

	(void)id;

	if (dev->mt->num_slots > HID_PROXY_TOUCH_MAX_CONTACTS) {
		async_msg("ERR: HID_PROXY_TOUCH_SLOTS");
		return -E2BIG;
	}

	source = kzalloc_flex(*source, slots, dev->mt->num_slots);
	if (!source) {
		async_msg("ERR: HID_PROXY_TOUCH_NOMEM");
		return -ENOMEM;
	}

	source->slot_count = dev->mt->num_slots;
	source->supported_fields = hid_proxy_touch_supported_fields(dev);
	source->source_id = dev->port_proxy_id;
	source->vendor = dev->id.vendor;
	source->product = dev->id.product;
	source->next_frame_sequence = 1;
	if (test_bit(INPUT_PROP_DIRECT, dev->propbit))
		source->flags |= HID_PROXY_TOUCH_SOURCE_DIRECT;
	if (test_bit(INPUT_PROP_POINTER, dev->propbit))
		source->flags |= HID_PROXY_TOUCH_SOURCE_POINTER;
	if (test_bit(INPUT_PROP_SEMI_MT, dev->propbit))
		source->flags |= HID_PROXY_TOUCH_SOURCE_SEMI_MT;
	if (test_bit(INPUT_PROP_BUTTONPAD, dev->propbit))
		source->flags |= HID_PROXY_TOUCH_SOURCE_BUTTONPAD;
	if (test_bit(INPUT_PROP_PRESSUREPAD, dev->propbit))
		source->flags |= HID_PROXY_TOUCH_SOURCE_PRESSUREPAD;
	for (u16 i = 0; i < source->slot_count; i++) {
		source->slots[i].latest.tracking_id = -1;
		source->slots[i].target.tracking_id = -1;
		source->slots[i].published_tracking_id = -1;
	}

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	source->source_index = hid_proxy_touch_claim_index_locked(runtime);
	list_add_tail(&source->runtime_node, &runtime->touch_sources);
	xSemaphoreGive(runtime->mutex);

	source->handle.dev = dev;
	source->handle.name = handler->name;
	source->handle.handler = handler;
	source->handle.private = source;
	(void)input_register_handle(&source->handle);

	return 0;
}

static unsigned int hid_proxy_touch_events(struct input_handle *handle,
					   struct input_value *vals,
					   unsigned int count)
{
	struct hid_proxy_touch_handle *source = handle->private;
	struct hid_proxy_runtime *runtime = handle->handler->private;

	for (struct input_value *event = vals; event != vals + count; event++) {
		if (event->type != EV_SYN || event->code != SYN_REPORT)
			continue;

		xSemaphoreTake(runtime->mutex, portMAX_DELAY);
		if (source->active &&
		    hid_proxy_touch_snapshot_locked(source, handle->dev) &&
		    hid_proxy_touch_queue_latest_locked(runtime, source))
			runtime->touch_drop_warning_pending = true;
		xSemaphoreGive(runtime->mutex);
	}

	/* Observe the completed Linux Type-B frame without filtering evdev. */
	return count;
}

static void hid_proxy_touch_neutralize_latest_locked(
		struct hid_proxy_touch_handle *source)
{
	for (u16 i = 0; i < source->slot_count; i++) {
		memset(&source->slots[i].latest, 0,
		       sizeof(source->slots[i].latest));
		source->slots[i].latest.tracking_id = -1;
	}
	source->latest_buttons = 0;
}

static void hid_proxy_touch_disconnect(struct input_handle *handle)
{
	struct hid_proxy_touch_handle *source = handle->private;
	struct hid_proxy_runtime *runtime = handle->handler->private;
	bool release = false;

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	source->active = false;
	source->retiring = true;
	source->disconnecting = true;
	hid_proxy_touch_neutralize_latest_locked(source);
	if (source->source_published ||
	    runtime->touch_inflight.source == source) {
		if (source->reset_flush_pending) {
			if (source->resync_pending &&
			    !source->drop_reported) {
				source->drop_reported = true;
				runtime->touch_drop_warning_pending = true;
			}
			source->resync_pending = true;
		} else if (source->target_pending) {
			if (source->resync_pending &&
			    !source->drop_reported) {
				source->drop_reported = true;
				runtime->touch_drop_warning_pending = true;
			}
			source->resync_pending = true;
		} else {
			source->resync_pending = false;
			hid_proxy_touch_prepare_target_locked(source);
		}
	}
	xSemaphoreGive(runtime->mutex);

	if (handle->open)
		input_close_device(handle);
	input_unregister_handle(handle);

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	source->disconnecting = false;
	if (!source->source_published &&
	    runtime->touch_inflight.source != source) {
		list_del(&source->runtime_node);
		release = true;
	}
	xSemaphoreGive(runtime->mutex);

	if (release)
		kfree(source);
}

static const struct input_device_id hid_proxy_touch_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			 INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.bustype = BUS_USB,
		.evbit = {
			[BIT_WORD(EV_ABS)] = BIT_MASK(EV_ABS),
		},
		.absbit = {
			[BIT_WORD(ABS_MT_SLOT)] = BIT_MASK(ABS_MT_SLOT) |
				BIT_MASK(ABS_MT_TRACKING_ID) |
				BIT_MASK(ABS_MT_POSITION_X) |
				BIT_MASK(ABS_MT_POSITION_Y),
		},
	},
	{ }
};

static void hid_proxy_reset_frames_locked(struct hid_proxy_source *source)
{
	source->frame_head = 0;
	source->frame_count = 0;
	source->source_dirty = false;
	source->drop_reported = false;
}

static bool hid_proxy_queue_frame_locked(struct hid_proxy_source *source,
					 bool force_source)
{
	bool source_pending = force_source || source->source_dirty;
	bool warn = false;
	u8 index;

	if (source->frame_count == HID_PROXY_FRAME_QUEUE_DEPTH) {
		index = (source->frame_head + source->frame_count - 1) %
			HID_PROXY_FRAME_QUEUE_DEPTH;
		source_pending |= source->frames[index].source_pending;
		if (!source->drop_reported) {
			source->drop_reported = true;
			warn = true;
		}
	} else {
		index = (source->frame_head + source->frame_count) %
			HID_PROXY_FRAME_QUEUE_DEPTH;
		source->frame_count++;
	}

	source->frames[index].stylus = hid_proxy_stylus_report(source, false);
	source->frames[index].source = hid_proxy_source_report(source, true);
	source->frames[index].source_pending = source_pending;
	source->source_dirty = false;

	return warn;
}

static int hid_proxy_claim_index_locked(struct hid_proxy_runtime *runtime)
{
	for (u16 index = 1; index <= UINT8_MAX; index++) {
		struct hid_proxy_handle *proxy_handle;
		bool used = false;

		list_for_each_entry(proxy_handle, &runtime->sources, runtime_node) {
			if (proxy_handle->source.transducer_index == index) {
				used = true;
				break;
			}
		}
		if (!used)
			return index;
	}

	return -ENOSPC;
}

static int hid_proxy_connect(struct input_handler *handler,
			     struct input_dev *dev,
			     const struct input_device_id *id)
{
	struct hid_proxy_runtime *runtime = handler->private;
	struct hid_proxy_handle *proxy_handle;
	int index;

	(void)id;

	proxy_handle = kzalloc_obj(struct hid_proxy_handle);
	if (!proxy_handle) {
		async_msg("ERR: HID_PROXY_CONNECT_NOMEM");
		return -ENOMEM;
	}

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	index = hid_proxy_claim_index_locked(runtime);
	if (index >= 0) {
		proxy_handle->source.transducer_index = index;
		list_add_tail(&proxy_handle->runtime_node, &runtime->sources);
	}
	xSemaphoreGive(runtime->mutex);
	if (index < 0) {
		async_msg("ERR: HID_PROXY_INDEX_FULL");
		kfree(proxy_handle);
		return index;
	}

	proxy_handle->handle.dev = dev;
	proxy_handle->handle.name = handler->name;
	proxy_handle->handle.handler = handler;
	proxy_handle->handle.private = proxy_handle;

	(void)input_register_handle(&proxy_handle->handle);

	return 0;
}

static unsigned int hid_proxy_events(struct input_handle *handle,
				     struct input_value *vals,
				     unsigned int count)
{
	struct hid_proxy_handle *proxy_handle = handle->private;
	struct hid_proxy_runtime *runtime = handle->handler->private;
	struct hid_proxy_source *source = &proxy_handle->source;
	struct input_value *event;
	bool warn = false;

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	for (event = vals; event != vals + count; event++) {
		switch (event->type) {
		case EV_KEY:
			if (hid_proxy_is_tool_code(event->code)) {
				u16 tool_code = source->tool_code;

				if (event->value)
					tool_code = event->code;
				else if (tool_code == event->code)
					tool_code = 0;
				if (tool_code != source->tool_code) {
					source->tool_code = tool_code;
					source->source_dirty = true;
				}
			} else if (event->code == BTN_TOUCH) {
				source->touch = event->value;
			} else if (event->code == BTN_STYLUS) {
				source->barrel = event->value;
			} else if (event->code == BTN_STYLUS2) {
				source->secondary_barrel = event->value;
			} else if (event->code == BTN_STYLUS3) {
				bool stylus3 = event->value;

				if (stylus3 != source->stylus3) {
					source->stylus3 = stylus3;
					source->source_dirty = true;
				}
			}
			break;
		case EV_ABS:
			switch (event->code) {
			case ABS_X:
				source->x.value = event->value;
				break;
			case ABS_Y:
				source->y.value = event->value;
				break;
			case ABS_PRESSURE:
				source->pressure.value = event->value;
				break;
			case ABS_DISTANCE:
				source->distance.value = event->value;
				break;
			case ABS_TILT_X:
				source->tilt_x.value = event->value;
				break;
			case ABS_TILT_Y:
				source->tilt_y.value = event->value;
				break;
			case ABS_MISC:
				if (source->tool_id != (u32)event->value) {
					source->tool_id = (u32)event->value;
					source->source_dirty = true;
				}
				break;
			}
			break;
		case EV_MSC:
			if (event->code == MSC_SERIAL)
				source->serial = (u32)event->value;
			break;
		case EV_SYN:
			if (event->code == SYN_REPORT && source->active &&
			    runtime->mounted) {
				warn |= hid_proxy_queue_frame_locked(source, false);
			}
			break;
		}
	}

	xSemaphoreGive(runtime->mutex);
	if (warn)
		async_msg("WARN: HID_PROXY_FRAME_DROP");

	/* Observe stylus values without removing unrepresented events from evdev. */
	return count;
}

static void hid_proxy_disconnect(struct input_handle *handle)
{
	struct hid_proxy_handle *proxy_handle = handle->private;
	struct hid_proxy_runtime *runtime = handle->handler->private;
	struct hid_proxy_source *source = &proxy_handle->source;
	bool present_inflight;
	bool release = false;

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	source->active = false;
	xSemaphoreGive(runtime->mutex);

	if (handle->open)
		input_close_device(handle);
	input_unregister_handle(handle);

	xSemaphoreTake(runtime->mutex, portMAX_DELAY);
	present_inflight = runtime->inflight.source == source &&
		runtime->inflight.kind == HID_PROXY_INFLIGHT_SOURCE_PRESENT;
	hid_proxy_reset_frames_locked(source);
	if (runtime->mounted && (source->published || present_inflight)) {
		source->retiring = true;
		source->terminal_pending = true;
	} else {
		list_del(&proxy_handle->runtime_node);
		release = true;
	}
	xSemaphoreGive(runtime->mutex);

	if (release)
		kfree(proxy_handle);
}

static const struct input_device_id hid_proxy_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_BUS |
			 INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_ABSBIT,
		.bustype = BUS_USB,
		.evbit = {
			[BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY) |
					      BIT_MASK(EV_ABS),
		},
		.absbit = {
			[BIT_WORD(ABS_X)] = BIT_MASK(ABS_X) |
					       BIT_MASK(ABS_Y) |
					       BIT_MASK(ABS_PRESSURE),
		},
	},
	{ }
};

int hid_proxy_runtime_init(void)
{
	struct hid_proxy_runtime *runtime;

	runtime = pvPortMalloc(sizeof(*runtime));
	if (!runtime)
		return -ENOMEM;
	memset(runtime, 0, sizeof(*runtime));

	runtime->mutex = xSemaphoreCreateMutex();
	if (!runtime->mutex) {
		vPortFree(runtime);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&runtime->sources);
	INIT_LIST_HEAD(&runtime->released);
	INIT_LIST_HEAD(&runtime->touch_sources);
	INIT_LIST_HEAD(&runtime->touch_released);
	runtime->handler.private = runtime;
	runtime->handler.events = hid_proxy_events;
	runtime->handler.match = hid_proxy_match;
	runtime->handler.connect = hid_proxy_connect;
	runtime->handler.disconnect = hid_proxy_disconnect;
	runtime->handler.name = "hid-proxy-stylus";
	runtime->handler.id_table = hid_proxy_ids;
	runtime->touch_handler.private = runtime;
	runtime->touch_handler.events = hid_proxy_touch_events;
	runtime->touch_handler.match = hid_proxy_touch_match;
	runtime->touch_handler.connect = hid_proxy_touch_connect;
	runtime->touch_handler.disconnect = hid_proxy_touch_disconnect;
	runtime->touch_handler.name = "hid-proxy-touch";
	runtime->touch_handler.id_table = hid_proxy_touch_ids;
	runtime->last_source.version = 1;
	runtime->last_touch_source.version = 1;
	hid_proxy = runtime;

	return 0;
}

void hid_proxy_input_init(void)
{
	if (!hid_proxy)
		return;

	(void)input_register_handler(&hid_proxy->handler);
	(void)input_register_handler(&hid_proxy->touch_handler);
}

static void hid_proxy_reset_reports_locked(struct hid_proxy_runtime *runtime)
{
	memset(&runtime->inflight, 0, sizeof(runtime->inflight));
	memset(&runtime->last_direct, 0, sizeof(runtime->last_direct));
	memset(&runtime->last_indirect, 0, sizeof(runtime->last_indirect));
	memset(&runtime->last_source, 0, sizeof(runtime->last_source));
	runtime->last_source.version = 1;
	memset(&runtime->touch_inflight, 0, sizeof(runtime->touch_inflight));
	memset(&runtime->last_touch_direct, 0,
	       sizeof(runtime->last_touch_direct));
	memset(&runtime->last_touch_indirect, 0,
	       sizeof(runtime->last_touch_indirect));
	memset(&runtime->last_touch_source, 0,
	       sizeof(runtime->last_touch_source));
	runtime->last_touch_source.version = 1;
	runtime->touch_drop_warning_pending = false;
}

static void hid_proxy_touch_clear_target_locked(
		struct hid_proxy_touch_handle *source)
{
	source->target_pending = false;
	source->target_started = false;
	source->resync_pending = false;
	source->drop_reported = false;
	source->combined_phase = false;
	source->phase = HID_PROXY_TOUCH_PHASE_NONE;
	source->active_count = 0;
	source->phase_count = 0;
	source->phase_done = 0;
	source->scan_slot = 0;
}

static void hid_proxy_touch_forget_downstream_locked(
		struct hid_proxy_touch_handle *source)
{
	source->source_published = false;
	source->source_pending = false;
	source->reset_flush_pending = false;
	source->published_buttons = 0;
	hid_proxy_touch_clear_target_locked(source);
	for (u16 i = 0; i < source->slot_count; i++)
		source->slots[i].published_tracking_id = -1;
}

static void hid_proxy_touch_reconcile_remount_locked(
		struct hid_proxy_touch_handle *source)
{
	if (source->target_started) {
		source->published_buttons = source->target_buttons;
		if (source->phase == HID_PROXY_TOUCH_PHASE_ACTIVE)
			for (u16 i = 0; i < source->slot_count; i++)
				source->slots[i].published_tracking_id =
					source->slots[i].target.tracking_id;
	}
	hid_proxy_touch_clear_target_locked(source);
	source->target_buttons = source->latest_buttons;
	if (source->source_published) {
		source->reset_flush_pending = true;
		source->frame_sequence = source->next_frame_sequence++;
	} else {
		source->reset_flush_pending = false;
		if (source->active)
			hid_proxy_touch_prepare_target_locked(source);
	}
}

void hid_proxy_activate_hid(struct hid_device *hid)
{
	struct input_handle *handle;

	if (!hid_proxy)
		return;

	list_for_each_entry(handle, &hid_proxy->handler.h_list, h_node) {
		struct hid_proxy_handle *proxy_handle = handle->private;
		struct input_dev *dev = handle->dev;
		struct hid_proxy_source *source = &proxy_handle->source;
		int error;

		if (input_get_drvdata(dev) != hid && dev->dev.parent != &hid->dev)
			continue;
		if (handle->open)
			continue;

		xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
		hid_proxy_source_seed(source, dev);
		xSemaphoreGive(hid_proxy->mutex);

		/* The proxy owns input independently of the optional evdev client. */
		error = input_open_device(handle);
		if (error) {
			async_msg("ERR: HID_PROXY_OPEN_FAIL");
			continue;
		}

		xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
		source->active = true;
		source->retiring = false;
		source->terminal_pending = false;
		hid_proxy_reset_frames_locked(source);
		if (hid_proxy->mounted) {
			(void)hid_proxy_queue_frame_locked(source, true);
		}
		xSemaphoreGive(hid_proxy->mutex);
	}

	list_for_each_entry(handle, &hid_proxy->touch_handler.h_list, h_node) {
		struct hid_proxy_touch_handle *source = handle->private;
		struct input_dev *dev = handle->dev;
		int error;

		if (input_get_drvdata(dev) != hid && dev->dev.parent != &hid->dev)
			continue;
		if (handle->open)
			continue;

		mutex_lock(&dev->port_event_mutex);
		error = input_open_device(handle);
		if (error) {
			mutex_unlock(&dev->port_event_mutex);
			async_msg("ERR: HID_PROXY_TOUCH_OPEN_FAIL");
			continue;
		}

		xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
		source->active = true;
		source->retiring = false;
		source->disconnecting = false;
		hid_proxy_touch_forget_downstream_locked(source);
		source->source_pending = true;
		(void)hid_proxy_touch_snapshot_locked(source, dev);
		if (hid_proxy->mounted)
			hid_proxy_touch_prepare_target_locked(source);
		xSemaphoreGive(hid_proxy->mutex);
		mutex_unlock(&dev->port_event_mutex);
	}
}

void hid_proxy_tud_mount(void)
{
	struct hid_proxy_handle *proxy_handle;
	struct hid_proxy_handle *next;
	struct hid_proxy_touch_handle *touch_source;
	struct hid_proxy_touch_handle *touch_next;
	bool remount;

	if (!hid_proxy)
		return;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	remount = hid_proxy->mounted;
	hid_proxy->mounted = true;
	/* TinyUSB calls mount again after bus reset without a preceding umount. */
	hid_proxy_reset_reports_locked(hid_proxy);
	list_for_each_entry_safe(proxy_handle, next, &hid_proxy->sources,
				 runtime_node) {
		struct hid_proxy_source *source = &proxy_handle->source;

		if (source->retiring) {
			if (source->published) {
				source->terminal_pending = true;
			} else {
				list_del(&proxy_handle->runtime_node);
				list_add_tail(&proxy_handle->runtime_node,
					      &hid_proxy->released);
			}
			continue;
		}
		if (source->active) {
			hid_proxy_reset_frames_locked(source);
			(void)hid_proxy_queue_frame_locked(source, true);
		}
	}
	list_for_each_entry_safe(touch_source, touch_next,
				 &hid_proxy->touch_sources, runtime_node) {
		if (remount) {
			hid_proxy_touch_reconcile_remount_locked(touch_source);
			if (touch_source->active)
				touch_source->source_pending = true;
		} else {
			hid_proxy_touch_forget_downstream_locked(touch_source);
			if (touch_source->active) {
				touch_source->source_pending = true;
				hid_proxy_touch_prepare_target_locked(touch_source);
			}
		}

		if (touch_source->retiring) {
			if (!touch_source->source_published &&
			    !touch_source->disconnecting) {
				list_del(&touch_source->runtime_node);
				list_add_tail(&touch_source->runtime_node,
					      &hid_proxy->touch_released);
			}
		}
	}
	xSemaphoreGive(hid_proxy->mutex);
}

void hid_proxy_tud_umount(void)
{
	struct hid_proxy_handle *proxy_handle;
	struct hid_proxy_handle *next;
	struct hid_proxy_touch_handle *touch_source;
	struct hid_proxy_touch_handle *touch_next;

	if (!hid_proxy)
		return;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	hid_proxy->mounted = false;
	hid_proxy_reset_reports_locked(hid_proxy);
	list_for_each_entry_safe(proxy_handle, next, &hid_proxy->sources,
				 runtime_node) {
		struct hid_proxy_source *source = &proxy_handle->source;

		if (source->retiring) {
			list_del(&proxy_handle->runtime_node);
			list_add_tail(&proxy_handle->runtime_node,
				      &hid_proxy->released);
			continue;
		}
		source->published = false;
		source->terminal_pending = false;
		hid_proxy_reset_frames_locked(source);
	}
	list_for_each_entry_safe(touch_source, touch_next,
				 &hid_proxy->touch_sources, runtime_node) {
		hid_proxy_touch_forget_downstream_locked(touch_source);
		if (touch_source->retiring) {
			if (!touch_source->disconnecting) {
				list_del(&touch_source->runtime_node);
				list_add_tail(&touch_source->runtime_node,
					      &hid_proxy->touch_released);
			}
		}
	}
	xSemaphoreGive(hid_proxy->mutex);
}

static bool hid_proxy_prepare_report_locked(struct hid_proxy_runtime *runtime)
{
	struct hid_proxy_handle *proxy_handle;

	list_for_each_entry(proxy_handle, &runtime->sources, runtime_node) {
		struct hid_proxy_source *source = &proxy_handle->source;
		struct hid_proxy_inflight *inflight = &runtime->inflight;

		if (source->retiring) {
			if (source->terminal_pending) {
				struct hid_proxy_stylus_report report =
					hid_proxy_stylus_report(source, true);

				inflight->kind = HID_PROXY_INFLIGHT_STYLUS_NEUTRAL;
				inflight->report_id = source->direct ?
					REPORT_ID_STYLUS_DIRECT :
					REPORT_ID_STYLUS_INDIRECT;
				inflight->length = sizeof(report);
				memcpy(inflight->data, &report, sizeof(report));
			} else {
				struct hid_proxy_source_report report =
					hid_proxy_source_report(source, false);

				inflight->kind = HID_PROXY_INFLIGHT_SOURCE_REMOVED;
				inflight->report_id = REPORT_ID_STYLUS_SOURCE;
				inflight->length = sizeof(report);
				memcpy(inflight->data, &report, sizeof(report));
			}
		} else if (source->active && source->frame_count) {
			struct hid_proxy_frame *frame =
				&source->frames[source->frame_head];

			if (!source->published || frame->source_pending) {
				inflight->kind = HID_PROXY_INFLIGHT_SOURCE_PRESENT;
				inflight->report_id = REPORT_ID_STYLUS_SOURCE;
				inflight->length = sizeof(frame->source);
				memcpy(inflight->data, &frame->source,
				       sizeof(frame->source));
			} else {
				inflight->kind = HID_PROXY_INFLIGHT_STYLUS_FRAME;
				inflight->report_id = source->direct ?
					REPORT_ID_STYLUS_DIRECT :
					REPORT_ID_STYLUS_INDIRECT;
				inflight->length = sizeof(frame->stylus);
				memcpy(inflight->data, &frame->stylus,
				       sizeof(frame->stylus));
			}
		} else {
			continue;
		}

		inflight->source = source;
		list_del(&proxy_handle->runtime_node);
		list_add_tail(&proxy_handle->runtime_node, &runtime->sources);
		return true;
	}

	return false;
}

static bool hid_proxy_prepare_touch_contact_locked(
		struct hid_proxy_runtime *runtime,
		struct hid_proxy_touch_handle *source,
		struct hid_proxy_touch_inflight *inflight)
{
	struct hid_proxy_touch_report report;
	u16 slot;

	if (source->phase == HID_PROXY_TOUCH_PHASE_DUMMY) {
		report = hid_proxy_touch_contact_report(runtime, source, 0,
							false, true);
		inflight->slot = 0;
		inflight->phase = source->phase;
		goto prepared;
	}

again:
	for (slot = source->scan_slot; slot < source->slot_count; slot++) {
		bool selected = source->phase == HID_PROXY_TOUCH_PHASE_RELEASE ?
			hid_proxy_touch_release_needed(&source->slots[slot]) :
			source->slots[slot].target.tracking_id >= 0;

		if (!selected)
			continue;

		report = hid_proxy_touch_contact_report(runtime, source, slot,
			source->phase == HID_PROXY_TOUCH_PHASE_RELEASE, false);
		inflight->slot = slot;
		inflight->phase = source->phase;
		goto prepared;
	}

	if (source->phase == HID_PROXY_TOUCH_PHASE_RELEASE &&
	    source->combined_phase) {
		source->phase = HID_PROXY_TOUCH_PHASE_ACTIVE;
		source->scan_slot = 0;
		goto again;
	}

	return false;

prepared:
	inflight->kind = HID_PROXY_TOUCH_INFLIGHT_CONTACT;
	inflight->report_id = source->flags & HID_PROXY_TOUCH_SOURCE_DIRECT ?
		REPORT_ID_TOUCH_DIRECT : REPORT_ID_TOUCH_INDIRECT;
	inflight->length = sizeof(report);
	inflight->source = source;
	memcpy(inflight->data, &report, sizeof(report));
	source->target_started = true;

	return true;
}

static bool hid_proxy_prepare_touch_reset_flush_locked(
		struct hid_proxy_runtime *runtime,
		struct hid_proxy_touch_handle *source,
		struct hid_proxy_touch_inflight *inflight)
{
	struct hid_proxy_touch_report report =
		hid_proxy_touch_contact_report(runtime, source, 0, false, true);

	report.contact_id = cpu_to_le32(0x01000000u |
		((u32)source->source_index << 16));
	report.contact_count = 1;
	inflight->kind = HID_PROXY_TOUCH_INFLIGHT_CONTACT;
	inflight->report_id = source->flags & HID_PROXY_TOUCH_SOURCE_DIRECT ?
		REPORT_ID_TOUCH_DIRECT : REPORT_ID_TOUCH_INDIRECT;
	inflight->length = sizeof(report);
	inflight->source = source;
	inflight->slot = 0;
	inflight->phase = HID_PROXY_TOUCH_PHASE_RESET_FLUSH;
	memcpy(inflight->data, &report, sizeof(report));

	return true;
}

static bool hid_proxy_touch_topology_reset_pending_locked(
		struct hid_proxy_runtime *runtime,
		const struct hid_proxy_touch_handle *source)
{
	struct hid_proxy_touch_handle *other;
	u8 topology = source->flags & HID_PROXY_TOUCH_SOURCE_DIRECT;

	list_for_each_entry(other, &runtime->touch_sources, runtime_node)
		if ((other->flags & HID_PROXY_TOUCH_SOURCE_DIRECT) == topology &&
		    other->reset_flush_pending)
			return true;

	return false;
}

static bool hid_proxy_prepare_touch_source_locked(
		struct hid_proxy_runtime *runtime,
		struct hid_proxy_touch_handle *source,
		struct hid_proxy_touch_inflight *inflight)
{
	if (source->source_pending) {
		struct hid_proxy_touch_source_report report =
			hid_proxy_touch_source_report(source, true);

		inflight->kind = HID_PROXY_TOUCH_INFLIGHT_SOURCE_PRESENT;
		inflight->report_id = REPORT_ID_TOUCH_SOURCE;
		inflight->length = sizeof(report);
		inflight->source = source;
		memcpy(inflight->data, &report, sizeof(report));
		return true;
	}

	if (source->reset_flush_pending)
		return hid_proxy_prepare_touch_reset_flush_locked(runtime, source,
								  inflight);

	if (source->source_published && source->target_pending) {
		if (hid_proxy_touch_topology_reset_pending_locked(runtime,
								 source))
			return false;
		return hid_proxy_prepare_touch_contact_locked(runtime, source,
							      inflight);
	}

	if (source->retiring && source->source_published &&
	    !source->target_pending) {
		struct hid_proxy_touch_source_report report =
			hid_proxy_touch_source_report(source, false);

		inflight->kind = HID_PROXY_TOUCH_INFLIGHT_SOURCE_REMOVED;
		inflight->report_id = REPORT_ID_TOUCH_SOURCE;
		inflight->length = sizeof(report);
		inflight->source = source;
		memcpy(inflight->data, &report, sizeof(report));
		return true;
	}

	return false;
}

static bool hid_proxy_prepare_touch_report_locked(
		struct hid_proxy_runtime *runtime)
{
	struct hid_proxy_touch_handle *source;

	list_for_each_entry(source, &runtime->touch_sources, runtime_node) {
		if (source->target_started &&
		    hid_proxy_prepare_touch_source_locked(
			    runtime, source, &runtime->touch_inflight))
			return true;
	}

	list_for_each_entry(source, &runtime->touch_sources, runtime_node) {
		if (source->disconnecting)
			continue;
		if (!hid_proxy_prepare_touch_source_locked(
			    runtime, source, &runtime->touch_inflight))
			continue;

		list_del(&source->runtime_node);
		list_add_tail(&source->runtime_node, &runtime->touch_sources);
		return true;
	}

	return false;
}

static void hid_proxy_touch_report_complete_locked(
		struct hid_proxy_runtime *runtime, bool success)
{
	struct hid_proxy_touch_inflight *inflight = &runtime->touch_inflight;
	struct hid_proxy_touch_handle *source;

	if (inflight->kind == HID_PROXY_TOUCH_INFLIGHT_NONE)
		return;
	source = inflight->source;

	if (!success) {
		if (inflight->kind == HID_PROXY_TOUCH_INFLIGHT_SOURCE_PRESENT &&
		    source->retiring && !source->source_published) {
			source->source_pending = false;
			if (!source->disconnecting) {
				list_del(&source->runtime_node);
				list_add_tail(&source->runtime_node,
					      &runtime->touch_released);
			}
		}
		memset(inflight, 0, sizeof(*inflight));
		return;
	}

	if (inflight->kind == HID_PROXY_TOUCH_INFLIGHT_SOURCE_PRESENT) {
		memcpy(&runtime->last_touch_source, inflight->data,
		       sizeof(runtime->last_touch_source));
		source->source_published = true;
		source->source_pending = false;
	} else if (inflight->kind == HID_PROXY_TOUCH_INFLIGHT_SOURCE_REMOVED) {
		memcpy(&runtime->last_touch_source, inflight->data,
		       sizeof(runtime->last_touch_source));
		source->source_published = false;
		list_del(&source->runtime_node);
		list_add_tail(&source->runtime_node, &runtime->touch_released);
	} else {
		if (inflight->report_id == REPORT_ID_TOUCH_DIRECT)
			memcpy(&runtime->last_touch_direct, inflight->data,
			       sizeof(runtime->last_touch_direct));
		else
			memcpy(&runtime->last_touch_indirect, inflight->data,
			       sizeof(runtime->last_touch_indirect));

		if (inflight->phase == HID_PROXY_TOUCH_PHASE_RESET_FLUSH) {
			source->published_buttons = source->target_buttons;
			source->reset_flush_pending = false;
			source->resync_pending = false;
			source->drop_reported = false;
			hid_proxy_touch_prepare_target_locked(source);
			memset(inflight, 0, sizeof(*inflight));
			return;
		}

		source->phase_done++;
		if (inflight->phase != HID_PROXY_TOUCH_PHASE_DUMMY)
			source->scan_slot = inflight->slot + 1;
		if (source->phase_done == source->phase_count) {
			if (source->phase == HID_PROXY_TOUCH_PHASE_RELEASE &&
			    !source->combined_phase && source->active_count) {
				source->phase = HID_PROXY_TOUCH_PHASE_ACTIVE;
				source->phase_count = source->active_count;
				source->phase_done = 0;
				source->scan_slot = 0;
				source->frame_sequence =
					source->next_frame_sequence++;
			} else {
				hid_proxy_touch_complete_target_locked(source);
			}
		}
	}

	memset(inflight, 0, sizeof(*inflight));
}

static void hid_proxy_free_released(void)
{
	LIST_HEAD(released);
	LIST_HEAD(touch_released);
	struct hid_proxy_handle *proxy_handle;
	struct hid_proxy_touch_handle *touch_source;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	while (!list_empty(&hid_proxy->released)) {
		proxy_handle = list_first_entry(&hid_proxy->released,
						struct hid_proxy_handle,
						runtime_node);
		list_del(&proxy_handle->runtime_node);
		list_add_tail(&proxy_handle->runtime_node, &released);
	}
	while (!list_empty(&hid_proxy->touch_released)) {
		touch_source = list_first_entry(&hid_proxy->touch_released,
					      struct hid_proxy_touch_handle,
					      runtime_node);
		list_del(&touch_source->runtime_node);
		list_add_tail(&touch_source->runtime_node, &touch_released);
	}
	xSemaphoreGive(hid_proxy->mutex);

	while (!list_empty(&released)) {
		proxy_handle = list_first_entry(&released,
						struct hid_proxy_handle,
						runtime_node);
		list_del(&proxy_handle->runtime_node);
		kfree(proxy_handle);
	}
	while (!list_empty(&touch_released)) {
		touch_source = list_first_entry(&touch_released,
					      struct hid_proxy_touch_handle,
					      runtime_node);
		list_del(&touch_source->runtime_node);
		kfree(touch_source);
	}
}

void hid_proxy_tud_poll(void)
{
	struct hid_proxy_inflight report;
	struct hid_proxy_touch_inflight touch_report;
	bool prepared = false;
	bool touch_prepared = false;
	bool warn;

	if (!hid_proxy)
		return;

	/* TinyUSB callbacks only retire objects; task-side polling frees them. */
	hid_proxy_free_released();
	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	warn = hid_proxy->touch_drop_warning_pending;
	hid_proxy->touch_drop_warning_pending = false;
	xSemaphoreGive(hid_proxy->mutex);
	if (warn)
		async_msg("WARN: HID_PROXY_TOUCH_DROP");

	if (tud_hid_n_ready(HID_WEBHID_INSTANCE)) {
		xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
		if (hid_proxy->mounted &&
		    hid_proxy->inflight.kind == HID_PROXY_INFLIGHT_NONE) {
			prepared = hid_proxy_prepare_report_locked(hid_proxy);
			report = hid_proxy->inflight;
		}
		xSemaphoreGive(hid_proxy->mutex);

		if (prepared &&
		    !tud_hid_n_report(HID_WEBHID_INSTANCE, report.report_id,
				      report.data, report.length))
			hid_proxy_tud_report_complete(HID_WEBHID_INSTANCE, false);
	}

	if (tud_hid_n_ready(HID_TOUCH_INSTANCE)) {
		xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
		if (hid_proxy->mounted &&
		    hid_proxy->touch_inflight.kind ==
			    HID_PROXY_TOUCH_INFLIGHT_NONE) {
			touch_prepared =
				hid_proxy_prepare_touch_report_locked(hid_proxy);
			touch_report = hid_proxy->touch_inflight;
		}
		xSemaphoreGive(hid_proxy->mutex);

		if (touch_prepared &&
		    !tud_hid_n_report(HID_TOUCH_INSTANCE,
				      touch_report.report_id,
				      touch_report.data,
				      touch_report.length))
			hid_proxy_tud_report_complete(HID_TOUCH_INSTANCE, false);
	}

	hid_proxy_free_released();
}

void hid_proxy_tud_report_complete(uint8_t instance, bool success)
{
	struct hid_proxy_inflight *inflight;
	struct hid_proxy_source *source;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	if (instance == HID_TOUCH_INSTANCE) {
		hid_proxy_touch_report_complete_locked(hid_proxy, success);
		xSemaphoreGive(hid_proxy->mutex);
		return;
	}
	inflight = &hid_proxy->inflight;
	if (inflight->kind == HID_PROXY_INFLIGHT_NONE) {
		xSemaphoreGive(hid_proxy->mutex);
		return;
	}
	source = inflight->source;

	if (!success) {
		if (inflight->kind == HID_PROXY_INFLIGHT_SOURCE_PRESENT &&
		    source->retiring && !source->published) {
			struct hid_proxy_handle *proxy_handle =
				container_of(source, struct hid_proxy_handle, source);

			list_del(&proxy_handle->runtime_node);
			list_add_tail(&proxy_handle->runtime_node,
				      &hid_proxy->released);
		}
		memset(inflight, 0, sizeof(*inflight));
		xSemaphoreGive(hid_proxy->mutex);
		return;
	}

	if (inflight->kind == HID_PROXY_INFLIGHT_SOURCE_PRESENT) {
		memcpy(&hid_proxy->last_source, inflight->data,
		       sizeof(hid_proxy->last_source));
		source->published = true;
		if (!source->retiring)
			source->frames[source->frame_head].source_pending = false;
	} else if (inflight->kind == HID_PROXY_INFLIGHT_SOURCE_REMOVED) {
		struct hid_proxy_handle *proxy_handle =
			container_of(source, struct hid_proxy_handle, source);

		memcpy(&hid_proxy->last_source, inflight->data,
		       sizeof(hid_proxy->last_source));
		list_del(&proxy_handle->runtime_node);
		list_add_tail(&proxy_handle->runtime_node, &hid_proxy->released);
	} else {
		if (inflight->report_id == REPORT_ID_STYLUS_DIRECT)
			memcpy(&hid_proxy->last_direct, inflight->data,
			       sizeof(hid_proxy->last_direct));
		else
			memcpy(&hid_proxy->last_indirect, inflight->data,
			       sizeof(hid_proxy->last_indirect));
		if (inflight->kind == HID_PROXY_INFLIGHT_STYLUS_NEUTRAL) {
			source->terminal_pending = false;
		} else if (source->frame_count) {
			source->frame_head = (source->frame_head + 1) %
				HID_PROXY_FRAME_QUEUE_DEPTH;
			source->frame_count--;
		}
	}

	memset(inflight, 0, sizeof(*inflight));
	xSemaphoreGive(hid_proxy->mutex);
}

uint16_t hid_proxy_tud_get_report(uint8_t instance, uint8_t report_id,
				  uint8_t report_type, uint8_t *buffer,
				  uint16_t reqlen)
{
	const void *report;
	uint16_t length;
	u8 contact_max = HID_PROXY_TOUCH_MAX_CONTACTS;

	if (instance == HID_TOUCH_INSTANCE &&
	    report_type == HID_REPORT_TYPE_FEATURE &&
	    (report_id == REPORT_ID_TOUCH_INDIRECT_MAX ||
	     report_id == REPORT_ID_TOUCH_DIRECT_MAX)) {
		length = min((uint16_t)sizeof(contact_max), reqlen);
		memcpy(buffer, &contact_max, length);
		return length;
	}
	if (report_type != HID_REPORT_TYPE_INPUT)
		return 0;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	switch (report_id) {
	case REPORT_ID_STYLUS_DIRECT:
		report = &hid_proxy->last_direct;
		length = sizeof(hid_proxy->last_direct);
		break;
	case REPORT_ID_STYLUS_INDIRECT:
		report = &hid_proxy->last_indirect;
		length = sizeof(hid_proxy->last_indirect);
		break;
	case REPORT_ID_STYLUS_SOURCE:
		report = &hid_proxy->last_source;
		length = sizeof(hid_proxy->last_source);
		break;
	case REPORT_ID_TOUCH_DIRECT:
		report = &hid_proxy->last_touch_direct;
		length = sizeof(hid_proxy->last_touch_direct);
		break;
	case REPORT_ID_TOUCH_INDIRECT:
		report = &hid_proxy->last_touch_indirect;
		length = sizeof(hid_proxy->last_touch_indirect);
		break;
	case REPORT_ID_TOUCH_SOURCE:
		report = &hid_proxy->last_touch_source;
		length = sizeof(hid_proxy->last_touch_source);
		break;
	default:
		xSemaphoreGive(hid_proxy->mutex);
		return 0;
	}
	length = min(length, reqlen);
	memcpy(buffer, report, length);
	xSemaphoreGive(hid_proxy->mutex);

	return length;
}
