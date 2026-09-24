// SPDX-License-Identifier: GPL-2.0-only

/*
 * Firmware adapter for selected upstream HID-BPF programs.
 *
 * Linux owns a BPF loader, program maps, and the HID-BPF dispatcher. The
 * firmware links the selected, immutable upstream programs natively and keeps
 * their descriptor/event hooks at the same hid-core call sites. Every mutable
 * object below belongs to one hid_device attachment.
 */

#include "hid_bpf_static.h"

#include <linux/usb.h>

#include "stdio_tusb_cdc.h"

struct hid_bpf_static_device {
	struct hid_device *hdev;
	const struct hid_bpf_static_program *program;
	u8 *event_data;
	u32 allocated_data;
	char *firmware_id;
	bool connected;
	bool huion_mode_confirmed;
	struct delayed_work async_work;
	int (*async_callback)(struct hid_bpf_ctx *ctx);
	u8 private_data[];
};

extern const struct hid_bpf_static_program __start_hid_bpf_programs[];
extern const struct hid_bpf_static_program __stop_hid_bpf_programs[];

/* Keep the optional-program linker section defined when none are enabled. */
static const struct hid_bpf_static_program hid_bpf_static_sentinel
__attribute__((used, section("hid_bpf_programs")));

#define HUION_STRING_TIMEOUT_MS 100
#define HUION_FIRMWARE_ID_SIZE 64

static bool hid_bpf_static_id_match(const struct hid_device *hdev,
				    const struct hid_device_id *id)
{
	if (id->bus != HID_BUS_ANY && id->bus != hdev->bus)
		return false;
	if (id->group != HID_GROUP_ANY && id->group != hdev->group)
		return false;
	if (id->vendor != HID_ANY_ID && id->vendor != hdev->vendor)
		return false;
	if (id->product != HID_ANY_ID && id->product != hdev->product)
		return false;

	return true;
}

static bool hid_bpf_static_program_match(
		const struct hid_device *hdev,
		const struct hid_bpf_static_program *program)
{
	for (size_t i = 0; i < program->device_count; i++)
		if (hid_bpf_static_id_match(hdev, &program->devices[i]))
			return true;

	return false;
}

static int huion_get_string_descriptor(struct usb_device *dev, u8 index,
				       u16 langid, u8 *buf, u16 length)
{
	memset(buf, 0, length);
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       USB_REQ_GET_DESCRIPTOR,
			       USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
			       (USB_DT_STRING << 8) | index, langid, buf, length,
			       HUION_STRING_TIMEOUT_MS);
}

static int huion_validate_utf16_descriptor(const u8 *buf, int length)
{
	if (length < 2 || buf[0] != length || (length & 1))
		return -EINVAL;

	return 0;
}

static int huion_decode_utf16(const u8 *buf, int length, char *out,
			      size_t out_size, bool trim_trailing_nul)
{
	size_t written = 0;
	bool output_full = false;

	for (int i = 2; i < length; i += 2) {
		u32 codepoint = (u32)buf[i] | ((u32)buf[i + 1] << 8);

		if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
			u32 low;

			if (i + 3 >= length)
				return -EINVAL;
			low = (u32)buf[i + 2] | ((u32)buf[i + 3] << 8);
			if (low < 0xdc00 || low > 0xdfff)
				return -EINVAL;
			codepoint = 0x10000 + ((codepoint - 0xd800) << 10) +
				    (low - 0xdc00);
			i += 2;
		} else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
			return -EINVAL;
		}

		if (out && !output_full) {
			size_t encoded_size = codepoint < 0x80 ? 1 :
					      codepoint < 0x800 ? 2 :
					      codepoint < 0x10000 ? 3 : 4;

			if (written + encoded_size >= out_size) {
				output_full = true;
				continue;
			}
			if (encoded_size == 1) {
				out[written++] = (char)codepoint;
			} else if (encoded_size == 2) {
				out[written++] = (char)(0xc0 | (codepoint >> 6));
				out[written++] = (char)(0x80 | (codepoint & 0x3f));
			} else if (encoded_size == 3) {
				out[written++] = (char)(0xe0 | (codepoint >> 12));
				out[written++] = (char)(0x80 |
						      ((codepoint >> 6) & 0x3f));
				out[written++] = (char)(0x80 | (codepoint & 0x3f));
			} else {
				out[written++] = (char)(0xf0 | (codepoint >> 18));
				out[written++] = (char)(0x80 |
						      ((codepoint >> 12) & 0x3f));
				out[written++] = (char)(0x80 |
						      ((codepoint >> 6) & 0x3f));
				out[written++] = (char)(0x80 | (codepoint & 0x3f));
			}
		}
	}

	if (out) {
		if (trim_trailing_nul)
			while (written && !out[written - 1])
				written--;
		out[written] = '\0';
	}

	return 0;
}

/*
 * Firmware equivalent of huion-switcher 7f63cd48aed5362b073b3c876d98228b3e3a6e90.
 * It uses only standard device-recipient GET_DESCRIPTOR requests. The decoded
 * firmware property is intentionally retained once string 201 succeeds, even
 * if a later mode request fails, matching the userspace program's output.
 */
static int hid_bpf_huion_switch_mode(struct hid_bpf_static_device *device)
{
	struct usb_device *udev = hid_to_usb_dev(device->hdev);
	u8 *buf = kmalloc(256, GFP_KERNEL);
	char *firmware_id;
	bool have_english = false;
	int ret;

	if (!buf)
		return -ENOMEM;

	ret = huion_get_string_descriptor(udev, 0, 0, buf, 255);
	/* Userspace read_languages().unwrap() terminates its process here; the
	 * firmware lifecycle reports the same descriptor failure to its caller. */
	if (ret < 0)
		goto out;
	ret = huion_validate_utf16_descriptor(buf, ret);
	if (ret)
		goto out;
	for (int i = 2; i + 1 < buf[0]; i += 2) {
		u16 langid = (u16)buf[i] | ((u16)buf[i + 1] << 8);

		if (langid == 0x0409) {
			have_english = true;
			break;
		}
	}
	if (!have_english) {
		ret = 0;
		goto out;
	}

	ret = huion_get_string_descriptor(udev, 201, 0x0409, buf, 255);
	if (ret < 0)
		goto out;
	ret = huion_validate_utf16_descriptor(buf, ret);
	if (ret)
		goto out;
	firmware_id = kzalloc(HUION_FIRMWARE_ID_SIZE, GFP_KERNEL);
	if (!firmware_id) {
		ret = -ENOMEM;
		goto out;
	}
	ret = huion_decode_utf16(buf, buf[0], firmware_id,
				 HUION_FIRMWARE_ID_SIZE, true);
	if (ret) {
		kfree(firmware_id);
		goto out;
	}
	device->firmware_id = firmware_id;

	ret = huion_get_string_descriptor(udev, 200, 0x0409, buf, 256);
	if (ret >= 0 && buf[0] != (u8)ret) {
		ret = -EINVAL;
		goto out;
	}
	if (ret >= 18) {
		device->huion_mode_confirmed = true;
		ret = 0;
		goto out;
	}
	if (ret < 0 && ret != -EPIPE)
		goto out;

	ret = huion_get_string_descriptor(udev, 100, 0x0409, buf, 256);
	if (ret >= 0 && buf[0] != (u8)ret) {
		ret = -EINVAL;
		goto out;
	}
	if (ret == -EPIPE || (ret >= 0 && ret < 12)) {
		ret = 0;
		goto out;
	}
	if (ret < 0)
		goto out;

	ret = huion_get_string_descriptor(udev, 123, 0x0409, buf, 255);
	if (ret < 0)
		goto out;
	ret = huion_validate_utf16_descriptor(buf, ret);
	if (ret)
		goto out;
	ret = huion_decode_utf16(buf, buf[0], NULL, 0, false);
	if (!ret)
		device->huion_mode_confirmed = true;

out:
	kfree(buf);
	return ret;
}

static void hid_bpf_static_async_work(struct work_struct *work)
{
	struct hid_bpf_static_device *device = container_of(
		to_delayed_work(work), struct hid_bpf_static_device, async_work);
	struct hid_bpf_ctx ctx = {
		.hid = device->hdev,
		.allocated_size = device->allocated_data,
		.size = device->allocated_data,
		.data = device->event_data,
	};

	if (device->connected && device->async_callback)
		device->async_callback(&ctx);
}

static void hid_bpf_static_free(struct hid_device *hdev)
{
	struct hid_bpf_static_device *device = hdev->bpf_static;

	if (!device)
		return;

	device->connected = false;
	cancel_delayed_work_sync(&device->async_work);
	kfree(device->event_data);
	kfree(device->firmware_id);
	kfree(device);
	hdev->bpf_static = NULL;
}

static int hid_bpf_static_attach(struct hid_device *hdev, const u8 *rdesc,
				 unsigned int size)
{
	struct hid_bpf_probe_args *args = NULL;

	if (hdev->bpf_static)
		return 0;

	for (const struct hid_bpf_static_program *program =
			__start_hid_bpf_programs;
	     program < __stop_hid_bpf_programs; program++) {
		struct hid_bpf_static_device *device;
		int ret;

		if (!hid_bpf_static_program_match(hdev, program))
			continue;
		if (!args) {
			args = kzalloc_obj(*args);
			if (!args)
				return -ENOMEM;
			args->hid = hdev->id;
			args->rdesc_size = size;
			memcpy(args->rdesc, rdesc, size);
			args->hdev = hdev;
		}

		/* BPF data maps already exist when Linux runs probe(). Native
		 * per-attachment globals must likewise be available to that hook. */
		device = kzalloc(sizeof(*device) + program->private_size,
				 GFP_KERNEL);
		if (!device) {
			kfree(args);
			return -ENOMEM;
		}
		device->hdev = hdev;
		device->program = program;
		INIT_DELAYED_WORK(&device->async_work,
				  hid_bpf_static_async_work);
		hdev->bpf_static = device;

		args->retval = 0;
		ret = program->probe(args);
		if (ret || args->retval) {
			hid_bpf_static_free(hdev);
			continue;
		}
		kfree(args);
		args = NULL;

		if (program->flags & HID_BPF_STATIC_HUION_SWITCHER) {
			ret = hid_bpf_huion_switch_mode(device);
			if (ret)
				async_msg("WARN: HUION_MODE_INIT");
			else if (!device->huion_mode_confirmed)
				async_msg("WARN: HUION_MODE_UNCONFIRMED");
		}

		return 0;
	}

	kfree(args);
	return 0;
}

u8 *hid_bpf_get_data(struct hid_bpf_ctx *ctx, unsigned int offset,
		     size_t size)
{
	if (size + offset > ctx->allocated_size)
		return NULL;

	return ctx->data + offset;
}

void *hid_bpf_get_private(struct hid_bpf_ctx *ctx)
{
	return ctx->hid->bpf_static->private_data;
}

const char *hid_bpf_get_firmware_id(struct hid_bpf_ctx *ctx)
{
	static const char empty_firmware_id[HUION_FIRMWARE_ID_SIZE];
	const char *firmware_id = ctx->hid->bpf_static->firmware_id;

	return firmware_id ? firmware_id : empty_firmware_id;
}

int hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx, u8 *buf, size_t size)
{
	struct hid_report_enum *report_enum =
		&ctx->hid->report_enum[HID_OUTPUT_REPORT];
	unsigned int report_id = report_enum->numbered ? buf[0] : 0;
	struct hid_report *report;
	u8 *dma_data;
	int ret;

	if (size < 1)
		return -EINVAL;
	report = hid_report_enum_lookup(report_enum, report_id);
	if (!report)
		return -EINVAL;
	size = min_t(size_t, size, hid_report_len(report));

	dma_data = kmemdup(buf, size, GFP_KERNEL);
	if (!dma_data)
		return -ENOMEM;
	ret = __hid_hw_output_report(ctx->hid, dma_data, size,
				     (u64)(uintptr_t)ctx, true);
	kfree(dma_data);
	return ret;
}

int hid_bpf_async_delayed_call(struct hid_bpf_ctx *ctx,
			       unsigned long delay_ms,
			       int (*callback)(struct hid_bpf_ctx *ctx))
{
	struct hid_bpf_static_device *device = ctx->hid->bpf_static;

	device->async_callback = callback;
	schedule_delayed_work(&device->async_work,
			      msecs_to_jiffies(delay_ms));
	return 0;
}

u8 *dispatch_hid_bpf_device_event(struct hid_device *hdev,
		enum hid_report_type type, u8 *data, size_t *buf_size, u32 *size,
		int interrupt, u64 source, bool from_bpf)
{
	struct hid_bpf_static_device *device = hdev->bpf_static;
	struct hid_bpf_ctx ctx;
	int ret;

	(void)interrupt;
	(void)source;
	(void)from_bpf;

	if (type >= HID_REPORT_TYPES)
		return ERR_PTR(-EINVAL);
	if (!device || !device->program->ops->hid_device_event ||
	    !device->event_data)
		return data;

	memset(device->event_data, 0, device->allocated_data);
	memcpy(device->event_data, data, *size);
	ctx.hid = hdev;
	ctx.allocated_size = device->allocated_data;
	ctx.size = *size;
	ctx.data = device->event_data;

	ret = device->program->ops->hid_device_event(&ctx);
	if (ret < 0)
		return ERR_PTR(ret);
	if (ret)
		ctx.size = ret;
	if (ctx.size > ctx.allocated_size)
		return ERR_PTR(-EINVAL);
	if (ctx.size)
		*size = ctx.size;
	*buf_size = ctx.allocated_size;
	return ctx.data;
}

const u8 *call_hid_bpf_rdesc_fixup(struct hid_device *hdev, const u8 *rdesc,
				   unsigned int *size)
{
	struct hid_bpf_static_device *device;
	struct hid_bpf_ctx ctx;
	unsigned int result_size = *size;
	u8 *data;
	u8 *result;
	int ret;

	ret = hid_bpf_static_attach(hdev, rdesc, *size);
	if (ret)
		return rdesc;
	device = hdev->bpf_static;
	if (!device || !device->program->ops->hid_rdesc_fixup)
		return rdesc;

	data = kzalloc(HID_MAX_DESCRIPTOR_SIZE, GFP_KERNEL);
	if (!data)
		goto ignore_program;
	memcpy(data, rdesc, min_t(unsigned int, *size,
				  HID_MAX_DESCRIPTOR_SIZE));
	ctx.hid = hdev;
	ctx.allocated_size = HID_MAX_DESCRIPTOR_SIZE;
	ctx.size = *size;
	ctx.data = data;

	ret = device->program->ops->hid_rdesc_fixup(&ctx);
	if (ret < 0 || ret > ctx.allocated_size)
		goto ignore_data;
	if (ret)
		result_size = ret;

	// return krealloc(ctx_kern.data, *size, GFP_KERNEL);
	// FreeRTOS has no size-aware krealloc(); copy the final descriptor out of
	// the transient 4096-byte upstream workspace.
	result = kmemdup(data, result_size, GFP_KERNEL);
	kfree(data);
	if (result) {
		*size = result_size;
		return result;
	}
	goto ignore_program;

ignore_data:
	kfree(data);
ignore_program:
	hid_bpf_static_free(hdev);
	return rdesc;
}

static int hid_bpf_allocate_event_data(struct hid_device *hdev)
{
	struct hid_bpf_static_device *device = hdev->bpf_static;
	unsigned int max_report_len = 0;

	if (!device || !device->program->ops->hid_device_event ||
	    device->event_data)
		return 0;

	for (unsigned int i = 0; i < HID_REPORT_TYPES; i++) {
		struct hid_report_enum *report_enum = &hdev->report_enum[i];
		struct hid_report *report;

		list_for_each_entry(report, &report_enum->report_list, list)
			max_report_len = max(max_report_len,
					     hid_report_len(report));
	}

	device->allocated_data = DIV_ROUND_UP(max_report_len, 64) * 64;
	device->event_data = kzalloc(device->allocated_data, GFP_KERNEL);
	if (!device->event_data)
		return -ENOMEM;

	return 0;
}

static int hid_bpf_connect_fallback(struct hid_device *hdev)
{
	const u8 *bpf_rdesc = hdev->bpf_rdesc;
	int ret;

	hid_close_report(hdev);
	if (bpf_rdesc != hdev->dev_rdesc)
		kfree(bpf_rdesc);
	hdev->bpf_rdesc = hdev->dev_rdesc;
	hdev->bpf_rsize = hdev->dev_rsize;
	hid_bpf_static_free(hdev);

	ret = hid_open_report(hdev);
	return ret;
}

int hid_bpf_connect_device(struct hid_device *hdev)
{
	struct hid_bpf_static_device *device = hdev->bpf_static;
	struct hid_bpf_ctx ctx;
	int ret;

	if (!device)
		return 0;

	ret = hid_bpf_allocate_event_data(hdev);
	if (ret)
		return ret;
	device->connected = true;
	if (!device->program->connect)
		return 0;

	ctx.hid = hdev;
	ctx.allocated_size = device->allocated_data;
	ctx.size = device->allocated_data;
	ctx.data = device->event_data;
	ret = device->program->connect(&ctx);
	if (!ret)
		return 0;

	device->connected = false;
	if (device->program->flags & HID_BPF_STATIC_CONNECT_FALLBACK)
		return hid_bpf_connect_fallback(hdev);

	return ret;
}

void hid_bpf_disconnect_device(struct hid_device *hdev)
{
	struct hid_bpf_static_device *device = hdev->bpf_static;

	if (!device)
		return;

	device->connected = false;
	cancel_delayed_work_sync(&device->async_work);
	kfree(device->event_data);
	device->event_data = NULL;
	device->allocated_data = 0;
}

void hid_bpf_destroy_device(struct hid_device *hdev)
{
	hid_bpf_static_free(hdev);
}

int hid_bpf_device_init(struct hid_device *hdev)
{
	hdev->bpf_static = NULL;
	return 0;
}
