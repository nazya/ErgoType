#ifndef USB_HOST_LINUX_LEDS_H
#define USB_HOST_LINUX_LEDS_H

#include "hid_compat.h"

// Upstream LED class is sysfs/device-core infrastructure. Firmware keeps
// the in-memory class device contract for HID driver LED/proxy state.

enum led_brightness {
	LED_OFF = 0,
	LED_ON = 1,
	LED_HALF = 127,
	LED_FULL = 255,
};

#define LED_CORE_SUSPENDRESUME BIT(16)
#define LED_RETAIN_AT_SHUTDOWN BIT(22)
#define LED_CLASSDEV_PROXY_ENUM_BEGIN ((__u32)-1)
#define LED_CLASSDEV_PROXY_STRING 64

struct led_init_data {
	const char *default_label;
};

struct led_classdev {
	const char *name;
	unsigned int brightness;
	unsigned int max_brightness;
	unsigned int color;
	int flags;
	__u32 port_proxy_id;
	void (*brightness_set)(struct led_classdev *led_cdev,
			       enum led_brightness brightness);
	int (*brightness_set_blocking)(struct led_classdev *led_cdev,
				       enum led_brightness brightness);
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);
	struct device compat_dev;
	struct device *dev;
	struct list_head node;
};

struct led_classdev_proxy_snapshot {
	__u32 proxy_id;
	__u32 brightness;
	__u32 max_brightness;
	__u32 color;
	__s32 flags;
	__u8 has_brightness_set;
	__u8 has_brightness_set_blocking;
	__u8 has_brightness_get;
	char name[LED_CLASSDEV_PROXY_STRING];
	char dev_name[LED_CLASSDEV_PROXY_STRING];
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
void devm_led_classdev_unregister(struct device *parent,
				  struct led_classdev *led_cdev);
void led_set_brightness(struct led_classdev *led_cdev, unsigned int brightness);
int led_set_brightness_sync(struct led_classdev *led_cdev, unsigned int value);
int led_update_brightness(struct led_classdev *led_cdev);
struct led_classdev *led_classdev_find_by_name(const char *name);
int led_classdev_for_each(int (*fn)(struct led_classdev *led_cdev, void *data), void *data);
int led_classdev_proxy_get_snapshot(__u32 proxy_id,
				    struct led_classdev_proxy_snapshot *snapshot);
int led_classdev_proxy_get_next_snapshot(__u32 after_proxy_id,
					 struct led_classdev_proxy_snapshot *snapshot);
int led_classdev_proxy_set_brightness_sync(__u32 proxy_id, unsigned int value);

#endif
