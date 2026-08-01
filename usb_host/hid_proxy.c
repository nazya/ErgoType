/* Firmware-only Linux input to TinyUSB device proxy boundary. */
#include "hid_proxy.h"

#include <limits.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "tusb.h"

#include "linux/include/linux/hid.h"
#include "linux/include/linux/input.h"
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

static_assert(sizeof(struct hid_proxy_stylus_report) == 22);
static_assert(sizeof(struct hid_proxy_source_report) == 17);

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

struct hid_proxy_inflight {
	u8 kind;
	u8 report_id;
	u8 length;
	struct hid_proxy_source *source;
	u8 data[sizeof(struct hid_proxy_stylus_report)];
};

struct hid_proxy_runtime {
	SemaphoreHandle_t mutex;
	struct input_handler handler;
	struct list_head sources;
	struct list_head released;
	struct hid_proxy_inflight inflight;
	struct hid_proxy_stylus_report last_direct;
	struct hid_proxy_stylus_report last_indirect;
	struct hid_proxy_source_report last_source;
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
	runtime->handler.private = runtime;
	runtime->handler.events = hid_proxy_events;
	runtime->handler.match = hid_proxy_match;
	runtime->handler.connect = hid_proxy_connect;
	runtime->handler.disconnect = hid_proxy_disconnect;
	runtime->handler.name = "hid-proxy-stylus";
	runtime->handler.id_table = hid_proxy_ids;
	runtime->last_source.version = 1;
	hid_proxy = runtime;

	return 0;
}

void hid_proxy_input_init(void)
{
	if (!hid_proxy)
		return;

	(void)input_register_handler(&hid_proxy->handler);
}

static void hid_proxy_reset_reports_locked(struct hid_proxy_runtime *runtime)
{
	memset(&runtime->inflight, 0, sizeof(runtime->inflight));
	memset(&runtime->last_direct, 0, sizeof(runtime->last_direct));
	memset(&runtime->last_indirect, 0, sizeof(runtime->last_indirect));
	memset(&runtime->last_source, 0, sizeof(runtime->last_source));
	runtime->last_source.version = 1;
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
}

void hid_proxy_tud_mount(void)
{
	struct hid_proxy_handle *proxy_handle;
	struct hid_proxy_handle *next;

	if (!hid_proxy)
		return;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
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
	xSemaphoreGive(hid_proxy->mutex);
}

void hid_proxy_tud_umount(void)
{
	struct hid_proxy_handle *proxy_handle;
	struct hid_proxy_handle *next;

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

static void hid_proxy_free_released(void)
{
	LIST_HEAD(released);
	struct hid_proxy_handle *proxy_handle;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	while (!list_empty(&hid_proxy->released)) {
		proxy_handle = list_first_entry(&hid_proxy->released,
						struct hid_proxy_handle,
						runtime_node);
		list_del(&proxy_handle->runtime_node);
		list_add_tail(&proxy_handle->runtime_node, &released);
	}
	xSemaphoreGive(hid_proxy->mutex);

	while (!list_empty(&released)) {
		proxy_handle = list_first_entry(&released,
						struct hid_proxy_handle,
						runtime_node);
		list_del(&proxy_handle->runtime_node);
		kfree(proxy_handle);
	}
}

void hid_proxy_tud_poll(void)
{
	struct hid_proxy_inflight report;
	bool prepared;

	if (!hid_proxy)
		return;

	/* TinyUSB callbacks only retire objects; task-side polling frees them. */
	hid_proxy_free_released();
	if (!tud_hid_n_ready(HID_WEBHID_INSTANCE))
		return;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
	if (!hid_proxy->mounted ||
	    hid_proxy->inflight.kind != HID_PROXY_INFLIGHT_NONE) {
		xSemaphoreGive(hid_proxy->mutex);
		return;
	}
	prepared = hid_proxy_prepare_report_locked(hid_proxy);
	report = hid_proxy->inflight;
	xSemaphoreGive(hid_proxy->mutex);

	if (!prepared)
		return;

	if (tud_hid_n_report(HID_WEBHID_INSTANCE, report.report_id,
			     report.data, report.length))
		return;

	hid_proxy_tud_report_complete(false);
	hid_proxy_free_released();
}

void hid_proxy_tud_report_complete(bool success)
{
	struct hid_proxy_inflight *inflight;
	struct hid_proxy_source *source;

	xSemaphoreTake(hid_proxy->mutex, portMAX_DELAY);
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

uint16_t hid_proxy_tud_get_report(uint8_t report_id, uint8_t *buffer,
				  uint16_t reqlen)
{
	const void *report;
	uint16_t length;

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
	default:
		xSemaphoreGive(hid_proxy->mutex);
		return 0;
	}
	length = min(length, reqlen);
	memcpy(buffer, report, length);
	xSemaphoreGive(hid_proxy->mutex);

	return length;
}
