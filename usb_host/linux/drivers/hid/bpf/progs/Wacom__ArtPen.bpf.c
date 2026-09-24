// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Benjamin Tissoires
 */

#include "vmlinux.h"
#include "hid_bpf.h"
#include "hid_bpf_helpers.h"
#include <bpf/bpf_tracing.h>
/* Native Cortex-M0+ accesses report fields through unaligned helpers. */
#include <linux/unaligned.h>

#define VID_WACOM		0x056a
#define ART_PEN_ID		0x0804
#define PID_INTUOS_PRO_2_M	0x0357

HID_BPF_CONFIG(
	// HID_DEVICE(BUS_USB, HID_GROUP_GENERIC, VID_WACOM, PID_INTUOS_PRO_2_M)
	// Native attach follows hid_set_group(): Linux classifies Wacom VID as
	// HID_GROUP_WACOM, so the upstream GENERIC metadata cannot match here.
	HID_DEVICE(BUS_USB, HID_GROUP_WACOM, VID_WACOM, PID_INTUOS_PRO_2_M)
);

/*
 * This filter is here for the Art Pen stylus only:
 * - when used on some Wacom devices (see the list of attached PIDs), this pen
 *   reports pressure every other events.
 * - to solve that, given that we know that the next event will be the same as
 *   the current one, we can emulate a smoother pressure reporting by reporting
 *   the mean of the previous value and the current one.
 *
 * We are effectively delaying the pressure by one event every other event, but
 * that's less of an annoyance compared to the chunkiness of the reported data.
 *
 * For example, let's assume the following set of events:
 * <Tip switch 0> <X 0> <Y 0> <Pressure    0 > <Tooltype 0x0804>
 * <Tip switch 1> <X 1> <Y 1> <Pressure  100 > <Tooltype 0x0804>
 * <Tip switch 1> <X 2> <Y 2> <Pressure  100 > <Tooltype 0x0804>
 * <Tip switch 1> <X 3> <Y 3> <Pressure  200 > <Tooltype 0x0804>
 * <Tip switch 1> <X 4> <Y 4> <Pressure  200 > <Tooltype 0x0804>
 * <Tip switch 0> <X 5> <Y 5> <Pressure    0 > <Tooltype 0x0804>
 *
 * The filter will report:
 * <Tip switch 0> <X 0> <Y 0> <Pressure    0 > <Tooltype 0x0804>
 * <Tip switch 1> <X 1> <Y 1> <Pressure * 50*> <Tooltype 0x0804>
 * <Tip switch 1> <X 2> <Y 2> <Pressure  100 > <Tooltype 0x0804>
 * <Tip switch 1> <X 3> <Y 3> <Pressure *150*> <Tooltype 0x0804>
 * <Tip switch 1> <X 4> <Y 4> <Pressure  200 > <Tooltype 0x0804>
 * <Tip switch 0> <X 5> <Y 5> <Pressure    0 > <Tooltype 0x0804>
 *
 */

struct wacom_params {
	__u16 pid;
	__u16 rdesc_len;
	__u8 report_id;
	__u8 report_len;
	struct {
		__u8 tip_switch;
		__u8 pressure;
		__u8 tool_type;
	} offsets;
};

/*
 * Multiple device can support the same stylus, so
 * we need to know which device has which offsets
 */
static const struct wacom_params devices[] = {
	{
		.pid = PID_INTUOS_PRO_2_M,
		.rdesc_len = 949,
		.report_id = 16,
		.report_len = 27,
		.offsets = {
			.tip_switch = 1,
			.pressure = 8,
			.tool_type = 25,
		},
	},
};

// static struct wacom_params params = { 0 };
// Linux BPF globals belong to each attachment; the native loader owns these
// fields in the same hid_device-private allocation before probe runs.
struct wacom_artpen_state {
	struct wacom_params params;
	bool odd;
	__u16 prev_pressure;
};

/* HID-BPF reports a 64 bytes chunk anyway, so this ensures
 * the verifier to know we are addressing the memory correctly
 */
#define PEN_REPORT_LEN		64

/* only odd frames are modified */
// static bool odd;
// Stored in wacom_artpen_state for this native attachment.

// static __u16 prev_pressure;
// Stored in wacom_artpen_state for this native attachment.

static inline void *get_bits(__u8 *data, unsigned int byte_offset)
{
	return data + byte_offset;
}

static inline __u16 *get_u16(__u8 *data, unsigned int offset)
{
	return (__u16 *)get_bits(data, offset);
}

static inline __u8 *get_u8(__u8 *data, unsigned int offset)
{
	return (__u8 *)get_bits(data, offset);
}

SEC(HID_BPF_DEVICE_EVENT)
int BPF_PROG(artpen_pressure_interpolate, struct hid_bpf_ctx *hctx)
{
	__u8 *data = hid_bpf_get_data(hctx, 0 /* offset */, PEN_REPORT_LEN /* size */);
	/* Native equivalent of this attachment's BPF data map. */
	struct wacom_artpen_state *state = hid_bpf_get_private(hctx);
	__u16 *pressure, *tool_type;
	__u8 *tip_switch;

	if (!data)
		return 0; /* EPERM check */

	// if (data[0] != params.report_id ||
	//     params.offsets.tip_switch >= PEN_REPORT_LEN ||
	//     params.offsets.pressure >= PEN_REPORT_LEN - 1 ||
	//     params.offsets.tool_type >= PEN_REPORT_LEN - 1)
	// Native attachment-local parameters replace the BPF globals.
	if (data[0] != state->params.report_id ||
	    state->params.offsets.tip_switch >= PEN_REPORT_LEN ||
	    state->params.offsets.pressure >= PEN_REPORT_LEN - 1 ||
	    state->params.offsets.tool_type >= PEN_REPORT_LEN - 1)
		return 0; /* invalid report or parameters */

	// tool_type = get_u16(data, params.offsets.tool_type);
	tool_type = get_u16(data, state->params.offsets.tool_type);
	// if (*tool_type != ART_PEN_ID)
	// Tool type is at byte 25; Cortex-M0+ cannot dereference that u16.
	if (get_unaligned_le16((const u8 *)tool_type) != ART_PEN_ID)
		return 0;

	// tip_switch = get_u8(data, params.offsets.tip_switch);
	tip_switch = get_u8(data, state->params.offsets.tip_switch);
	if ((*tip_switch & 0x01) == 0) {
		// prev_pressure = 0;
		// odd = true;
		// Reset only this attachment's pressure history.
		state->prev_pressure = 0;
		state->odd = true;
		return 0;
	}

	// pressure = get_u16(data, params.offsets.pressure);
	pressure = get_u16(data, state->params.offsets.pressure);

	// if (odd)
	// 	*pressure = (*pressure + prev_pressure) / 2;
	// Preserve little-endian wire values without native unaligned accesses.
	if (state->odd)
		put_unaligned(cpu_to_le16((get_unaligned_le16((const u8 *)pressure) +
					  state->prev_pressure) / 2), pressure);

	// prev_pressure = *pressure;
	// odd = !odd;
	state->prev_pressure = get_unaligned_le16((const u8 *)pressure);
	state->odd = !state->odd;

	return 0;
}

HID_BPF_OPS(wacom_artpen) = {
	.hid_device_event = (void *)artpen_pressure_interpolate,
};

SEC("syscall")
int probe(struct hid_bpf_probe_args *ctx)
{
	struct hid_bpf_ctx *hid_ctx;
	__u16 pid;
	size_t i;

	/* get a struct hid_device to access the actual pid of the device */
	// hid_ctx = hid_bpf_allocate_context(ctx->hid);
	// if (!hid_ctx) {
	// 	ctx->retval = -ENODEV;
	// 	return -1; /* EPERM check */
	// }
	// Native probe already owns the candidate HID and its private allocation.
	struct hid_bpf_ctx native_ctx = { .hid = ctx->hdev };
	hid_ctx = &native_ctx;
	struct wacom_artpen_state *state = hid_bpf_get_private(hid_ctx);
	pid = hid_ctx->hid->product;

	ctx->retval = -EINVAL;

	/* Match the given device with the list of known devices */
	for (i = 0; i < ARRAY_SIZE(devices); i++) {
		const struct wacom_params *device = &devices[i];

		if (device->pid == pid && device->rdesc_len == ctx->rdesc_size) {
			// params = *device;
			// Store parameters in this attachment's native BPF data map.
			state->params = *device;
			ctx->retval = 0;
		}
	}

	// hid_bpf_release_context(hid_ctx);
	// native_ctx is local; the loader retains ownership of the candidate HID.
	return 0;
}

// char _license[] SEC("license") = "GPL";
// Native registration replaces the Linux BPF loader and its ELF metadata.
static const char _license[] __attribute__((unused)) = "GPL";

/* Firmware-only registration at the existing HID-BPF hook points. */
HID_BPF_STATIC_PROGRAM(wacom_artpen, probe, sizeof(struct wacom_artpen_state),
		       0, NULL);
