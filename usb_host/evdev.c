// SPDX-License-Identifier: GPL-2.0-only
/*
 * Port note: upstream drivers/input/evdev.c exposes input events through
 * userspace file descriptors. This firmware keeps the upstream input_handler
 * shape and sends Linux-style EV_* records to a firmware evdev queue instead
 * of userspace eventX file descriptors.
 */
#include <string.h>

#include "linux/include/linux/hid.h"
#include "evdev.h"

#include "linux/include/uapi/linux/input-event-codes.h"

static struct input_handler evdev_handler;

static void evdev_copy_absinfo(struct input_absinfo_snapshot *dst,
			       const struct input_absinfo *src)
{
	dst->value = src->value;
	dst->minimum = src->minimum;
	dst->maximum = src->maximum;
	dst->fuzz = src->fuzz;
	dst->flat = src->flat;
	dst->resolution = src->resolution;
}

static struct port_input_dev evdev_port_input_dev(const struct input_dev *src,
						  uint16_t vendor,
						  uint16_t product)
{
	struct port_input_dev port_dev = {0};

	port_dev.vendor = vendor;
	port_dev.product = product;
	port_dev.name = src->name ? src->name : "usb-hid";
	memcpy(port_dev.keybit, src->keybit, sizeof(port_dev.keybit));
	memcpy(port_dev.relbit, src->relbit, sizeof(port_dev.relbit));
	memcpy(port_dev.absbit, src->absbit, sizeof(port_dev.absbit));
	memcpy(port_dev.propbit, src->propbit, sizeof(port_dev.propbit));

	/*
	 * This is the compression point from full Linux input_dev state to the
	 * devmon queue snapshot. KeyD currently mirrors only EVIOCGABS(ABS_X/Y);
	 * extend this struct/function if a future path needs more absinfo.
	 */
	if (test_bit(ABS_X, src->absbit) && test_bit(ABS_Y, src->absbit)) {
		evdev_copy_absinfo(&port_dev.abs_x, &src->absinfo[ABS_X]);
		evdev_copy_absinfo(&port_dev.abs_y, &src->absinfo[ABS_Y]);
	}

	return port_dev;
}

static unsigned int evdev_events(struct input_handle *handle,
				      struct input_value *vals,
				      unsigned int count)
{
	void *event_dev = handle->private;

	for (unsigned int i = 0; i < count; i++) {
		unsigned int type = vals[i].type;
		unsigned int code = vals[i].code;
		int value = vals[i].value;

		switch (type) {
		case EV_KEY:
		case EV_REL:
		case EV_ABS:
		case EV_SYN:
		case EV_LED:
			evdev_input_event(event_dev, type, code, value);
			break;
		default:
			/*
			 * The firmware evdev queue currently carries the event
			 * classes consumed by the input remapper. Keep the upstream
			 * input pipeline intact and stop unsupported event types here.
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
	struct port_input_dev port_dev;
	struct input_handle *handle;
	void *event_dev;
	int ret;

	(void)id;

	if (!hid && dev->dev.parent && dev->dev.parent->bus == &hid_bus_type) {
		// input_set_drvdata(input_dev, hid);
		// Some upstream drivers allocate extra input_dev objects from &hdev->dev
		// without storing HID drvdata. The firmware evdev layer needs that link.
		hid = to_hid_device(dev->dev.parent);
		input_set_drvdata(dev, hid);
	}

	handle = kzalloc_obj(*handle);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = handler->name;
	port_dev = evdev_port_input_dev(dev, hid->vendor, hid->product);
	event_dev = evdev_register_device(&port_dev);
	if (!event_dev) {
		ret = -EAGAIN;
		goto err_free;
	}
	handle->private = event_dev;

	ret = input_register_handle(handle);
	if (ret)
		goto err_unregister_device;

	// retval = input_open_device(&evdev->handle);
	// Firmware has no userspace open(eventX); keep the evdev handle always open.
	ret = input_open_device(handle);
	if (ret)
		goto err_unregister;

	return 0;

err_unregister:
	input_unregister_handle(handle);
err_unregister_device:
	evdev_unregister_device(event_dev);
err_free:
	kfree(handle);
	return ret;
}

static void evdev_disconnect(struct input_handle *handle)
{
	void *event_dev = handle->private;

	if (handle->open)
		input_close_device(handle);
	input_unregister_handle(handle);
	evdev_unregister_device(event_dev);
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
