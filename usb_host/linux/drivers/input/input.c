#include "../../include/linux/hid.h"
#include "../../include/linux/input.h"
#include "../../include/linux/input/mt.h"

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);
static unsigned int input_next_proxy_id;

struct input_find_cmd {
	const char *name;
	struct input_dev *dev;
};

struct input_for_each_cmd {
	int (*fn)(struct input_dev *dev, void *data);
	void *data;
};

static const struct input_value input_value_sync = { EV_SYN, SYN_REPORT, 1 };
static const unsigned int input_max_code[EV_CNT] = {
	[EV_KEY] = KEY_MAX,
	[EV_REL] = REL_MAX,
	[EV_ABS] = ABS_MAX,
	[EV_MSC] = MSC_MAX,
	[EV_SW] = SW_MAX,
	[EV_LED] = LED_MAX,
	[EV_SND] = SND_MAX,
	[EV_FF] = FF_MAX,
};

static inline int is_event_supported(unsigned int code,
				     unsigned long *bm, unsigned int max)
{
	return code <= max && test_bit(code, bm);
}

static void devm_input_device_unregister(void *data);
static void __input_unregister_device(struct input_dev *dev);
static void input_disconnect_device(struct input_dev *dev);
static void input_cleanse_bitmasks(struct input_dev *dev);
static int input_device_tune_vals(struct input_dev *dev);
static int input_default_getkeycode(struct input_dev *dev,
				    struct input_keymap_entry *ke);
int input_default_setkeycode(struct input_dev *dev,
			     const struct input_keymap_entry *ke,
			     unsigned int *old_keycode);
static void input_handle_event_locked(struct input_dev *dev,
				      unsigned int type, unsigned int code, int value);
static void input_event_dispose(struct input_dev *dev, int disposition,
				unsigned int type, unsigned int code, int value);
static void input_repeat_key(struct timer_list *t);

struct input_dev *input_allocate_device(void)
{
	struct input_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return NULL;

	/*
	 * Start with space for SYN_REPORT + 7 EV_KEY/EV_MSC events + 2 spare,
	 * see input_estimate_events_per_packet(). We will tune the number
	 * when we register the device.
	 */
	dev->max_vals = 10;
	dev->vals = kzalloc_objs(*dev->vals, dev->max_vals);
	if (!dev->vals) {
		kfree(dev);
		return NULL;
	}
	// dev->port_event_lock = xSemaphoreCreateMutex();
	// Callback-driven slice has no input worker; do not allocate a blocking lock.

	device_initialize(&dev->dev);
	// mutex_init(&dev->mutex);
	// spin_lock_init(&dev->event_lock);
	// Port input core has no mutex/event_lock; timer exists for upstream autorepeat.
	timer_setup(&dev->timer, NULL, 0);
	INIT_LIST_HEAD(&dev->h_list);
	INIT_LIST_HEAD(&dev->node);
	return dev;
}

struct input_dev *devm_input_allocate_device(struct device *dev)
{
	struct input_dev *input = input_allocate_device();

	if (!input)
		return NULL;

	input->devres_managed = true;
	input->dev.parent = dev;
	if (devm_add_action_or_reset(dev, devm_input_device_unregister, input))
		return NULL;

	return input;
}

void input_free_device(struct input_dev *dev)
{
	if (!dev)
		return;

	if (dev->devres_managed && dev->dev.parent) {
		if (!devm_release_action(dev->dev.parent, devm_input_device_unregister, dev))
			return;
	}

	input_ff_destroy(dev);
	input_mt_destroy_slots(dev);
	kfree(dev->absinfo);
	kfree(dev->vals);
	// vSemaphoreDelete(dev->port_event_lock);
	// Callback-driven slice does not allocate port_event_lock.
	kfree(dev);
}

static void devm_input_device_unregister(void *data)
{
	struct input_dev *dev = data;

	dev->devres_managed = false;
	if (dev->registered)
		input_unregister_device(dev);
	else
		input_free_device(dev);
}

bool input_match_device_id(const struct input_dev *dev,
			   const struct input_device_id *id)
{
	if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
		if (id->bustype != dev->id.bustype)
			return false;

	if (id->flags & INPUT_DEVICE_ID_MATCH_VENDOR)
		if (id->vendor != dev->id.vendor)
			return false;

	if (id->flags & INPUT_DEVICE_ID_MATCH_PRODUCT)
		if (id->product != dev->id.product)
			return false;

	if (id->flags & INPUT_DEVICE_ID_MATCH_VERSION)
		if (id->version != dev->id.version)
			return false;

	if (!bitmap_subset(id->evbit, dev->evbit, EV_CNT) ||
	    !bitmap_subset(id->keybit, dev->keybit, KEY_CNT) ||
	    !bitmap_subset(id->relbit, dev->relbit, REL_CNT) ||
	    !bitmap_subset(id->absbit, dev->absbit, ABS_CNT) ||
	    !bitmap_subset(id->mscbit, dev->mscbit, MSC_CNT) ||
	    !bitmap_subset(id->ledbit, dev->ledbit, LED_CNT) ||
	    !bitmap_subset(id->sndbit, dev->sndbit, SND_CNT) ||
	    !bitmap_subset(id->ffbit, dev->ffbit, FF_CNT) ||
	    !bitmap_subset(id->swbit, dev->swbit, SW_CNT) ||
	    !bitmap_subset(id->propbit, dev->propbit, INPUT_PROP_CNT)) {
		return false;
	}

	return true;
}

static const struct input_device_id *input_match_device(struct input_handler *handler,
							struct input_dev *dev)
{
	const struct input_device_id *id;

	for (id = handler->id_table; id->flags; id++)
		if (input_match_device_id(dev, id) &&
		    (!handler->match || handler->match(handler, dev)))
			return id;

	return NULL;
}

static int input_attach_handler(struct input_dev *dev, struct input_handler *handler)
{
	const struct input_device_id *id;
	int error;

	id = input_match_device(handler, dev);
	if (!id)
		return -ENODEV;

	error = handler->connect(handler, dev, id);
	if (error && error != -ENODEV)
		pr_err("failed to attach handler %s to device %s, error: %d\n",
		       handler->name, dev->name ? dev->name : "input", error);

	return error;
}

int input_register_device(struct input_dev *dev)
{
	struct input_handler *handler;
	int error;

	if (test_bit(EV_ABS, dev->evbit) && !dev->absinfo) {
		pr_err("Absolute device without dev->absinfo, refusing to register\n");
		return -EINVAL;
	}

	/* Every input device generates EV_SYN/SYN_REPORT events. */
	set_bit(EV_SYN, dev->evbit);

	/* KEY_RESERVED is not supposed to be transmitted to userspace. */
	clear_bit(KEY_RESERVED, dev->keybit);

	/* Make sure that bitmasks not mentioned in dev->evbit are clean. */
	input_cleanse_bitmasks(dev);

	error = input_device_tune_vals(dev);
	if (error)
		return error;

	/*
	 * If delay and period are pre-set by the driver, then autorepeating
	 * is handled by the driver itself and we don't do it in input.c.
	 */
	if (!dev->rep[REP_DELAY] && !dev->rep[REP_PERIOD])
		input_enable_softrepeat(dev, 250, 33);

	if (!dev->getkeycode)
		dev->getkeycode = input_default_getkeycode;

	if (!dev->setkeycode)
		dev->setkeycode = input_default_setkeycode;

	// if (dev->poller)
	// 	input_dev_poller_finalize(dev->poller);
	// Port has no input poller core.

	// Upstream device_add()/procfs/devres registration stops here; firmware
	// keeps an in-memory input_dev list and attaches handlers directly.
	list_add_tail(&dev->node, &input_dev_list);
	dev->port_proxy_id = ++input_next_proxy_id;
	dev->registered = true;
	list_for_each_entry(handler, &input_handler_list, node)
		input_attach_handler(dev, handler);

	return 0;
}

#define INPUT_CLEANSE_BITMASK(dev, type, bits)				\
	do {								\
		if (!test_bit(EV_##type, dev->evbit))			\
			memset(dev->bits##bit, 0,			\
				sizeof(dev->bits##bit));		\
	} while (0)

static void input_cleanse_bitmasks(struct input_dev *dev)
{
	INPUT_CLEANSE_BITMASK(dev, KEY, key);
	INPUT_CLEANSE_BITMASK(dev, REL, rel);
	INPUT_CLEANSE_BITMASK(dev, ABS, abs);
	INPUT_CLEANSE_BITMASK(dev, MSC, msc);
	INPUT_CLEANSE_BITMASK(dev, LED, led);
	INPUT_CLEANSE_BITMASK(dev, SND, snd);
	INPUT_CLEANSE_BITMASK(dev, FF, ff);
	INPUT_CLEANSE_BITMASK(dev, SW, sw);
}

static void __input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle, *next;

	input_disconnect_device(dev);
	list_for_each_entry_safe(handle, next, &dev->h_list, d_node)
		handle->handler->disconnect(handle);
	timer_delete_sync(&dev->timer);
	list_del(&dev->node);
	dev->registered = false;
}

void input_unregister_device(struct input_dev *dev)
{
	__input_unregister_device(dev);
	input_free_device(dev);
}

static int input_handler_check_methods(const struct input_handler *handler)
{
	int count = 0;

	if (handler->filter)
		count++;
	if (handler->events)
		count++;
	if (handler->event)
		count++;

	if (count > 1) {
		pr_err("%s: only one event processing method can be defined (%s)\n",
		       __func__, handler->name);
		return -EINVAL;
	}

	return 0;
}

int input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	int error;

	error = input_handler_check_methods(handler);
	if (error)
		return error;

	INIT_LIST_HEAD(&handler->h_list);
	list_add_tail(&handler->node, &input_handler_list);
	list_for_each_entry(dev, &input_dev_list, node)
		input_attach_handler(dev, handler);

	return 0;
}

void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle, *next;

	list_for_each_entry_safe(handle, next, &handler->h_list, h_node)
		handler->disconnect(handle);
	list_del(&handler->node);
}

int input_handler_for_each_handle(struct input_handler *handler, void *data,
				  int (*fn)(struct input_handle *handle, void *data))
{
	struct input_handle *handle;
	int retval;

	// list_for_each_entry_rcu(handle, &handler->h_list, h_node) {
	// Port input core has no RCU.
	list_for_each_entry(handle, &handler->h_list, h_node) {
		retval = fn(handle, data);
		if (retval)
			return retval;
	}

	return 0;
}

static unsigned int input_handle_events_default(struct input_handle *handle,
						struct input_value *vals,
						unsigned int count)
{
	struct input_handler *handler = handle->handler;
	struct input_value *v;

	for (v = vals; v != vals + count; v++)
		handler->event(handle, v->type, v->code, v->value);

	return count;
}

static unsigned int input_handle_events_filter(struct input_handle *handle,
					       struct input_value *vals,
					       unsigned int count)
{
	struct input_handler *handler = handle->handler;
	struct input_value *end = vals;
	struct input_value *v;

	for (v = vals; v != vals + count; v++) {
		if (handler->filter(handle, v->type, v->code, v->value))
			continue;
		if (end != v)
			*end = *v;
		end++;
	}

	return end - vals;
}

static unsigned int input_handle_events_null(struct input_handle *handle,
					     struct input_value *vals,
					     unsigned int count)
{
	return count;
}

static void input_handle_setup_event_handler(struct input_handle *handle)
{
	struct input_handler *handler = handle->handler;

	if (handler->filter)
		handle->handle_events = input_handle_events_filter;
	else if (handler->event)
		handle->handle_events = input_handle_events_default;
	else if (handler->events)
		handle->handle_events = handler->events;
	else
		handle->handle_events = input_handle_events_null;
}

int input_register_handle(struct input_handle *handle)
{
	struct input_handler *handler = handle->handler;
	struct input_dev *dev = handle->dev;

	INIT_LIST_HEAD(&handle->d_node);
	INIT_LIST_HEAD(&handle->h_node);
	input_handle_setup_event_handler(handle);
	// if (handler->filter)
	// 	list_add_rcu(&handle->d_node, &dev->h_list);
	// else
	// 	list_add_tail_rcu(&handle->d_node, &dev->h_list);
	// This port has no RCU input core; filters still go before normal handlers.
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Callback-driven slice has no concurrent input worker; do not block in HID callbacks.
	if (handler->filter)
		list_add(&handle->d_node, &dev->h_list);
	else
		list_add_tail(&handle->d_node, &dev->h_list);
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
	// list_add_tail_rcu(&handle->h_node, &handler->h_list);
	// Port list has no RCU variant.
	list_add_tail(&handle->h_node, &handler->h_list);
	if (handler->start)
		handler->start(handle);
	return 0;
}

void input_unregister_handle(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Callback-driven slice has no concurrent input worker; do not block in HID callbacks.
	list_del(&handle->d_node);
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
	// list_del_rcu(&handle->h_node);
	list_del(&handle->h_node); // Port list has no RCU variant.
}

int input_grab_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	// scoped_cond_guard(mutex_intr, return -EINTR, &dev->mutex) {
	// Port input core has no mutex.
	if (dev->grab)
		return -EBUSY;

	// rcu_assign_pointer(dev->grab, handle);
	// Port input core has no RCU.
	dev->grab = handle;

	return 0;
}

static void __input_release_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	struct input_handle *grabber;

	// grabber = rcu_dereference_protected(dev->grab,
	// 				    lockdep_is_held(&dev->mutex));
	// Port input core has no RCU/mutex lockdep.
	grabber = dev->grab;
	if (grabber == handle) {
		// rcu_assign_pointer(dev->grab, NULL);
		// Port input core has no RCU.
		dev->grab = NULL;
		// synchronize_rcu();
		// Port input core has no RCU; all delivery runs from the host task.

		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->open && handle->handler->start)
				handle->handler->start(handle);
	}
}

void input_release_device(struct input_handle *handle)
{
	// guard(mutex)(&dev->mutex);
	// Port input core has no mutex.
	__input_release_device(handle);
}

int input_open_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int ret = 0;

	// if (dev->going_away) {
	// 	retval = -ENODEV;
	// 	goto out;
	// }
	// Port input core has no mutex/out label; reject opens after disconnect starts.
	if (dev->going_away)
		return -ENODEV;

	handle->open++;

	if (handle->handler->passive_observer)
		return 0;

	if (dev->users++ || dev->inhibited) {
		/*
		 * Device is already opened and/or inhibited,
		 * so we can exit immediately and report success.
		 */
		return 0;
	}

	if (dev->open)
		ret = dev->open(dev);
	if (ret) {
		dev->users--;
		handle->open--;
		return ret;
	}

	// if (dev->poller)
	// 	input_dev_poller_start(dev->poller);
	// Port has no input poller core.
	return 0;
}

void input_close_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	// guard(mutex)(&dev->mutex);
	// Port input core has no mutex.
	__input_release_device(handle);

	if (!handle->handler->passive_observer) {
		if (!--dev->users && !dev->inhibited) {
			// if (dev->poller)
			// 	input_dev_poller_stop(dev->poller);
			// Port has no input poller core.
			if (dev->close)
				dev->close(dev);
		}
	}

	if (!--handle->open) {
		/*
		 * synchronize_rcu() makes sure that input_pass_values()
		 * completed and that no more input events are delivered
		 * through this handle
		 */
		// synchronize_rcu();
		// Port input core has no RCU.
		// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
		// xSemaphoreGive(dev->port_event_lock);
		// Callback-driven slice has no RCU wait point; do not block on close.
	}
}

int input_flush_device(struct input_handle *handle, struct file *file)
{
	struct input_dev *dev = handle->dev;

	// scoped_cond_guard(mutex_intr, return -EINTR, &dev->mutex) {
	// Port input core has no mutex.
	if (dev->flush)
		return dev->flush(dev, file);

	return 0;
}

static void input_alloc_absinfo(struct input_dev *dev)
{
	if (!dev->absinfo)
		dev->absinfo = kzalloc(sizeof(*dev->absinfo) * ABS_CNT, GFP_KERNEL);
}

void input_set_abs_params(struct input_dev *dev, unsigned int axis,
			  int min, int max, int fuzz, int flat)
{
	__set_bit(EV_ABS, dev->evbit);
	__set_bit(axis, dev->absbit);

	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	dev->absinfo[axis].minimum = min;
	dev->absinfo[axis].maximum = max;
	dev->absinfo[axis].fuzz = fuzz;
	dev->absinfo[axis].flat = flat;
}

void input_copy_abs(struct input_dev *dst, unsigned int dst_axis,
		    const struct input_dev *src, unsigned int src_axis)
{
	/* src must have EV_ABS and src_axis set */
	if (WARN_ON(!(test_bit(EV_ABS, src->evbit) &&
		      test_bit(src_axis, src->absbit))))
		return;

	/*
	 * input_alloc_absinfo() may have failed for the source. Our caller is
	 * expected to catch this when registering the input devices, which may
	 * happen after the input_copy_abs() call.
	 */
	if (!src->absinfo)
		return;

	input_set_capability(dst, EV_ABS, dst_axis);
	if (!dst->absinfo)
		return;

	dst->absinfo[dst_axis] = src->absinfo[src_axis];
}

void input_abs_set_res(struct input_dev *dev, unsigned int axis, int resolution)
{
	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	dev->absinfo[axis].resolution = resolution;
}

void input_set_events_per_packet(struct input_dev *dev, unsigned int n_events)
{
	dev->hint_events_per_packet = n_events;
}

void input_enable_softrepeat(struct input_dev *dev, int delay, int period)
{
	// set_bit(EV_REP, dev->evbit);
	// Upstream input_enable_softrepeat() does not declare EV_REP capability;
	// HID mapping sets EV_REP only for devices/usages that should repeat.
	dev->timer.function = input_repeat_key;
	dev->rep[REP_DELAY] = delay;
	dev->rep[REP_PERIOD] = period;
}

void input_set_capability(struct input_dev *dev, unsigned int type, unsigned int code)
{
	if (type < EV_CNT && input_max_code[type] && code > input_max_code[type]) {
		pr_err("%s: invalid code %u for type %u\n", __func__, code, type);
		// dump_stack();
		// Port has no stack unwinder.
		return;
	}

	switch (type) {
	case EV_KEY:
		__set_bit(code, dev->keybit);
		break;
	case EV_REL:
		__set_bit(code, dev->relbit);
		break;
	case EV_ABS:
		input_alloc_absinfo(dev);
		__set_bit(code, dev->absbit);
		break;
	case EV_MSC:
		__set_bit(code, dev->mscbit);
		break;
	case EV_LED:
		__set_bit(code, dev->ledbit);
		break;
	case EV_SND:
		__set_bit(code, dev->sndbit);
		break;
	case EV_SW:
		__set_bit(code, dev->swbit);
		break;
	case EV_FF:
		__set_bit(code, dev->ffbit);
		break;
	case EV_PWR:
		/* do nothing */
		break;
	default:
		pr_err("%s: unknown type %u (code %u)\n", __func__, type, code);
		// dump_stack();
		// Port has no stack unwinder.
		return;
	}

	__set_bit(type, dev->evbit);
}

static unsigned int input_estimate_events_per_packet(struct input_dev *dev)
{
	int mt_slots;
	int i;
	unsigned int events;

	if (dev->mt) {
		mt_slots = dev->mt->num_slots;
	} else if (test_bit(ABS_MT_TRACKING_ID, dev->absbit)) {
		mt_slots = dev->absinfo[ABS_MT_TRACKING_ID].maximum -
			   dev->absinfo[ABS_MT_TRACKING_ID].minimum + 1;
		mt_slots = clamp(mt_slots, 2, 32);
	} else if (test_bit(ABS_MT_POSITION_X, dev->absbit)) {
		mt_slots = 2;
	} else {
		mt_slots = 0;
	}

	events = mt_slots + 1; /* count SYN_MT_REPORT and SYN_REPORT */

	if (test_bit(EV_ABS, dev->evbit))
		for_each_set_bit(i, dev->absbit, ABS_CNT)
			events += input_is_mt_axis(i) ? mt_slots : 1;

	if (test_bit(EV_REL, dev->evbit))
		for_each_set_bit(i, dev->relbit, REL_CNT)
			events++;

	/* Make room for KEY and MSC events */
	events += 7;

	return events;
}

static int input_device_tune_vals(struct input_dev *dev)
{
	struct input_value *vals;
	unsigned int packet_size;
	unsigned int max_vals;

	packet_size = input_estimate_events_per_packet(dev);
	if (dev->hint_events_per_packet < packet_size)
		dev->hint_events_per_packet = packet_size;

	max_vals = dev->hint_events_per_packet + 2;
	if (dev->max_vals >= max_vals)
		return 0;

	vals = kzalloc_objs(*vals, max_vals);
	if (!vals)
		return -ENOMEM;

	dev->max_vals = max_vals;
	swap(dev->vals, vals);
	/* Because of swap() above, this frees the old vals memory */
	kfree(vals);

	return 0;
}

int input_scancode_to_scalar(const struct input_keymap_entry *ke, unsigned int *scancode)
{
	switch (ke->len) {
	case 1:
		*scancode = *((u8 *)ke->scancode);
		break;

	case 2:
		*scancode = *((u16 *)ke->scancode);
		break;

	case 4:
		*scancode = *((u32 *)ke->scancode);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Those routines handle the default case where no [gs]etkeycode() is
 * defined. In this case, an array indexed by the scancode is used.
 */

static unsigned int input_fetch_keycode(struct input_dev *dev,
					unsigned int index)
{
	switch (dev->keycodesize) {
	case 1:
		return ((u8 *)dev->keycode)[index];

	case 2:
		return ((u16 *)dev->keycode)[index];

	default:
		return ((u32 *)dev->keycode)[index];
	}
}

static int input_default_getkeycode(struct input_dev *dev,
				    struct input_keymap_entry *ke)
{
	unsigned int index;
	int error;

	if (!dev->keycodesize)
		return -EINVAL;

	if (ke->flags & INPUT_KEYMAP_BY_INDEX)
		index = ke->index;
	else {
		error = input_scancode_to_scalar(ke, &index);
		if (error)
			return error;
	}

	if (index >= dev->keycodemax)
		return -EINVAL;

	ke->keycode = input_fetch_keycode(dev, index);
	ke->index = index;
	ke->len = sizeof(index);
	memcpy(ke->scancode, &index, sizeof(index));

	return 0;
}

int input_default_setkeycode(struct input_dev *dev,
			     const struct input_keymap_entry *ke,
			     unsigned int *old_keycode)
{
	unsigned int index;
	int error;
	int i;

	// lockdep_assert_held(&dev->event_lock);
	// Port input core has no event_lock.

	if (!dev->keycodesize)
		return -EINVAL;

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
	} else {
		error = input_scancode_to_scalar(ke, &index);
		if (error)
			return error;
	}

	if (index >= dev->keycodemax)
		return -EINVAL;

	if (dev->keycodesize < sizeof(ke->keycode) &&
			(ke->keycode >> (dev->keycodesize * 8)))
		return -EINVAL;

	switch (dev->keycodesize) {
		case 1: {
			u8 *k = (u8 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
		case 2: {
			u16 *k = (u16 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
		default: {
			u32 *k = (u32 *)dev->keycode;
			*old_keycode = k[index];
			k[index] = ke->keycode;
			break;
		}
	}

	if (*old_keycode <= KEY_MAX) {
		__clear_bit(*old_keycode, dev->keybit);
		for (i = 0; i < dev->keycodemax; i++) {
			if (input_fetch_keycode(dev, i) == *old_keycode) {
				__set_bit(*old_keycode, dev->keybit);
				/* Setting the bit twice is useless, so break */
				break;
			}
		}
	}

	__set_bit(ke->keycode, dev->keybit);
	return 0;
}

#define INPUT_IGNORE_EVENT	0
#define INPUT_PASS_TO_HANDLERS	1
#define INPUT_PASS_TO_DEVICE	2
#define INPUT_SLOT		4
#define INPUT_FLUSH		8
#define INPUT_PASS_TO_ALL	(INPUT_PASS_TO_HANDLERS | INPUT_PASS_TO_DEVICE)

static int input_defuzz_abs_event(int value, int old_val, int fuzz)
{
	if (fuzz) {
		if (value > old_val - fuzz / 2 &&
		    value < old_val + fuzz / 2)
			return old_val;

		if (value > old_val - fuzz &&
		    value < old_val + fuzz)
			return (old_val * 3 + value) / 4;

		if (value > old_val - fuzz * 2 &&
		    value < old_val + fuzz * 2)
			return (old_val + value) / 2;
	}

	return value;
}

static int input_handle_abs_event(struct input_dev *dev,
				  unsigned int code, int *pval)
{
	struct input_mt *mt = dev->mt;
	bool is_new_slot = false;
	bool is_mt_event;
	__s32 *pold;

	if (code == ABS_MT_SLOT) {
		/*
		 * "Stage" the event; we'll flush it later, when we
		 * get actual touch data.
		 */
		if (mt && *pval >= 0 && *pval < mt->num_slots)
			mt->slot = *pval;

		return INPUT_IGNORE_EVENT;
	}

	is_mt_event = input_is_mt_value(code);

	if (!is_mt_event) {
		pold = &dev->absinfo[code].value;
	} else if (mt) {
		// pold = &mt->slots[mt->slot].abs[code - ABS_MT_FIRST];
		pold = (__s32 *)&mt->slots[mt->slot].abs[code - ABS_MT_FIRST]; // Port __s32 and int are distinct typedefs.
		is_new_slot = mt->slot != dev->absinfo[ABS_MT_SLOT].value;
	} else {
		/*
		 * Bypass filtering for multi-touch events when
		 * not employing slots.
		 */
		pold = NULL;
	}

	if (pold) {
		*pval = input_defuzz_abs_event(*pval, *pold,
					       dev->absinfo[code].fuzz);
		if (*pold == *pval)
			return INPUT_IGNORE_EVENT;

		*pold = *pval;
	}

	/* Flush pending "slot" event */
	if (is_new_slot) {
		dev->absinfo[ABS_MT_SLOT].value = mt->slot;
		return INPUT_PASS_TO_HANDLERS | INPUT_SLOT;
	}

	return INPUT_PASS_TO_HANDLERS;
}

static void input_start_autorepeat(struct input_dev *dev, int code)
{
	if (test_bit(EV_REP, dev->evbit) &&
	    dev->rep[REP_PERIOD] && dev->rep[REP_DELAY] &&
	    dev->timer.function) {
		dev->repeat_key = code;
		mod_timer(&dev->timer,
			  jiffies + msecs_to_jiffies(dev->rep[REP_DELAY]));
	}
}

static void input_stop_autorepeat(struct input_dev *dev)
{
	timer_delete(&dev->timer);
}

static void input_pass_values(struct input_dev *dev,
			      struct input_value *vals, unsigned int count)
{
	struct input_handle *handle;
	struct input_value *v;

	// lockdep_assert_held(&dev->event_lock);
	// scoped_guard(rcu) { ... }
	// Port input core has no event_lock/RCU; keep grab-first handler order.
	if (dev->grab) {
		if (dev->grab->open)
			count = dev->grab->handle_events(dev->grab, vals, count);
	} else {
		list_for_each_entry(handle, &dev->h_list, d_node) {
			if (handle->open) {
				count = handle->handle_events(handle, vals, count);
				if (!count)
					break;
			}
		}
	}

	/* trigger auto repeat for key events */
	if (test_bit(EV_REP, dev->evbit) && test_bit(EV_KEY, dev->evbit)) {
		for (v = vals; v != vals + count; v++) {
			if (v->type == EV_KEY && v->value != 2) {
				if (v->value)
					input_start_autorepeat(dev, v->code);
				else
					input_stop_autorepeat(dev);
			}
		}
	}
}

static int input_get_disposition(struct input_dev *dev,
				 unsigned int type, unsigned int code, int *pval)
{
	int disposition = INPUT_IGNORE_EVENT;
	int value = *pval;

	/* filter-out events from inhibited devices */
	if (dev->inhibited)
		return INPUT_IGNORE_EVENT;

	switch (type) {
	case EV_SYN:
		switch (code) {
		case SYN_CONFIG:
			disposition = INPUT_PASS_TO_ALL;
			break;
		case SYN_REPORT:
			disposition = INPUT_PASS_TO_HANDLERS | INPUT_FLUSH;
			break;
		case SYN_MT_REPORT:
			disposition = INPUT_PASS_TO_HANDLERS;
			break;
		}
		break;
	case EV_KEY:
		if (is_event_supported(code, dev->keybit, KEY_MAX)) {
			if (value == 2) {
				disposition = INPUT_PASS_TO_HANDLERS;
				break;
			}
			if (!!test_bit(code, dev->key) != !!value) {
				if (value)
					set_bit(code, dev->key);
				else
					clear_bit(code, dev->key);
				disposition = INPUT_PASS_TO_HANDLERS;
			}
		}
		break;
	case EV_REL:
		if (is_event_supported(code, dev->relbit, REL_MAX) && value)
			disposition = INPUT_PASS_TO_HANDLERS;
		break;
	case EV_ABS:
		if (is_event_supported(code, dev->absbit, ABS_MAX))
			disposition = input_handle_abs_event(dev, code, &value);
		break;
	case EV_MSC:
		if (is_event_supported(code, dev->mscbit, MSC_MAX))
			disposition = INPUT_PASS_TO_ALL;
		break;
	case EV_LED:
		if (is_event_supported(code, dev->ledbit, LED_MAX) &&
		    !!test_bit(code, dev->led) != !!value) {
			if (value)
				set_bit(code, dev->led);
			else
				clear_bit(code, dev->led);
			disposition = INPUT_PASS_TO_ALL;
		}
		break;
	case EV_SND:
		if (is_event_supported(code, dev->sndbit, SND_MAX)) {
			if (!!test_bit(code, dev->snd) != !!value) {
				if (value)
					set_bit(code, dev->snd);
				else
					clear_bit(code, dev->snd);
			}
			disposition = INPUT_PASS_TO_ALL;
		}
		break;
	case EV_SW:
		if (is_event_supported(code, dev->swbit, SW_MAX) &&
		    !!test_bit(code, dev->sw) != !!value) {
			if (value)
				set_bit(code, dev->sw);
			else
				clear_bit(code, dev->sw);
			disposition = INPUT_PASS_TO_HANDLERS;
		}
		break;
	case EV_REP:
		if (code <= REP_MAX && value >= 0 && dev->rep[code] != value) {
			dev->rep[code] = value;
			disposition = INPUT_PASS_TO_ALL;
		}
		break;
	case EV_FF:
		if (value >= 0)
			disposition = INPUT_PASS_TO_ALL;
		break;
	case EV_PWR:
		disposition = INPUT_PASS_TO_ALL;
		break;
	default:
		break;
	}

	*pval = value;
	return disposition;
}

static void input_event_dispose(struct input_dev *dev, int disposition,
				unsigned int type, unsigned int code, int value)
{
	if ((disposition & INPUT_PASS_TO_DEVICE) && dev->event)
		dev->event(dev, type, code, value);

	if (disposition & INPUT_PASS_TO_HANDLERS) {
		struct input_value *v;

		if (disposition & INPUT_SLOT) {
			v = &dev->vals[dev->num_vals++];
			v->type = EV_ABS;
			v->code = ABS_MT_SLOT;
			v->value = dev->mt->slot;
		}

		v = &dev->vals[dev->num_vals++];
		v->type = type;
		v->code = code;
		v->value = value;
	}

	if (disposition & INPUT_FLUSH) {
		if (dev->num_vals >= 2)
			input_pass_values(dev, dev->vals, dev->num_vals);
		dev->num_vals = 0;

		/*
		 * Reset the timestamp on flush so we won't end up
		 * with a stale one. Note we only need to reset the
		 * monolithic one as we use its presence when deciding
		 * whether to generate a synthetic timestamp.
		 */
		// dev->timestamp[INPUT_CLK_MONO] = ktime_set(0, 0);
		dev->timestamp[INPUT_CLK_MONO] = 0; // Port ktime_t is scalar.
	} else if (dev->num_vals >= dev->max_vals - 2) {
		dev->vals[dev->num_vals++] = input_value_sync;
		input_pass_values(dev, dev->vals, dev->num_vals);
		dev->num_vals = 0;
	}
}

static void input_handle_event_locked(struct input_dev *dev,
				      unsigned int type, unsigned int code, int value)
{
	int disposition;

	disposition = input_get_disposition(dev, type, code, &value);
	// Upstream entropy side effect:
	// if (type != EV_SYN)
	// 	add_input_randomness(type, code, value);
	// Firmware has no Linux entropy pool; keep only the HID/input event side effect.
	if (disposition != INPUT_IGNORE_EVENT)
		input_event_dispose(dev, disposition, type, code, value);
}

void input_handle_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value)
{
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Callback-driven slice has no concurrent input worker; do not block in report path.
	input_handle_event_locked(dev, type, code, value);
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
}

void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	// guard(spinlock_irqsave)(&dev->event_lock);
	// Port input core has no event_lock; boundary proxy synchronization is tracked separately.
	if (is_event_supported(type, dev->evbit, EV_MAX))
		input_handle_event(dev, type, code, value);
}

void input_inject_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct input_dev *dev = handle->dev;

	if (!is_event_supported(type, dev->evbit, EV_MAX))
		return;

	// grab = rcu_dereference(dev->grab);
	// if (!grab || grab == handle)
	// 	input_handle_event(dev, type, code, value);
	// Port input core has no RCU; use the same grab rule directly.
	if (dev->grab && dev->grab != handle)
		return;

	input_handle_event(dev, type, code, value);
}

void input_sync(struct input_dev *dev)
{
	// Linux include/linux/input.h: input_sync() emits SYN_REPORT.
	input_event(dev, EV_SYN, SYN_REPORT, 0);
}

void input_set_timestamp(struct input_dev *dev, ktime_t timestamp)
{
	dev->timestamp[INPUT_CLK_MONO] = timestamp;
	// dev->timestamp[INPUT_CLK_REAL] = ktime_mono_to_real(timestamp);
	// dev->timestamp[INPUT_CLK_BOOT] = ktime_mono_to_any(timestamp,
	// 						  TK_OFFS_BOOT);
	// Port ktime_t is scalar milliseconds; no real/boot clock conversion exists.
	dev->timestamp[INPUT_CLK_REAL] = timestamp;
	dev->timestamp[INPUT_CLK_BOOT] = timestamp;
}

ktime_t *input_get_timestamp(struct input_dev *dev)
{
	const ktime_t invalid_timestamp = 0;

	if (dev->timestamp[INPUT_CLK_MONO] == invalid_timestamp)
		// input_set_timestamp(dev, ktime_get());
		input_set_timestamp(dev, ktime_get_coarse()); // Port ktime_t is scalar milliseconds.

	return dev->timestamp;
}

#define INPUT_DO_TOGGLE(dev, type, bits, on)				\
	do {								\
		int i;							\
		bool active;						\
									\
		if (!test_bit(EV_##type, dev->evbit))			\
			break;						\
									\
		for_each_set_bit(i, dev->bits##bit, type##_CNT) {	\
			active = test_bit(i, dev->bits);		\
			if (!active && !on)				\
				continue;				\
									\
			dev->event(dev, EV_##type, i, on ? active : 0);	\
		}							\
	} while (0)

static void input_dev_toggle(struct input_dev *dev, bool activate)
{
	if (!dev->event)
		return;

	INPUT_DO_TOGGLE(dev, LED, led, activate);
	INPUT_DO_TOGGLE(dev, SND, snd, activate);

	if (activate && test_bit(EV_REP, dev->evbit)) {
		dev->event(dev, EV_REP, REP_PERIOD, dev->rep[REP_PERIOD]);
		dev->event(dev, EV_REP, REP_DELAY, dev->rep[REP_DELAY]);
	}
}

/*
 * Simulate keyup events for all keys that are marked as pressed.
 * The function must be called with dev->event_lock held.
 */
static bool input_dev_release_keys(struct input_dev *dev)
{
	bool need_sync = false;
	int code;

	// lockdep_assert_held(&dev->event_lock);
	// Port input core has no event_lock.
	if (is_event_supported(EV_KEY, dev->evbit, EV_MAX)) {
		for_each_set_bit(code, dev->key, KEY_CNT) {
			input_handle_event_locked(dev, EV_KEY, code, 0);
			need_sync = true;
		}
	}

	return need_sync;
}

/*
 * Prepare device for unregistering
 */
static void input_disconnect_device(struct input_dev *dev)
{
	struct input_handle *handle;

	/*
	 * Mark device as going away. Note that we take dev->mutex here
	 * not to protect access to dev->going_away but rather to ensure
	 * that there are no threads in the middle of input_open_device()
	 */
	// scoped_guard(mutex, &dev->mutex)
	// Port input core has no mutex guard.
	dev->going_away = true;

	// guard(spinlock_irq)(&dev->event_lock);
	// Port input core has no event_lock.
	/*
	 * Simulate keyup events for all pressed keys so that handlers
	 * are not left with "stuck" keys. The driver may continue
	 * generate events even after we done here but they will not
	 * reach any handlers.
	 */
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Unregister runs from the same host callback/task slice; do not block.
	if (input_dev_release_keys(dev)) {
		input_handle_event_locked(dev, EV_SYN, SYN_REPORT, 1);
	}
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.

	list_for_each_entry(handle, &dev->h_list, d_node)
		while (handle->open) {
			// handle->open = 0;
			// Port input handles are opened by the always-on firmware consumer,
			// so unregister must run the normal close path before handles are freed.
			input_close_device(handle);
		}
}

void input_reset_device(struct input_dev *dev)
{
	// guard(mutex)(&dev->mutex);
	// guard(spinlock_irqsave)(&dev->event_lock);
	// Port input core has no mutex/event_lock guards.
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Reset is synchronous in this slice; no blocking event lock.
	input_dev_toggle(dev, true);
	if (input_dev_release_keys(dev)) {
		input_handle_event_locked(dev, EV_SYN, SYN_REPORT, 1);
	}
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
}

/*
 * Generate software autorepeat event. Note that we take
 * dev->event_lock here to avoid racing with input_event
 * which may cause keys get "stuck".
 */
static void input_repeat_key(struct timer_list *t)
{
	struct input_dev *dev = timer_container_of(dev, t, timer);

	// guard(spinlock_irqsave)(&dev->event_lock);
	// Port input core has no event_lock; timer callbacks and input processing run
	// from the HID driver task's Linux workqueue/timer pump.
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Software repeat worker is not active in the callback-driven slice.
	if (!dev->inhibited &&
	    test_bit(dev->repeat_key, dev->key) &&
	    is_event_supported(dev->repeat_key, dev->keybit, KEY_MAX)) {

		// input_set_timestamp(dev, ktime_get());
		input_set_timestamp(dev, ktime_get_coarse()); // Port ktime_t is scalar milliseconds.
		input_handle_event_locked(dev, EV_KEY, dev->repeat_key, 2);
		input_handle_event_locked(dev, EV_SYN, SYN_REPORT, 1);

		if (dev->rep[REP_PERIOD])
			mod_timer(&dev->timer, jiffies +
					msecs_to_jiffies(dev->rep[REP_PERIOD]));
	}
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
}

struct input_dev *input_find_device_by_name(const char *name)
{
	struct input_dev *dev;

	list_for_each_entry(dev, &input_dev_list, node)
		if (!strcmp(dev->name, name))
			return dev;

	return NULL;
}

int input_for_each_device(int (*fn)(struct input_dev *dev, void *data), void *data)
{
	struct input_dev *dev;

	list_for_each_entry(dev, &input_dev_list, node) {
		int ret = fn(dev, data);

		if (ret)
			return ret;
	}

	return 0;
}
