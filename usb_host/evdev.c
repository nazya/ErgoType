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

static uint8_t evdev_mask8(const unsigned long *bits)
{
	uint8_t mask = 0;

	for (unsigned int i = 0; i < 8; i++)
		if (test_bit(i, bits))
			mask |= BIT(i);

	return mask;
}

static uint32_t evdev_count_keys(struct input_dev *dev)
{
	uint32_t count = 0;

	for (unsigned int i = 0; i < BITS_TO_LONGS(KEY_CNT); i++)
		count += __builtin_popcountl(dev->keybit[i]);

	return count;
}

static void evdev_copy_keymask(uint32_t *mask, struct input_dev *dev)
{
	for (unsigned int bit = 0; bit < EVDEV_KEYMASK_WORDS * 32; bit++)
		if (test_bit(bit, dev->keybit))
			mask[bit / 32] |= BIT(bit % 32);
}

static struct evdev_input_info evdev_input_info(struct input_dev *dev)
{
	struct evdev_input_info info = {0};

	evdev_copy_keymask(info.keymask, dev);
	info.num_keys = evdev_count_keys(dev);
	info.relmask = evdev_mask8(dev->relbit);
	info.absmask = evdev_mask8(dev->absbit);

	if (test_bit(ABS_X, dev->absbit) && test_bit(ABS_Y, dev->absbit)) {
		info.minx = dev->absinfo[ABS_X].minimum;
		info.maxx = dev->absinfo[ABS_X].maximum;
		info.miny = dev->absinfo[ABS_Y].minimum;
		info.maxy = dev->absinfo[ABS_Y].maximum;
	}

	return info;
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
	struct evdev_input_info info;
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
	info = evdev_input_info(dev);
	event_dev = evdev_register_device(hid->vendor, hid->product, &info);
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
