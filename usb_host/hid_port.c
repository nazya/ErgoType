#include <stdint.h>

#include "forward.h"
#include "hid_port.h"
#include "linux/include/linux/hid-input.h"
#include "linux/include/uapi/linux/input-event-codes.h"
#include "stdio_tusb_cdc.h"

// Upstream Linux: no equivalent. This is the always-open firmware consumer
// boundary after Linux hid-input mapping, replacing Linux userspace open.
static int hidinput_abs_to_mouse(struct hid_input *hidinput)
{
	struct input_dev *input = hidinput->input;

	if (test_bit(INPUT_PROP_POINTER, input->propbit) ||
	    test_bit(INPUT_PROP_DIRECT, input->propbit))
		return 1;

	switch (hidinput->application) {
	case HID_GD_MOUSE:
	case HID_GD_POINTER:
	case HID_DG_DIGITIZER:
	case HID_DG_PEN:
	case HID_DG_TOUCHSCREEN:
	case HID_DG_TOUCHPAD:
	case HID_DG_STYLUS:
	case HID_DG_PUCK:
	case HID_DG_FINGER:
		return 1;
	default:
		return 0;
	}
}

static uint8_t hidinput_device_caps(struct hid_device *hid)
{
	uint8_t caps = 0;
	struct hid_input *hidinput;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		struct input_dev *input = hidinput->input;

		for (unsigned int i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
			if (input->keybit[i])
				caps |= FORWARD_CAP_KEY;

		if (test_bit(REL_X, input->relbit) || test_bit(REL_Y, input->relbit) ||
		    test_bit(REL_WHEEL, input->relbit) || test_bit(REL_HWHEEL, input->relbit))
			caps |= FORWARD_CAP_MOUSE;

		if (test_bit(ABS_X, input->absbit) &&
		    test_bit(ABS_Y, input->absbit) &&
		    hidinput_abs_to_mouse(hidinput))
			caps |= FORWARD_CAP_MOUSE_ABS;

		switch (hidinput->application) {
		case HID_GD_KEYBOARD:
		case HID_GD_KEYPAD:
			caps |= FORWARD_CAP_KEY | FORWARD_CAP_KEYBOARD;
			break;
		case HID_GD_MOUSE:
		case HID_GD_POINTER:
			caps |= FORWARD_CAP_MOUSE;
			break;
		default:
			break;
		}
	}

	return caps;
}

static void hidinput_trace_device_caps(struct hid_device *hid, uint8_t caps)
{
	uint8_t rel_flags = 0;
	uint8_t abs_flags = 0;
	uint8_t has_key = 0;
	struct hid_input *hidinput;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		struct input_dev *input = hidinput->input;

		if (test_bit(REL_X, input->relbit))
			rel_flags |= BIT(0);
		if (test_bit(REL_Y, input->relbit))
			rel_flags |= BIT(1);
		if (test_bit(REL_WHEEL, input->relbit))
			rel_flags |= BIT(2);
		if (test_bit(REL_HWHEEL, input->relbit))
			rel_flags |= BIT(3);
		if (test_bit(ABS_X, input->absbit))
			abs_flags |= BIT(0);
		if (test_bit(ABS_Y, input->absbit))
			abs_flags |= BIT(1);
		for (unsigned int i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
			if (input->keybit[i])
				has_key = 1;
	}

	hid_host_trace_input_caps(hid, caps, rel_flags, abs_flags, has_key);
}

int hid_port_register_device(struct hid_device *hid)
{
	uint8_t caps;
	int ret;

	// async_err("HID port register");
	// TinyUSB mount/register path must not log synchronously from callback flow.

	/*
	 * This is the FreeRTOS/keyd device boundary. Linux hid-core stops after
	 * report parsing/input connection; queue allocation and device_add()
	 * belong to this port layer, not to the Linux mirror.
	 */
	/* HID++ delayed input can arrive after hid_connect() already registered this hdev. */
	if (hid->keyd_device) {
		// async_err("HID port reactivate");
		// TinyUSB mount/register path must not log synchronously from callback flow.
		forward_add_device_caps(hid->keyd_device, hidinput_device_caps(hid));
		return input_port_activate_hid(hid);
	}

	caps = hidinput_device_caps(hid);
	hidinput_trace_device_caps(hid, caps);
	// input_open_device(handle);
	// This port has no Linux userspace open; the always-on input handler is
	// activated after the keyd queue exists.
	hid->keyd_device = forward_register_device(hid->vendor, hid->product, caps);
	if (!hid->keyd_device) {
		// async_err("HID forward reg fail");
		// TinyUSB mount/register path must not log synchronously from callback flow.
		return -EAGAIN;
	}

	ret = input_port_activate_hid(hid);
	if (ret < 0) {
		// async_err("HID input act fail");
		// TinyUSB mount/register path must not log synchronously from callback flow.
		input_port_deactivate_hid(hid);
		forward_unregister_device(hid->keyd_device);
		hid->keyd_device = NULL;
		return ret;
	}

	// async_err("HID port register ok");
	// TinyUSB mount/register path must not log synchronously from callback flow.
	return 0;
}

void hid_port_unregister_device(struct hid_device *hid)
{
	// async_err("HID port unregister");
	// TinyUSB unmount path must not log synchronously from callback flow.

	/*
	 * Tell keyd about removal; evloop owns device_delete() after it
	 * converts DEV_REMOVED to EV_DEV_REMOVE.
	 */
	// input_close_device(handle);
	// The keyd-visible device is the consumer that opened input handles.
	input_port_deactivate_hid(hid);
	forward_unregister_device(hid->keyd_device);
	hid->keyd_device = NULL;
}
