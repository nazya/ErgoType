// SPDX-License-Identifier: GPL-2.0-only
/*
 * Event char devices, giving access to raw input device events.
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 *
 * Port note: firmware keeps the upstream input_handler shape and replaces
 * userspace eventX file descriptors with a devmon-backed client.
 */
#include "linux/include/linux/input.h"
#include "evdev.h"

struct evdev {
	// int open;
	struct input_handle handle;
	// struct evdev_client __rcu *grab;
	// Firmware has one devmon-backed client instead of Linux grab/client_list.
	struct evdev_client *client;
	// struct list_head client_list;
	// spinlock_t client_lock; /* protects client_list */
	// struct mutex mutex;
	// struct device dev;
	// struct cdev cdev;
	// bool exist;
};

static void evdev_pass_values(struct evdev_client *client,
			      const struct input_value *vals, unsigned int count,
			      ktime_t *ev_time)
{
	const struct input_value *v;
	struct input_event event;

	// struct timespec64 ts;
	// bool wakeup = false;
	// if (client->revoked)
	// 	return;
	// ts = ktime_to_timespec64(ev_time[client->clk_type]);
	// event.input_event_sec = ts.tv_sec;
	// event.input_event_usec = ts.tv_nsec / NSEC_PER_USEC;
	// spin_lock(&client->buffer_lock);
	// Firmware keyd/evloop timestamps events after reading devmon.
	(void)ev_time;
	for (v = vals; v != vals + count; v++) {
		// if (__evdev_is_filtered(client, v->type, v->code))
		// 	continue;
		// Firmware has no per-fd event masks.
		// if (v->type == EV_SYN && v->code == SYN_REPORT) {
		// 	if (client->packet_head == client->head)
		// 		continue;
		// 	wakeup = true;
		// }
		// Firmware keeps SYN_REPORT as the frame boundary for keyd.
		event.type = v->type;
		event.code = v->code;
		event.value = v->value;
		__pass_event(client, &event);
	}
	// spin_unlock(&client->buffer_lock);
	// if (wakeup)
	// 	wake_up_interruptible_poll(&client->wait,
	// 		EPOLLIN | EPOLLOUT | EPOLLRDNORM | EPOLLWRNORM);
	// Firmware wakes keyd through the devmon queue set.
}

static unsigned int evdev_events(struct input_handle *handle,
				 struct input_value *vals, unsigned int count)
{
	struct evdev *evdev = handle->private;
	struct evdev_client *client;
	ktime_t *ev_time = input_get_timestamp(handle->dev);

	// rcu_read_lock();
	// client = rcu_dereference(evdev->grab);
	// if (client)
	// 	evdev_pass_values(client, vals, count, ev_time);
	// else
	// 	list_for_each_entry_rcu(client, &evdev->client_list, node)
	// 		evdev_pass_values(client, vals, count, ev_time);
	// rcu_read_unlock();
	// Firmware has one devmon-backed client.
	client = evdev->client;
	evdev_pass_values(client, vals, count, ev_time);

	return count;
}

// static ssize_t evdev_write(struct file *file, const char __user *buffer,
// 			   size_t count, loff_t *ppos)
// Firmware has no userspace file; callers pass already-local port event data.
int evdev_write(struct evdev *evdev, const struct port_input_event *events, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		// input_event_from_user(buffer + retval, &event);
		// Firmware event data is already local memory.
		// input_inject_event(&evdev->handle,
		// 		   event.type, event.code, event.value);
		input_inject_event(&evdev->handle, events[i].type,
				   events[i].code, events[i].value);
	}

	return count;
}

int evdev_upload_ff(struct evdev *evdev, struct ff_effect *effect, struct file *file)
{
	struct input_dev *dev = evdev->handle.dev;

	// error = input_ff_upload(dev, &effect, file);
	// Firmware caller passes an already-local ff_effect.
	return input_ff_upload(dev, effect, file);
}

int evdev_erase_ff(struct evdev *evdev, int effect_id, struct file *file)
{
	struct input_dev *dev = evdev->handle.dev;

	// return input_ff_erase(dev, (int)(unsigned long) p, file);
	// Firmware caller passes the effect id directly.
	return input_ff_erase(dev, effect_id, file);
}

/*
 * Create new evdev device. Note that input core serializes calls
 * to connect and disconnect.
 */
static int evdev_connect(struct input_handler *handler, struct input_dev *dev,
			 const struct input_device_id *id)
{
	struct evdev *evdev;
	int error;

	(void)id;

	// minor = input_get_new_minor(EVDEV_MINOR_BASE, EVDEV_MINORS, true);
	// Firmware has no userspace eventX cdev/minor; devmon owns the client
	// boundary for this input_dev.
	evdev = kzalloc_obj(struct evdev);
	if (!evdev) {
		error = -ENOMEM;
		goto err_free_minor;
	}

	// INIT_LIST_HEAD(&evdev->client_list);
	// spin_lock_init(&evdev->client_lock);
	// mutex_init(&evdev->mutex);
	// evdev->exist = true;
	// Firmware has no eventX client list/mutex/open count.

	// dev_no = minor;
	// if (dev_no < EVDEV_MINOR_BASE + EVDEV_MINORS)
	// 	dev_no -= EVDEV_MINOR_BASE;
	// dev_set_name(&evdev->dev, "event%d", dev_no);
	// Firmware does not allocate Linux input minors.

	// evdev->handle.dev = input_get_device(dev);
	// evdev->handle.name = dev_name(&evdev->dev);
	evdev->handle.dev = dev;
	evdev->handle.name = handler->name;
	evdev->handle.handler = handler;
	evdev->handle.private = evdev;

	error = input_register_handle(&evdev->handle);
	if (error)
		goto err_free_evdev;

	// evdev->dev.devt = MKDEV(INPUT_MAJOR, minor);
	// evdev->dev.class = &input_class;
	// evdev->dev.parent = &dev->dev;
	// evdev->dev.release = evdev_free;
	// device_initialize(&evdev->dev);
	// cdev_init(&evdev->cdev, &evdev_fops);
	// error = cdev_device_add(&evdev->cdev, &evdev->dev);
	// Firmware registers a devmon-backed client instead of Linux eventX fd.
	evdev->client = evdev_register_input_device(dev, evdev);
	if (!evdev->client) {
		error = -EAGAIN;
		goto err_unregister_handle;
	}

	// error = evdev_open_device(evdev);
	// Firmware has no userspace open(eventX); keep the input handle always open.
	error = input_open_device(&evdev->handle);
	if (error)
		goto err_cleanup_evdev;

	return 0;

err_cleanup_evdev:
	evdev_unregister_device(evdev->client);
err_unregister_handle:
	input_unregister_handle(&evdev->handle);
err_free_evdev:
	kfree(evdev);
err_free_minor:
	// input_free_minor(minor);
	// Firmware has no Linux input minor to release.
	return error;
}

static void evdev_disconnect(struct input_handle *handle)
{
	struct evdev *evdev = handle->private;

	// cdev_device_del(&evdev->cdev, &evdev->dev);
	// evdev_cleanup(evdev);
	// Upstream evdev_cleanup() flushes open clients before close. Firmware
	// disconnect is physical USB teardown, so sending FF cleanup reports here
	// is too late; keep that for a future close-like client lifecycle.
	// input_free_minor(MINOR(evdev->dev.devt));
	// put_device(&evdev->dev);
	// Firmware tears down the devmon-backed client directly.
	if (handle->open)
		input_close_device(handle);
	evdev_unregister_device(evdev->client);
	input_unregister_handle(handle);
	kfree(evdev);
}

static const struct input_device_id evdev_ids[] = {
	{
		/* Matches all devices */
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_SYN) },
	},
	{ }	/* Terminating zero entry */
};

// MODULE_DEVICE_TABLE(input, evdev_ids);
// Firmware links input handlers statically and has no Linux module table.
static struct input_handler evdev_handler = {
	.events		= evdev_events,
	.connect	= evdev_connect,
	.disconnect	= evdev_disconnect,
	// .legacy_minors	= true,
	// .minor		= EVDEV_MINOR_BASE,
	// Firmware has no Linux input minors.
	.name		= "evdev",
	.id_table	= evdev_ids,
};

// static int __init evdev_init(void)
// Firmware calls evdev_init() from the host startup path.
int evdev_init(void)
{
	return input_register_handler(&evdev_handler);
}
