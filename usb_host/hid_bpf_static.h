#ifndef USB_HOST_HID_BPF_STATIC_H
#define USB_HOST_HID_BPF_STATIC_H

#include <linux/hid.h>

/*
 * Upstream Linux loads these programs through HID-BPF. Firmware has no BPF
 * VM or userspace loader, so selected upstream programs register immutable
 * native callbacks at the same hid-core hook points.
 */

struct hid_bpf_ctx {
	struct hid_device *hid;
	u32 allocated_size;
	u32 size;
	u8 *data;
};

struct hid_bpf_probe_args {
	unsigned int hid;
	unsigned int rdesc_size;
	unsigned char rdesc[HID_MAX_DESCRIPTOR_SIZE];
	int retval;
	/* Firmware-only native-loader context; upstream BPF receives hid by ID. */
	struct hid_device *hdev;
};

struct hid_bpf_ops {
	int (*hid_rdesc_fixup)(struct hid_bpf_ctx *ctx);
	int (*hid_device_event)(struct hid_bpf_ctx *ctx);
};

enum hid_bpf_static_flags {
	HID_BPF_STATIC_HUION_SWITCHER = BIT(0),
	HID_BPF_STATIC_CONNECT_FALLBACK = BIT(1),
};

struct hid_bpf_static_program {
	const struct hid_device_id *devices;
	size_t device_count;
	const struct hid_bpf_ops *ops;
	int (*probe)(struct hid_bpf_probe_args *args);
	size_t private_size;
	unsigned int flags;
	int (*connect)(struct hid_bpf_ctx *ctx);
};

/*
 * Every selected native program includes its generated vmlinux.h first. That
 * header reaches this firmware glue before the program includes Linux's BPF
 * loader/helper headers. Keep those imported headers byte-for-byte upstream,
 * but suppress their unavailable VM/map ABI and provide the selected native
 * ABI here. A newly imported program that needs another helper must therefore
 * fail to compile instead of receiving a reduced silent implementation.
 */
#define ____HID_BPF__H
#define __HID_BPF_HELPERS_H
#define __HID_BPF_ASYNC_H__

#define HID_BPF_DEVICE_EVENT "struct_ops/hid_device_event"
#define HID_BPF_RDESC_FIXUP  "struct_ops/hid_rdesc_fixup"

#undef HID_DEVICE
#define HID_DEVICE(b, g, ven, prod) \
	{ .bus = (b), .group = (g), .vendor = (ven), .product = (prod) }
#define HID_BPF_CONFIG(...) \
	static const struct hid_device_id hid_bpf_devices[] = { __VA_ARGS__ }
#define HID_BPF_OPS(name) static const struct hid_bpf_ops name
#define hid_set_name(_hdev, _name) \
	__builtin_memcpy((_hdev)->name, (_name), sizeof(_name))

#define HID_IGNORE_EVENT (-1)

#define HID_BPF_ASYNC_FUN(fun) fun
#define HID_BPF_ASYNC_INIT(fun) 0
#define HID_BPF_ASYNC_DELAYED_CALL(fun, ctx, delay) \
	hid_bpf_async_delayed_call((ctx), (delay), (fun))

#define HID_BPF_STATIC_PROGRAM(name, probe_fn, private_bytes, program_flags, connect_fn) \
	static const struct hid_bpf_static_program name##_static_program \
	__attribute__((used, section("hid_bpf_programs"))) = { \
		.devices = hid_bpf_devices, \
		.device_count = ARRAY_SIZE(hid_bpf_devices), \
		.ops = &(name), \
		.probe = (probe_fn), \
		.private_size = (private_bytes), \
		.flags = (program_flags), \
		.connect = (connect_fn), \
	}

u8 *hid_bpf_get_data(struct hid_bpf_ctx *ctx, unsigned int offset,
		     size_t size);
void *hid_bpf_get_private(struct hid_bpf_ctx *ctx);
const char *hid_bpf_get_firmware_id(struct hid_bpf_ctx *ctx);
int hid_bpf_hw_output_report(struct hid_bpf_ctx *ctx, u8 *buf, size_t size);
int hid_bpf_async_delayed_call(struct hid_bpf_ctx *ctx,
			       unsigned long delay_ms,
			       int (*callback)(struct hid_bpf_ctx *ctx));

#endif
