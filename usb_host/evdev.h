#pragma once

#include <stddef.h>
#include <stdint.h>

#include "devmon.h"

struct input_dev;
struct input_event;
struct input_handle;
struct evdev;
struct evdev_client;
struct ff_effect;
struct file;
struct hid_device;

struct evdev_client *evdev_register_input_device(struct input_dev *src,
						 struct input_handle *handle,
						 struct evdev *evdev,
						 struct evdev_client **client_slot);
void evdev_unregister_device(struct evdev_client *dev);
void __pass_event(struct evdev_client *dev,
		  const struct input_event *ev);
int evdev_client_write(struct evdev_client *client,
		       const struct port_input_event *events, size_t count);
int evdev_client_upload_ff(struct evdev_client *client, struct ff_effect *effect);
int evdev_client_erase_ff(struct evdev_client *client, int effect_id);
int evdev_write(struct evdev *evdev, const struct port_input_event *events, size_t count);
int evdev_upload_ff(struct evdev *evdev, struct ff_effect *effect, struct file *file);
int evdev_erase_ff(struct evdev *evdev, int effect_id, struct file *file);
int evdev_activate_hid(struct hid_device *hid);
int evdev_init(void);
