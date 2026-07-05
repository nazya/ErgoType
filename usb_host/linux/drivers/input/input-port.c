// SPDX-License-Identifier: GPL-2.0-only
#include "../../include/linux/hid.h"
#include "../../../forward.h"
#include "../../../hid_port.h"

#include "uapi/linux/input-event-codes.h"

#define INPUT_PORT_ABS_MAX 1023

static struct input_handler input_port_handler;

static int input_port_abs_to_mouse(struct hid_device *hid, struct input_dev *dev)
{
	struct hid_input *hidinput;

	if (test_bit(INPUT_PROP_POINTER, dev->propbit) ||
	    test_bit(INPUT_PROP_DIRECT, dev->propbit))
		return 1;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		if (hidinput->input != dev)
			continue;

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

	return 0;
}

static uint8_t input_port_caps(struct hid_device *hid, struct input_dev *dev)
{
	uint8_t caps = 0;
	struct hid_input *hidinput;
	struct hid_report *report;
	int matched_hidinput = 0;

	for (unsigned int i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
		if (dev->keybit[i])
			caps |= FORWARD_CAP_KEY;

	if (test_bit(REL_X, dev->relbit) || test_bit(REL_Y, dev->relbit) ||
	    test_bit(REL_WHEEL, dev->relbit) || test_bit(REL_HWHEEL, dev->relbit))
		caps |= FORWARD_CAP_MOUSE;

	if (test_bit(ABS_X, dev->absbit) &&
	    test_bit(ABS_Y, dev->absbit) &&
	    input_port_abs_to_mouse(hid, dev))
		caps |= FORWARD_CAP_MOUSE_ABS;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		if (hidinput->input != dev)
			continue;

		matched_hidinput = 1;
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

		break;
	}

	if (!matched_hidinput) {
		list_for_each_entry(report, &hid->report_enum[HID_INPUT_REPORT].report_list, list) {
			switch (report->application) {
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
	}

	return caps;
}

static int input_port_dev_in_hid_inputs(struct hid_device *hid, struct input_dev *dev)
{
	struct hid_input *hidinput;

	if (!hid->inputs.next)
		return 0;

	list_for_each_entry(hidinput, &hid->inputs, list)
		if (hidinput->input == dev)
			return 1;

	return 0;
}

static int32_t input_port_abs_value(struct input_absinfo *abs)
{
	__s64 value = abs->value - abs->minimum;
	__s64 range = abs->maximum - abs->minimum;

	if (!range)
		return 0;

	return (int32_t)(value * INPUT_PORT_ABS_MAX / range);
}

static int input_port_open_hid_handle(struct input_handle *handle, void *data)
{
	if (input_get_drvdata(handle->dev) == data && !handle->open) {
		struct hid_device *hid = data;
		int ret;

		/*
		 * Linux opens input handles from userspace. Firmware has one
		 * always-on KeyD consumer after hid-input mapped the device.
		 */
		ret = input_open_device(handle);
		hid_host_trace_input_state(hid, 1, handle->open, ret);
		if (ret)
			return ret;
		forward_add_device_caps(hid->keyd_device, input_port_caps(hid, handle->dev));
	}

	return 0;
}

int input_port_activate_hid(struct hid_device *hid)
{
	return input_handler_for_each_handle(&input_port_handler, hid, input_port_open_hid_handle);
}

static int input_port_close_hid_handle(struct input_handle *handle, void *data)
{
	if (input_get_drvdata(handle->dev) == data && handle->open)
		input_close_device(handle);

	return 0;
}

void input_port_deactivate_hid(struct hid_device *hid)
{
	input_handler_for_each_handle(&input_port_handler, hid, input_port_close_hid_handle);
}

static unsigned int input_port_events(struct input_handle *handle,
				      struct input_value *vals,
				      unsigned int count)
{
	struct input_dev *dev = handle->dev;
	struct hid_device *hid = input_get_drvdata(dev);
	int32_t rel_x = 0;
	int32_t rel_y = 0;
	int32_t scroll_x = 0;
	int32_t scroll_y = 0;
	int abs_changed = 0;
	int abs_mouse = test_bit(ABS_X, dev->absbit) &&
			test_bit(ABS_Y, dev->absbit) &&
			input_port_abs_to_mouse(hid, dev);

	hid_host_trace_input_state(hid, 2, count, handle->open);

	for (unsigned int i = 0; i < count; i++) {
		unsigned int type = vals[i].type;
		unsigned int code = vals[i].code;
		int value = vals[i].value;

		switch (type) {
		case EV_KEY:
			hid_host_trace_input_event(hid, type, code, value);
			/* Upstream keyd/src/device.c ignores EV_KEY repeat value 2. */
			if (value == 2)
				break;
			forward_key(hid->keyd_device, code, !!value);
			break;
		case EV_REL:
			hid_host_trace_input_event(hid, type, code, value);
			if (code == REL_X)
				rel_x += value;
			else if (code == REL_Y)
				rel_y += value;
			else if (code == REL_HWHEEL)
				scroll_x += value;
			else if (code == REL_WHEEL)
				scroll_y += value;
			break;
		case EV_ABS:
			if ((code == ABS_X || code == ABS_Y) && abs_mouse)
				abs_changed = 1;
			break;
		default:
			break;
		}
	}

	if (rel_x || rel_y)
		forward_mouse_move(hid->keyd_device, rel_x, rel_y);
	if (scroll_x || scroll_y)
		forward_mouse_scroll(hid->keyd_device, scroll_x, scroll_y);
	if (abs_changed)
		forward_mouse_move_abs(hid->keyd_device,
				       input_port_abs_value(&dev->absinfo[ABS_X]),
				       input_port_abs_value(&dev->absinfo[ABS_Y]));

	return count;
}

static int input_port_connect(struct input_handler *handler,
			      struct input_dev *dev,
			      const struct input_device_id *id)
{
	struct hid_device *hid = input_get_drvdata(dev);
	struct input_handle *handle;
	int ret;

	(void)id;

	if (!hid && dev->dev.parent && dev->dev.parent->bus == &hid_bus_type) {
		// input_set_drvdata(input_dev, hid);
		// Some upstream drivers allocate extra input_dev objects from &hdev->dev
		// without storing HID drvdata. The port needs that link for KeyD forwarding.
		hid = to_hid_device(dev->dev.parent);
		input_set_drvdata(dev, hid);
	}

	handle = kzalloc_obj(*handle);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = handler->name;

	ret = input_register_handle(handle);
	if (ret)
		goto err_free;

	if (hid && hid->keyd_device) {
		// Linux opens input handles from userspace; this port has one always-on
		// consumer and opens late driver-created input devices immediately.
		ret = input_open_device(handle);
		if (ret)
			goto err_unregister;
		forward_add_device_caps(hid->keyd_device, input_port_caps(hid, dev));
	} else if (hid && !input_port_dev_in_hid_inputs(hid, dev)) {
		hid->keyd_device = forward_register_device(hid->vendor, hid->product,
							   input_port_caps(hid, dev));
		if (!hid->keyd_device) {
			ret = -EAGAIN;
			goto err_unregister;
		}

		ret = input_open_device(handle);
		if (ret)
			goto err_unregister_keyd;
	}

	return 0;

err_unregister_keyd:
	forward_unregister_device(hid->keyd_device);
	hid->keyd_device = NULL;
err_unregister:
	input_unregister_handle(handle);
err_free:
	kfree(handle);
	return ret;
}

static void input_port_disconnect(struct input_handle *handle)
{
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id input_port_ids[] = {
	{ .flags = INPUT_DEVICE_ID_MATCH_BUS, .bustype = BUS_USB },
	{ },
};

static struct input_handler input_port_handler = {
	.events = input_port_events,
	.connect = input_port_connect,
	.disconnect = input_port_disconnect,
	.name = "ergotype-input-port",
	.id_table = input_port_ids,
};

int input_port_init(void)
{
	return input_register_handler(&input_port_handler);
}
