#ifndef USB_HOST_LINUX_LEDS_H
#define USB_HOST_LINUX_LEDS_H

#include "hid_compat.h"

// Upstream LED class is sysfs/device-core infrastructure. Firmware keeps the
// in-memory class device contract used by the active Wacom driver.

enum led_brightness {
	LED_OFF = 0,
	LED_ON = 1,
	LED_HALF = 127,
	LED_FULL = 255,
};

#define LED_CORE_SUSPENDRESUME BIT(16)
#define LED_HW_PLUGGABLE BIT(19)
#define LED_RETAIN_AT_SHUTDOWN BIT(22)
#define LED_SET_BRIGHTNESS_OFF 6
#define LED_SET_BRIGHTNESS 7

struct led_init_data {
	const char *default_label;
};

struct led_classdev;

// Active Wacom callers register one private trigger before its class device;
// Linux reverse registration, multi-LED trigger lists, and sysfs locking
// remain outside this contract.
struct led_trigger {
	const char *name;
	enum led_brightness brightness;
	struct led_classdev *led_cdev;
	struct list_head node;
};

struct led_classdev {
	const char *name;
	unsigned int brightness;
	unsigned int max_brightness;
	unsigned int color;
	int flags;
	unsigned long work_flags;
	void (*brightness_set)(struct led_classdev *led_cdev,
			       enum led_brightness brightness);
	int (*brightness_set_blocking)(struct led_classdev *led_cdev,
				       enum led_brightness brightness);
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);
	const char *default_trigger;
	struct led_trigger *trigger;
	struct work_struct set_brightness_work;
	int delayed_set_value;
	struct device compat_dev;
	struct device *dev;
};

int led_classdev_register_ext(struct device *parent,
			      struct led_classdev *led_cdev,
			      struct led_init_data *init_data);

static inline int led_classdev_register(struct device *parent,
					struct led_classdev *led_cdev)
{
	return led_classdev_register_ext(parent, led_cdev, NULL);
}

int devm_led_classdev_register_ext(struct device *parent,
				   struct led_classdev *led_cdev,
				   struct led_init_data *init_data);

static inline int devm_led_classdev_register(struct device *parent,
					     struct led_classdev *led_cdev)
{
	return devm_led_classdev_register_ext(parent, led_cdev, NULL);
}

void led_classdev_unregister(struct led_classdev *led_cdev);
void led_set_brightness(struct led_classdev *led_cdev, unsigned int brightness);
int led_update_brightness(struct led_classdev *led_cdev);
int devm_led_trigger_register(struct device *dev, struct led_trigger *trigger);
void led_trigger_event(struct led_trigger *trigger,
		       enum led_brightness brightness);

#endif
