// SPDX-License-Identifier: GPL-2.0-only
/*
 * Port note: upstream drivers/input/evdev.c exposes input events through
 * userspace file descriptors. This firmware keeps the upstream input_handler
 * shape and sends Linux-style EV_* records to the KeyD device queue instead.
 * KeyD device.c performs the upstream-style EV_* -> device_event conversion
 * that Linux keyd normally performs after read(fd, input_event).
 */
#include "linux/include/linux/hid.h"
#include "evdev.h"
#include "hid_port.h"

#include "linux/include/uapi/linux/input-event-codes.h"

static struct input_handler evdev_handler;

static int evdev_abs_to_mouse(struct hid_device *hid, struct input_dev *dev)
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

static uint8_t evdev_caps(struct hid_device *hid, struct input_dev *dev)
{
	uint8_t caps = 0;
	struct hid_input *hidinput;
	struct hid_report *report;
	int matched_hidinput = 0;

	for (unsigned int i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
		if (dev->keybit[i])
			caps |= EVDEV_CAP_KEY;

	if (test_bit(REL_X, dev->relbit) || test_bit(REL_Y, dev->relbit) ||
	    test_bit(REL_WHEEL, dev->relbit) || test_bit(REL_HWHEEL, dev->relbit))
		caps |= EVDEV_CAP_MOUSE;

	if (test_bit(ABS_X, dev->absbit) &&
	    test_bit(ABS_Y, dev->absbit) &&
	    evdev_abs_to_mouse(hid, dev))
		caps |= EVDEV_CAP_MOUSE_ABS;

	list_for_each_entry(hidinput, &hid->inputs, list) {
		if (hidinput->input != dev)
			continue;

		matched_hidinput = 1;
		switch (hidinput->application) {
		case HID_GD_KEYBOARD:
		case HID_GD_KEYPAD:
			caps |= EVDEV_CAP_KEY | EVDEV_CAP_KEYBOARD;
			break;
		case HID_GD_MOUSE:
		case HID_GD_POINTER:
			caps |= EVDEV_CAP_MOUSE;
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
				caps |= EVDEV_CAP_KEY | EVDEV_CAP_KEYBOARD;
				break;
			case HID_GD_MOUSE:
			case HID_GD_POINTER:
				caps |= EVDEV_CAP_MOUSE;
				break;
			default:
				break;
			}
		}
	}

	return caps;
}

static int evdev_dev_in_hid_inputs(struct hid_device *hid, struct input_dev *dev)
{
	struct hid_input *hidinput;

	if (!hid->inputs.next)
		return 0;

	list_for_each_entry(hidinput, &hid->inputs, list)
		if (hidinput->input == dev)
			return 1;

	return 0;
}

static void evdev_configure_abs_dev(void *keyd_device, struct input_dev *dev)
{
	if (test_bit(ABS_X, dev->absbit) && test_bit(ABS_Y, dev->absbit))
		evdev_configure_abs(keyd_device,
				      dev->absinfo[ABS_X].minimum,
				      dev->absinfo[ABS_X].maximum,
				      dev->absinfo[ABS_Y].minimum,
				      dev->absinfo[ABS_Y].maximum);
}

static int evdev_open_hid_handle(struct input_handle *handle, void *data)
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
		evdev_add_device_caps(hid->keyd_device, evdev_caps(hid, handle->dev));
		evdev_configure_abs_dev(hid->keyd_device, handle->dev);
	}

	return 0;
}

int evdev_activate_hid(struct hid_device *hid)
{
	return input_handler_for_each_handle(&evdev_handler, hid, evdev_open_hid_handle);
}

static int evdev_close_hid_handle(struct input_handle *handle, void *data)
{
	if (input_get_drvdata(handle->dev) == data && handle->open)
		input_close_device(handle);

	return 0;
}

void evdev_deactivate_hid(struct hid_device *hid)
{
	input_handler_for_each_handle(&evdev_handler, hid, evdev_close_hid_handle);
}

static unsigned int evdev_events(struct input_handle *handle,
				      struct input_value *vals,
				      unsigned int count)
{
	struct input_dev *dev = handle->dev;
	struct hid_device *hid = input_get_drvdata(dev);

	hid_host_trace_input_state(hid, 2, count, handle->open);

	for (unsigned int i = 0; i < count; i++) {
		unsigned int type = vals[i].type;
		unsigned int code = vals[i].code;
		int value = vals[i].value;

		switch (type) {
		case EV_KEY:
		case EV_REL:
			hid_host_trace_input_event(hid, type, code, value);
			break;
		case EV_ABS:
			if ((code == ABS_X || code == ABS_Y) &&
			    evdev_abs_to_mouse(hid, dev))
				hid_host_trace_input_event(hid, type, code, value);
			break;
		default:
			break;
		}

		switch (type) {
		case EV_KEY:
		case EV_REL:
		case EV_ABS:
		case EV_SYN:
		case EV_LED:
			evdev_input_event(hid->keyd_device, type, code, value);
			break;
		default:
			/*
			 * Current KeyD boundary has no sink for the rest of the
			 * Linux input stream. Keep the upstream input pipeline
			 * intact and stop unsupported event types here.
			 */
			break;
		}
	}
	return count;
}

static int evdev_connect(struct input_handler *handler,
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
		evdev_add_device_caps(hid->keyd_device, evdev_caps(hid, dev));
		evdev_configure_abs_dev(hid->keyd_device, dev);
	} else if (hid && !evdev_dev_in_hid_inputs(hid, dev)) {
		hid->keyd_device = evdev_register_device(hid->vendor, hid->product,
							   evdev_caps(hid, dev));
		if (!hid->keyd_device) {
			ret = -EAGAIN;
			goto err_unregister;
		}

		ret = input_open_device(handle);
		if (ret)
			goto err_unregister_keyd;
		evdev_configure_abs_dev(hid->keyd_device, dev);
	}

	return 0;

err_unregister_keyd:
	evdev_unregister_device(hid->keyd_device);
	hid->keyd_device = NULL;
err_unregister:
	input_unregister_handle(handle);
err_free:
	kfree(handle);
	return ret;
}

static void evdev_disconnect(struct input_handle *handle)
{
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id evdev_ids[] = {
	{ .flags = INPUT_DEVICE_ID_MATCH_BUS, .bustype = BUS_USB },
	{ },
};

static struct input_handler evdev_handler = {
	.events = evdev_events,
	.connect = evdev_connect,
	.disconnect = evdev_disconnect,
	.name = "ergotype-evdev",
	.id_table = evdev_ids,
};

int evdev_init(void)
{
	return input_register_handler(&evdev_handler);
}
