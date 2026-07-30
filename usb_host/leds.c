/*
 * Firmware boundary for the reduced Linux LED class and trigger contract.
 *
 * Linux runs blocking LED setters from its LED workqueue. Firmware keeps the
 * same nonblocking trigger edge on the common HID workqueue, so report parsing
 * never performs a synchronous USB control request.
 */
#include "linux/include/linux/leds.h"

static LIST_HEAD(led_trigger_list);
/*
 * Registration and removal run in the serialized HID lifecycle task. Report
 * callbacks only follow an already-bound trigger pointer, and hid_hw_stop()
 * fences them before lifecycle removal mutates the trigger list.
 */

static void led_set_brightness_work(struct work_struct *work)
{
	struct led_classdev *led_cdev =
		container_of(work, struct led_classdev, set_brightness_work);

	if (test_and_clear_bit(LED_SET_BRIGHTNESS_OFF,
			       &led_cdev->work_flags)) {
		if (led_cdev->brightness_set)
			led_cdev->brightness_set(led_cdev, LED_OFF);
		else if (led_cdev->brightness_set_blocking)
			led_cdev->brightness_set_blocking(led_cdev, LED_OFF);

		if (__atomic_load_n(&led_cdev->delayed_set_value,
				    __ATOMIC_ACQUIRE) != LED_OFF)
			set_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
	}

	if (test_and_clear_bit(LED_SET_BRIGHTNESS,
			       &led_cdev->work_flags)) {
		unsigned int brightness =
			__atomic_load_n(&led_cdev->delayed_set_value,
					__ATOMIC_ACQUIRE);

		if (led_cdev->brightness_set)
			led_cdev->brightness_set(led_cdev, brightness);
		else if (led_cdev->brightness_set_blocking)
			led_cdev->brightness_set_blocking(led_cdev, brightness);
	}
}

void led_set_brightness(struct led_classdev *led_cdev,
			unsigned int brightness)
{
	brightness = min(brightness, led_cdev->max_brightness);
	led_cdev->brightness = brightness;
	__atomic_store_n(&led_cdev->delayed_set_value, brightness,
			 __ATOMIC_RELEASE);

	if (brightness) {
		set_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
	} else {
		clear_bit(LED_SET_BRIGHTNESS, &led_cdev->work_flags);
		set_bit(LED_SET_BRIGHTNESS_OFF, &led_cdev->work_flags);
	}

	schedule_work(&led_cdev->set_brightness_work);
}

int led_update_brightness(struct led_classdev *led_cdev)
{
	int brightness;

	if (!led_cdev->brightness_get)
		return 0;

	brightness = led_cdev->brightness_get(led_cdev);
	if (brightness < 0)
		return brightness;
	led_cdev->brightness = brightness;
	return 0;
}

static void led_trigger_set(struct led_classdev *led_cdev,
			    struct led_trigger *trigger)
{
	if (led_cdev->trigger) {
		led_cdev->trigger->led_cdev = NULL;
		led_cdev->trigger = NULL;
		led_set_brightness(led_cdev, LED_OFF);
	}

	if (!trigger)
		return;

	led_cdev->trigger = trigger;
	trigger->led_cdev = led_cdev;
	led_set_brightness(led_cdev, trigger->brightness);
}

static void led_trigger_set_default(struct led_classdev *led_cdev)
{
	struct led_trigger *trigger;

	if (!led_cdev->default_trigger)
		return;

	list_for_each_entry(trigger, &led_trigger_list, node) {
		if (!strcmp(led_cdev->default_trigger, trigger->name)) {
			led_trigger_set(led_cdev, trigger);
			return;
		}
	}
}

static int led_trigger_register(struct led_trigger *trigger)
{
	struct led_trigger *registered;

	list_for_each_entry(registered, &led_trigger_list, node) {
		if (!strcmp(registered->name, trigger->name))
			return -EEXIST;
	}

	trigger->led_cdev = NULL;
	INIT_LIST_HEAD(&trigger->node);
	list_add_tail(&trigger->node, &led_trigger_list);

	return 0;
}

static void led_trigger_unregister(struct led_trigger *trigger)
{
	if (trigger->led_cdev)
		led_trigger_set(trigger->led_cdev, NULL);
	list_del(&trigger->node);
}

static void devm_led_trigger_release(void *data)
{
	led_trigger_unregister(data);
}

int devm_led_trigger_register(struct device *dev,
			      struct led_trigger *trigger)
{
	int error = led_trigger_register(trigger);

	if (error)
		return error;
	return devm_add_action_or_reset(dev, devm_led_trigger_release,
					trigger);
}

void led_trigger_event(struct led_trigger *trigger,
		       enum led_brightness brightness)
{
	if (!trigger)
		return;

	trigger->brightness = brightness;
	if (trigger->led_cdev)
		led_set_brightness(trigger->led_cdev, brightness);
}

int led_classdev_register_ext(struct device *parent,
			      struct led_classdev *led_cdev,
			      struct led_init_data *init_data)
{
	(void)init_data;

	device_initialize(&led_cdev->compat_dev);
	led_cdev->compat_dev.parent = parent;
	dev_set_name(&led_cdev->compat_dev, "%s",
		     led_cdev->name ? led_cdev->name : "led");
	led_cdev->dev = &led_cdev->compat_dev;
	if (!led_cdev->max_brightness)
		led_cdev->max_brightness = LED_FULL;

	led_cdev->work_flags = 0;
	led_cdev->trigger = NULL;
	INIT_WORK(&led_cdev->set_brightness_work, led_set_brightness_work);
	led_update_brightness(led_cdev);
	led_trigger_set_default(led_cdev);
	return 0;
}

void led_classdev_unregister(struct led_classdev *led_cdev)
{
	led_trigger_set(led_cdev, NULL);
	if (!(led_cdev->flags & LED_RETAIN_AT_SHUTDOWN))
		led_set_brightness(led_cdev, LED_OFF);
	flush_work(&led_cdev->set_brightness_work);

	led_cdev->compat_dev.parent = NULL;
	led_cdev->dev = NULL;
}

static void devm_led_classdev_release(void *data)
{
	led_classdev_unregister(data);
}

int devm_led_classdev_register_ext(struct device *parent,
				   struct led_classdev *led_cdev,
				   struct led_init_data *init_data)
{
	int error = led_classdev_register_ext(parent, led_cdev, init_data);

	if (error)
		return error;
	return devm_add_action_or_reset(parent, devm_led_classdev_release,
					led_cdev);
}
