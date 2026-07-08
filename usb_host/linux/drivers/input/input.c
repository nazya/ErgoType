// SPDX-License-Identifier: GPL-2.0-only
/*
 * The input core
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * Reduced Linux input core slice. Upstream drivers/input/input.c also owns
 * char devices, procfs/sysfs plumbing, IDA allocation, RCU locking, poller
 * support, and more lifecycle code. This port keeps the in-memory handler and
 * event path needed by hid-input and the firmware event boundary.
 */

// #define pr_fmt(fmt) KBUILD_BASENAME ": " fmt
//
// #include <linux/export.h>
// #include <linux/init.h>
// #include <linux/types.h>
// #include <linux/idr.h>
// #include <linux/input/mt.h>
// #include <linux/module.h>
// #include <linux/slab.h>
// #include <linux/random.h>
// #include <linux/major.h>
// #include <linux/proc_fs.h>
// #include <linux/sched.h>
// #include <linux/seq_file.h>
// #include <linux/pm.h>
// #include <linux/poll.h>
// #include <linux/device.h>
// #include <linux/kstrtox.h>
// #include <linux/mutex.h>
// #include <linux/rcupdate.h>
// #include "input-compat.h"
// #include "input-core-private.h"
// #include "input-poller.h"
// Firmware builds this slice through local compat headers instead.
#include "../../include/linux/hid.h"
#include "../../include/linux/input.h"
#include "../../include/linux/input/mt.h"

// MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
// MODULE_DESCRIPTION("Input core");
// MODULE_LICENSE("GPL");
//
// #define INPUT_MAX_CHAR_DEVICES		1024
// #define INPUT_FIRST_DYNAMIC_DEV		256
// static DEFINE_IDA(input_ida);
// Firmware has no Linux module metadata, char devices, or IDA minor allocator.

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

/*
 * input_mutex protects access to both input_dev_list and input_handler_list.
 * This also causes input_[un]register_device and input_[un]register_handler
 * be mutually exclusive which simplifies locking in drivers implementing
 * input handlers.
 */
// static DEFINE_MUTEX(input_mutex);
// Firmware registers input devices/handlers from the host task and has no
// blocking input_mutex in the callback-driven path.

// Upstream Linux allocates inputN via IDA/device model. Firmware keeps only a
// monotonic in-memory proxy id for code that needs stable input_dev identity.
static unsigned int input_next_proxy_id;

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
static void input_event_dispose(struct input_dev *dev, int disposition,
				unsigned int type, unsigned int code, int value);
static void input_repeat_key(struct timer_list *t);

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

/*
 * Pass values first through all filters and then, if event has not been
 * filtered out, through all open handles. This order is achieved by placing
 * filters at the head of the list of handles attached to the device, and
 * placing regular handles at the tail of the list.
 *
 * This function is called with dev->event_lock held and interrupts disabled.
 */
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

#define INPUT_IGNORE_EVENT	0
#define INPUT_PASS_TO_HANDLERS	1
#define INPUT_PASS_TO_DEVICE	2
#define INPUT_SLOT		4
#define INPUT_FLUSH		8
#define INPUT_PASS_TO_ALL	(INPUT_PASS_TO_HANDLERS | INPUT_PASS_TO_DEVICE)

static int input_handle_abs_event(struct input_dev *dev,
				  unsigned int code, int *pval)
{
	struct input_mt *mt = dev->mt;
	bool is_new_slot = false;
	bool is_mt_event;
	// int *pold;
	// Port input_mt slot storage uses __s32 while hid_compat keeps int distinct.
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

			/* auto-repeat bypasses state updates */
			if (value == 2) {
				disposition = INPUT_PASS_TO_HANDLERS;
				break;
			}

			if (!!test_bit(code, dev->key) != !!value) {

				// __change_bit(code, dev->key);
				// hid_compat has set/clear helpers but no __change_bit.
				if (value)
					set_bit(code, dev->key);
				else
					clear_bit(code, dev->key);
				disposition = INPUT_PASS_TO_HANDLERS;
			}
		}
		break;

	case EV_SW:
		if (is_event_supported(code, dev->swbit, SW_MAX) &&
		    !!test_bit(code, dev->sw) != !!value) {

			// __change_bit(code, dev->sw);
			// hid_compat has set/clear helpers but no __change_bit.
			if (value)
				set_bit(code, dev->sw);
			else
				clear_bit(code, dev->sw);
			disposition = INPUT_PASS_TO_HANDLERS;
		}
		break;

	case EV_ABS:
		if (is_event_supported(code, dev->absbit, ABS_MAX))
			disposition = input_handle_abs_event(dev, code, &value);

		break;

	case EV_REL:
		if (is_event_supported(code, dev->relbit, REL_MAX) && value)
			disposition = INPUT_PASS_TO_HANDLERS;

		break;

	case EV_MSC:
		if (is_event_supported(code, dev->mscbit, MSC_MAX))
			disposition = INPUT_PASS_TO_ALL;

		break;

	case EV_LED:
		if (is_event_supported(code, dev->ledbit, LED_MAX) &&
		    !!test_bit(code, dev->led) != !!value) {

			// __change_bit(code, dev->led);
			// hid_compat has set/clear helpers but no __change_bit.
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
				// __change_bit(code, dev->snd);
				// hid_compat has set/clear helpers but no __change_bit.
				if (value)
					set_bit(code, dev->snd);
				else
					clear_bit(code, dev->snd);
			}
			disposition = INPUT_PASS_TO_ALL;
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

void input_handle_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value)
{
	int disposition;

	// lockdep_assert_held(&dev->event_lock);
	// Callback-driven slice has no active event_lock; do not block in report path.
	disposition = input_get_disposition(dev, type, code, &value);
	if (disposition != INPUT_IGNORE_EVENT) {
		// if (type != EV_SYN)
		// 	add_input_randomness(type, code, value);
		// Firmware has no Linux entropy pool; keep only the HID/input event side effect.
		input_event_dispose(dev, disposition, type, code, value);
	}
}

/**
 * input_event() - report new input event
 * @dev: device that generated the event
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * This function should be used by drivers implementing various input
 * devices to report input events. See also input_inject_event().
 *
 * NOTE: input_event() may be safely used right after input device was
 * allocated with input_allocate_device(), even before it is registered
 * with input_register_device(), but the event will not reach any of the
 * input handlers. Such early invocation of input_event() may be used
 * to 'seed' initial state of a switch or initial position of absolute
 * axis, etc.
 */
void input_event(struct input_dev *dev,
		 unsigned int type, unsigned int code, int value)
{
	if (is_event_supported(type, dev->evbit, EV_MAX)) {
		// guard(spinlock_irqsave)(&dev->event_lock);
		// Port input core has no event_lock; boundary proxy synchronization is tracked separately.
		input_handle_event(dev, type, code, value);
	}
}
// EXPORT_SYMBOL(input_event);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_inject_event() - send input event from input handler
 * @handle: input handle to send event through
 * @type: type of the event
 * @code: event code
 * @value: value of the event
 *
 * Similar to input_event() but will ignore event if device is
 * "grabbed" and handle injecting event is not the one that owns
 * the device.
 */
void input_inject_event(struct input_handle *handle,
			unsigned int type, unsigned int code, int value)
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
// EXPORT_SYMBOL(input_inject_event);
// Firmware links this file directly and has no Linux module symbol export.

void input_sync(struct input_dev *dev)
{
	// Linux include/linux/input.h: input_sync() emits SYN_REPORT.
	input_event(dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * input_alloc_absinfo - allocates array of input_absinfo structs
 * @dev: the input device emitting absolute events
 *
 * If the absinfo struct the caller asked for is already allocated, this
 * functions will not do anything.
 */
void input_alloc_absinfo(struct input_dev *dev)
{
	if (dev->absinfo)
		return;

	dev->absinfo = kzalloc_objs(*dev->absinfo, ABS_CNT);
	// if (!dev->absinfo) {
	// 	dev_err(dev->dev.parent ?: &dev->dev,
	// 		"%s: unable to allocate memory\n", __func__);
	// }
	// Firmware reports ABS allocation failure when input_register_device()
	// refuses ABS devices without absinfo.
}
// EXPORT_SYMBOL(input_alloc_absinfo);
// Firmware links this file directly and has no Linux module symbol export.

void input_set_abs_params(struct input_dev *dev, unsigned int axis,
			  int min, int max, int fuzz, int flat)
{
	struct input_absinfo *absinfo;

	__set_bit(EV_ABS, dev->evbit);
	__set_bit(axis, dev->absbit);

	input_alloc_absinfo(dev);
	if (!dev->absinfo)
		return;

	absinfo = &dev->absinfo[axis];
	absinfo->minimum = min;
	absinfo->maximum = max;
	absinfo->fuzz = fuzz;
	absinfo->flat = flat;
}
// EXPORT_SYMBOL(input_set_abs_params);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_copy_abs - Copy absinfo from one input_dev to another
 * @dst: Destination input device to copy the abs settings to
 * @dst_axis: ABS_* value selecting the destination axis
 * @src: Source input device to copy the abs settings from
 * @src_axis: ABS_* value selecting the source axis
 *
 * Set absinfo for the selected destination axis by copying it from
 * the specified source input device's source axis.
 * This is useful to e.g. setup a pen/stylus input-device for combined
 * touchscreen/pen hardware where the pen uses the same coordinates as
 * the touchscreen.
 */
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
// EXPORT_SYMBOL(input_copy_abs);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_grab_device - grabs device for exclusive use
 * @handle: input handle that wants to own the device
 *
 * When a device is grabbed by an input handle all events generated by
 * the device are delivered only to this handle. Also events injected
 * by other input handles are ignored while device is grabbed.
 */
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
	// }

	return 0;
}
// EXPORT_SYMBOL(input_grab_device);
// Firmware links this file directly and has no Linux module symbol export.

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
		// /* Make sure input_pass_values() notices that grab is gone */
		// synchronize_rcu();
		// Port input core has no RCU; all delivery runs from the host task.

		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->open && handle->handler->start)
				handle->handler->start(handle);
	}
}

/**
 * input_release_device - release previously grabbed device
 * @handle: input handle that owns the device
 *
 * Releases previously grabbed device so that other input handles can
 * start receiving input events. Upon release all handlers attached
 * to the device have their start() method called so they have a change
 * to synchronize device state with the rest of the system.
 */
void input_release_device(struct input_handle *handle)
{
	// struct input_dev *dev = handle->dev;
	// guard(mutex)(&dev->mutex);
	// Port input core has no mutex.
	__input_release_device(handle);
}
// EXPORT_SYMBOL(input_release_device);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_open_device - open input device
 * @handle: handle through which device is being accessed
 *
 * This function should be called by input handlers when they
 * want to start receive events from given input device.
 */
int input_open_device(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;
	int error;

	// scoped_cond_guard(mutex_intr, return -EINTR, &dev->mutex) {
	// Port input core has no mutex; reject opens after disconnect starts.
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

	if (dev->open) {
		error = dev->open(dev);
		if (error) {
			dev->users--;
			handle->open--;
			/*
			 * Make sure we are not delivering any more
			 * events through this handle.
			 */
			// synchronize_rcu();
			// Port input core has no RCU; open failure returns directly.
			return error;
		}
	}

	// if (dev->poller)
	// 	input_dev_poller_start(dev->poller);
	// Port has no input poller core.
	// }
	return 0;
}
// EXPORT_SYMBOL(input_open_device);
// Firmware links this file directly and has no Linux module symbol export.

int input_flush_device(struct input_handle *handle, struct file *file)
{
	struct input_dev *dev = handle->dev;

	// scoped_cond_guard(mutex_intr, return -EINTR, &dev->mutex) {
	// Port input core has no mutex.
	if (dev->flush)
		return dev->flush(dev, file);
	// }

	return 0;
}
// EXPORT_SYMBOL(input_flush_device);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_close_device - close input device
 * @handle: handle through which device is being accessed
 *
 * This function should be called by input handlers when they
 * want to stop receive events from given input device.
 */
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
		// Callback-driven slice has no RCU wait point; do not block on close.
	}
}
// EXPORT_SYMBOL(input_close_device);
// Firmware links this file directly and has no Linux module symbol export.

static bool input_dev_release_keys(struct input_dev *dev)
{
	bool need_sync = false;
	int code;

	// lockdep_assert_held(&dev->event_lock);
	// Port input core has no event_lock.
	if (is_event_supported(EV_KEY, dev->evbit, EV_MAX)) {
		for_each_set_bit(code, dev->key, KEY_CNT) {
			input_handle_event(dev, EV_KEY, code, 0);
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
	// Unregister runs from the same host callback/task slice; do not block.
	if (input_dev_release_keys(dev)) {
		input_handle_event(dev, EV_SYN, SYN_REPORT, 1);
	}
	// See nonblocking callback-driven note above.

	list_for_each_entry(handle, &dev->h_list, d_node)
		while (handle->open) {
			// handle->open = 0;
			// Port input handles are opened by the always-on firmware consumer,
			// so unregister must run the normal close path before handles are freed.
			input_close_device(handle);
		}
}

/**
 * input_scancode_to_scalar() - converts scancode in &struct input_keymap_entry
 * @ke: keymap entry containing scancode to be converted.
 * @scancode: pointer to the location where converted scancode should
 *	be stored.
 *
 * This function is used to convert scancode stored in &struct keymap_entry
 * into scalar form understood by legacy keymap handling methods. These
 * methods expect scancodes to be represented as 'unsigned int'.
 */
int input_scancode_to_scalar(const struct input_keymap_entry *ke,
			     unsigned int *scancode)
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
// EXPORT_SYMBOL(input_scancode_to_scalar);
// Firmware links this file directly and has no Linux module symbol export.

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

/**
 * input_default_setkeycode - default setkeycode method
 * @dev: input device which keymap is being updated.
 * @ke: new keymap entry.
 * @old_keycode: pointer to the location where old keycode should be stored.
 *
 * This function is the default implementation of &input_dev.setkeycode()
 * method. It is typically used when a driver does not provide its own
 * implementation, but it is also exported so drivers can extend it.
 *
 * The function must be called with &input_dev.event_lock held.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
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
// EXPORT_SYMBOL(input_default_setkeycode);
// Firmware links this file directly and has no Linux module symbol export.

// /**
//  * input_get_keycode - retrieve keycode currently mapped to a given scancode
//  * @dev: input device which keymap is being queried
//  * @ke: keymap entry
//  *
//  * This function should be called by anyone interested in retrieving current
//  * keymap. Presently evdev handlers use it.
//  */
// int input_get_keycode(struct input_dev *dev, struct input_keymap_entry *ke)
// {
// 	guard(spinlock_irqsave)(&dev->event_lock);
//
// 	return dev->getkeycode(dev, ke);
// }
// EXPORT_SYMBOL(input_get_keycode);
//
// /**
//  * input_set_keycode - attribute a keycode to a given scancode
//  * @dev: input device which keymap is being updated
//  * @ke: new keymap entry
//  *
//  * This function should be called by anyone needing to update current
//  * keymap. Presently keyboard and evdev handlers use it.
//  */
// int input_set_keycode(struct input_dev *dev,
// 		      const struct input_keymap_entry *ke)
// {
// 	unsigned int old_keycode;
// 	int error;
//
// 	if (ke->keycode > KEY_MAX)
// 		return -EINVAL;
//
// 	guard(spinlock_irqsave)(&dev->event_lock);
//
// 	error = dev->setkeycode(dev, ke, &old_keycode);
// 	if (error)
// 		return error;
//
// 	/* Make sure KEY_RESERVED did not get enabled. */
// 	__clear_bit(KEY_RESERVED, dev->keybit);
//
// 	/*
// 	 * Simulate keyup event if keycode is not present
// 	 * in the keymap anymore
// 	 */
// 	if (old_keycode > KEY_MAX) {
// 		dev_warn(dev->dev.parent ?: &dev->dev,
// 			 "%s: got too big old keycode %#x\n",
// 			 __func__, old_keycode);
// 	} else if (test_bit(EV_KEY, dev->evbit) &&
// 		   !is_event_supported(old_keycode, dev->keybit, KEY_MAX) &&
// 		   __test_and_clear_bit(old_keycode, dev->key)) {
// 		/*
// 		 * We have to use input_event_dispose() here directly instead
// 		 * of input_handle_event() because the key we want to release
// 		 * here is considered no longer supported by the device and
// 		 * input_handle_event() will ignore it.
// 		 */
// 		input_event_dispose(dev, INPUT_PASS_TO_HANDLERS,
// 				    EV_KEY, old_keycode, 0);
// 		input_event_dispose(dev, INPUT_PASS_TO_HANDLERS | INPUT_FLUSH,
// 				    EV_SYN, SYN_REPORT, 1);
// 	}
//
// 	return 0;
// }
// EXPORT_SYMBOL(input_set_keycode);
// Firmware has no evdev userspace keymap ioctl path or event_lock yet.

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
// EXPORT_SYMBOL(input_match_device_id);
// Firmware links this file directly and has no Linux module symbol export.

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

/*
 * Upstream Linux has the procfs and sysfs presentation layer here:
 * input_wakeup_procfs_readers(), /proc/bus/input/devices seq_file handlers,
 * modalias generation, input_dev attribute groups, input_dev_release(), and
 * input_dev_uevent(). Firmware has no Linux userspace file descriptors,
 * procfs, sysfs, kobject uevents, or input class device presentation. The
 * in-memory event and handler path continues below; discovery/presentation is
 * handled by firmware glue outside this upstream-derived file.
 */

/**
 * input_set_timestamp - set timestamp for input events
 * @dev: input device to set timestamp for
 * @timestamp: the time at which the event has occurred
 *   in CLOCK_MONOTONIC
 *
 * This function is intended to provide to the input system a more
 * accurate time of when an event actually occurred. The driver should
 * call this function as soon as a timestamp is acquired ensuring
 * clock conversions in input_set_timestamp are done correctly.
 *
 * The system entering suspend state between timestamp acquisition and
 * calling input_set_timestamp can result in inaccurate conversions.
 */
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
// EXPORT_SYMBOL(input_set_timestamp);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_get_timestamp - get timestamp for input events
 * @dev: input device to get timestamp from
 *
 * A valid timestamp is a timestamp of non-zero value.
 */
ktime_t *input_get_timestamp(struct input_dev *dev)
{
	const ktime_t invalid_timestamp = 0;

	if (dev->timestamp[INPUT_CLK_MONO] == invalid_timestamp)
		// input_set_timestamp(dev, ktime_get());
		input_set_timestamp(dev, ktime_get_coarse()); // Port ktime_t is scalar milliseconds.

	return dev->timestamp;
}
// EXPORT_SYMBOL(input_get_timestamp);
// Firmware links this file directly and has no Linux module symbol export.

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

/**
 * input_reset_device() - reset/restore the state of input device
 * @dev: input device whose state needs to be reset
 *
 * This function tries to reset the state of an opened input device and
 * bring internal state and state if the hardware in sync with each other.
 * We mark all keys as released, restore LED state, repeat rate, etc.
 */
void input_reset_device(struct input_dev *dev)
{
	// guard(mutex)(&dev->mutex);
	// guard(spinlock_irqsave)(&dev->event_lock);
	// Port input core has no mutex/event_lock guards.
	// Reset is synchronous in this slice; no blocking event lock.
	input_dev_toggle(dev, true);
	if (input_dev_release_keys(dev)) {
		input_handle_event(dev, EV_SYN, SYN_REPORT, 1);
	}
	// See nonblocking callback-driven note above.
}
// EXPORT_SYMBOL(input_reset_device);
// Firmware links this file directly and has no Linux module symbol export.

// static int input_inhibit_device(struct input_dev *dev)
// {
// 	guard(mutex)(&dev->mutex);
//
// 	if (dev->inhibited)
// 		return 0;
//
// 	if (dev->users) {
// 		if (dev->close)
// 			dev->close(dev);
// 		if (dev->poller)
// 			input_dev_poller_stop(dev->poller);
// 	}
//
// 	scoped_guard(spinlock_irq, &dev->event_lock) {
// 		input_mt_release_slots(dev);
// 		input_dev_release_keys(dev);
// 		input_handle_event(dev, EV_SYN, SYN_REPORT, 1);
// 		input_dev_toggle(dev, false);
// 	}
//
// 	dev->inhibited = true;
//
// 	return 0;
// }
//
// static int input_uninhibit_device(struct input_dev *dev)
// {
// 	int error;
//
// 	guard(mutex)(&dev->mutex);
//
// 	if (!dev->inhibited)
// 		return 0;
//
// 	if (dev->users) {
// 		if (dev->open) {
// 			error = dev->open(dev);
// 			if (error)
// 				return error;
// 		}
// 		if (dev->poller)
// 			input_dev_poller_start(dev->poller);
// 	}
//
// 	dev->inhibited = false;
//
// 	scoped_guard(spinlock_irq, &dev->event_lock)
// 		input_dev_toggle(dev, true);
//
// 	return 0;
// }
//
// static int input_dev_suspend(struct device *dev)
// {
// 	struct input_dev *input_dev = to_input_dev(dev);
//
// 	guard(spinlock_irq)(&input_dev->event_lock);
//
// 	/*
// 	 * Keys that are pressed now are unlikely to be
// 	 * still pressed when we resume.
// 	 */
// 	if (input_dev_release_keys(input_dev))
// 		input_handle_event(input_dev, EV_SYN, SYN_REPORT, 1);
//
// 	/* Turn off LEDs and sounds, if any are active. */
// 	input_dev_toggle(input_dev, false);
//
// 	return 0;
// }
//
// static int input_dev_resume(struct device *dev)
// {
// 	struct input_dev *input_dev = to_input_dev(dev);
//
// 	guard(spinlock_irq)(&input_dev->event_lock);
//
// 	/* Restore state of LEDs and sounds, if any were active. */
// 	input_dev_toggle(input_dev, true);
//
// 	return 0;
// }
//
// static int input_dev_freeze(struct device *dev)
// {
// 	struct input_dev *input_dev = to_input_dev(dev);
//
// 	guard(spinlock_irq)(&input_dev->event_lock);
//
// 	/*
// 	 * Keys that are pressed now are unlikely to be
// 	 * still pressed when we resume.
// 	 */
// 	if (input_dev_release_keys(input_dev))
// 		input_handle_event(input_dev, EV_SYN, SYN_REPORT, 1);
//
// 	return 0;
// }
//
// static int input_dev_poweroff(struct device *dev)
// {
// 	struct input_dev *input_dev = to_input_dev(dev);
//
// 	guard(spinlock_irq)(&input_dev->event_lock);
//
// 	/* Turn off LEDs and sounds, if any are active. */
// 	input_dev_toggle(input_dev, false);
//
// 	return 0;
// }
//
// static const struct dev_pm_ops input_dev_pm_ops = {
// 	.suspend	= input_dev_suspend,
// 	.resume		= input_dev_resume,
// 	.freeze		= input_dev_freeze,
// 	.poweroff	= input_dev_poweroff,
// 	.restore	= input_dev_resume,
// };
//
// static const struct device_type input_dev_type = {
// 	.groups		= input_dev_attr_groups,
// 	.release	= input_dev_release,
// 	.uevent		= input_dev_uevent,
// 	.pm		= pm_sleep_ptr(&input_dev_pm_ops),
// };
//
// static char *input_devnode(const struct device *dev, umode_t *mode)
// {
// 	return kasprintf(GFP_KERNEL, "input/%s", dev_name(dev));
// }
//
// const struct class input_class = {
// 	.name		= "input",
// 	.devnode	= input_devnode,
// };
// EXPORT_SYMBOL_GPL(input_class);
// Firmware has no Linux sysfs input class, PM callbacks, inhibit attributes,
// or poller lifecycle. Device state is managed by the firmware event boundary.

/**
 * input_allocate_device - allocate memory for new input device
 *
 * Returns prepared struct input_dev or %NULL.
 *
 * NOTE: Use input_free_device() to free devices that have not been
 * registered; input_unregister_device() should be used for already
 * registered devices.
 */
struct input_dev *input_allocate_device(void)
{
	// static atomic_t input_no = ATOMIC_INIT(-1);
	// Firmware has no Linux inputN device model naming here.
	struct input_dev *dev;

	dev = kzalloc_obj(*dev);
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

	// mutex_init(&dev->mutex);
	// spin_lock_init(&dev->event_lock);
	// Port input core has no mutex/event_lock; timer exists for upstream autorepeat.
	timer_setup(&dev->timer, NULL, 0);
	INIT_LIST_HEAD(&dev->h_list);
	INIT_LIST_HEAD(&dev->node);

	// dev->dev.type = &input_dev_type;
	// dev->dev.class = &input_class;
	// Firmware has no Linux input device class.
	device_initialize(&dev->dev);
	/*
	 * From this point on we can no longer simply "kfree(dev)", we need
	 * to use input_free_device() so that device core properly frees its
	 * resources associated with the input device.
	 */
	// Firmware input_free_device() owns the direct release path.
	//
	// dev_set_name(&dev->dev, "input%lu",
	// 	      (unsigned long)atomic_inc_return(&input_no));
	// __module_get(THIS_MODULE);
	// Firmware has no Linux device name allocation or module refcount.
	return dev;
}
// EXPORT_SYMBOL(input_allocate_device);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * devm_input_allocate_device - allocate managed input device
 * @dev: device owning the input device being created
 *
 * Returns prepared struct input_dev or %NULL.
 *
 * Managed input devices do not need to be explicitly unregistered or
 * freed as it will be done automatically when owner device unbinds from
 * its driver (or binding fails). Once managed input device is allocated,
 * it is ready to be set up and registered in the same fashion as regular
 * input device. There are no special devm_input_device_[un]register()
 * variants, regular ones work with both managed and unmanaged devices,
 * should you need them. In most cases however, managed input device need
 * not be explicitly unregistered or freed.
 *
 * NOTE: the owner device is set up as parent of input device and users
 * should not override it.
 */
struct input_dev *devm_input_allocate_device(struct device *dev)
{
	// struct input_devres *devres;
	struct input_dev *input = input_allocate_device();

	// devres = devres_alloc(devm_input_device_release,
	// 		       sizeof(*devres), GFP_KERNEL);
	// if (!devres)
	// 	return NULL;
	// Firmware devres uses devm_add_action_or_reset() instead of Linux
	// devres_alloc()/devres_add() objects.
	if (!input)
		return NULL;

	input->dev.parent = dev;
	input->devres_managed = true;
	// devres->input = input;
	// devres_add(dev, devres);
	if (devm_add_action_or_reset(dev, devm_input_device_unregister, input))
		return NULL;

	return input;
}
// EXPORT_SYMBOL(devm_input_allocate_device);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_free_device - free memory occupied by input_dev structure
 * @dev: input device to free
 *
 * This function should only be used if input_register_device()
 * was not called yet or if it failed. Once device was registered
 * use input_unregister_device() and memory will be freed once last
 * reference to the device is dropped.
 *
 * Device should be allocated by input_allocate_device().
 *
 * NOTE: If there are references to the input device then memory
 * will not be freed until last reference is dropped.
 */
void input_free_device(struct input_dev *dev)
{
	// if (dev) {
	// Firmware keeps the direct NULL return, then releases port-owned storage.
	if (!dev)
		return;

	// if (dev->devres_managed)
	// 	WARN_ON(devres_destroy(dev->dev.parent,
	// 				devm_input_device_release,
	// 				devm_input_device_match,
	// 				dev));
	// Firmware devres uses devm_release_action().
	if (dev->devres_managed && dev->dev.parent) {
		if (!devm_release_action(dev->dev.parent, devm_input_device_unregister, dev))
			return;
	}

	// input_put_device(dev);
	// Firmware has no Linux device refcount release path.
	input_ff_destroy(dev);
	input_mt_destroy_slots(dev);
	kfree(dev->absinfo);
	kfree(dev->vals);
	kfree(dev);
	// }
}
// EXPORT_SYMBOL(input_free_device);
// Firmware links this file directly and has no Linux module symbol export.

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

/**
 * input_enable_softrepeat - enable software autorepeat
 * @dev: input device
 * @delay: repeat delay
 * @period: repeat period
 *
 * Enable software autorepeat on the input device.
 */
void input_enable_softrepeat(struct input_dev *dev, int delay, int period)
{
	dev->timer.function = input_repeat_key;
	dev->rep[REP_DELAY] = delay;
	dev->rep[REP_PERIOD] = period;
}
// EXPORT_SYMBOL(input_enable_softrepeat);
// Firmware links this file directly and has no Linux module symbol export.

// bool input_device_enabled(struct input_dev *dev)
// {
// 	lockdep_assert_held(&dev->mutex);
//
// 	return !dev->inhibited && dev->users > 0;
// }
// EXPORT_SYMBOL_GPL(input_device_enabled);
// Firmware has no active input_mutex userspace inhibit path.

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
// EXPORT_SYMBOL(input_set_capability);
// Firmware links this file directly and has no Linux module symbol export.

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

	// scoped_guard(spinlock_irq, &dev->event_lock) {
	// Port input core has no event_lock; registration/tuning is synchronous.
	dev->max_vals = max_vals;
	swap(dev->vals, vals);
	// }

	/* Because of swap() above, this frees the old vals memory */
	kfree(vals);

	return 0;
}

/**
 * input_register_device - register device with input core
 * @dev: device to be registered
 *
 * This function registers device with input core. The device must be
 * allocated with input_allocate_device() and all it's capabilities
 * set up before registering.
 * If function fails the device must be freed with input_free_device().
 * Once device has been successfully registered it can be unregistered
 * with input_unregister_device(); input_free_device() should not be
 * called in this case.
 *
 * Note that this function is also used to register managed input devices
 * (ones allocated with devm_input_allocate_device()). Such managed input
 * devices need not be explicitly unregistered or freed, their tear down
 * is controlled by the devres infrastructure. It is also worth noting
 * that tear down of managed input devices is internally a 2-step process:
 * registered managed input device is first unregistered, but stays in
 * memory and can still handle input_event() calls (although events will
 * not be delivered anywhere). The freeing of managed input device will
 * happen later, when devres stack is unwound to the point where device
 * allocation was made.
 */
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

	// error = device_add(&dev->dev);
	// if (error)
	// 	goto err_devres_free;
	// Firmware has no Linux device model; keep an in-memory input_dev list.
	//
	// path = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
	// pr_info("%s as %s\n",
	// 	dev->name ? dev->name : "Unspecified device",
	// 	path ? path : "N/A");
	// kfree(path);
	// Firmware has no Linux kobject path to log.
	//
	// error = -EINTR;
	// scoped_cond_guard(mutex_intr, goto err_device_del, &input_mutex) {
	// Firmware has no input_mutex; input devices register from the host task.
	list_add_tail(&dev->node, &input_dev_list);
	dev->port_proxy_id = ++input_next_proxy_id;
	dev->registered = true;
	list_for_each_entry(handler, &input_handler_list, node)
		input_attach_handler(dev, handler);
	// }
	//
	// if (dev->devres_managed) {
	// 	dev_dbg(dev->dev.parent, "%s: registering %s with devres.\n",
	// 		__func__, dev_name(&dev->dev));
	// 	devres_add(dev->dev.parent, devres);
	// }
	// Firmware devm_input_allocate_device() uses devm_add_action_or_reset().

	return 0;
}
// EXPORT_SYMBOL(input_register_device);
// Firmware links this file directly and has no Linux module symbol export.

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
	// scoped_guard(mutex, &input_mutex) {
	// Firmware has no input_mutex; unregister runs in the host task context.
	list_for_each_entry_safe(handle, next, &dev->h_list, d_node)
		handle->handler->disconnect(handle);
	// WARN_ON(!list_empty(&dev->h_list));
	// Firmware build keeps warnings out of this callback-driven slice.
	timer_delete_sync(&dev->timer);
	// list_del_init(&dev->node);
	// Port list helper set does not require reinitializing the node.
	list_del(&dev->node);
	// input_wakeup_procfs_readers();
	// }
	//
	// device_del(&dev->dev);
	// Firmware has no Linux device model node to delete.
	dev->registered = false;
}

/**
 * input_unregister_device - unregister previously registered device
 * @dev: device to be unregistered
 *
 * This function unregisters an input device. Once device is unregistered
 * the caller should not try to access it as it may get freed at any moment.
 */
void input_unregister_device(struct input_dev *dev)
{
	// if (dev->devres_managed) {
	// 	WARN_ON(devres_destroy(dev->dev.parent,
	// 				devm_input_device_unregister,
	// 				devm_input_device_match,
	// 				dev));
	// 	__input_unregister_device(dev);
	// } else {
	// 	__input_unregister_device(dev);
	// 	input_put_device(dev);
	// }
	// Firmware has no Linux devres/input_put reference model here; unregister
	// owns the allocation and releases it directly.
	__input_unregister_device(dev);
	input_free_device(dev);
}
// EXPORT_SYMBOL(input_unregister_device);
// Firmware links this file directly and has no Linux module symbol export.

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

/**
 * input_register_handler - register a new input handler
 * @handler: handler to be registered
 *
 * This function registers a new input handler (interface) for input
 * devices in the system and attaches it to all input devices that
 * are compatible with the handler.
 */
int input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	int error;

	error = input_handler_check_methods(handler);
	if (error)
		return error;

	// scoped_cond_guard(mutex_intr, return -EINTR, &input_mutex) {
	// Firmware has no input_mutex; handlers register from the host task.
	INIT_LIST_HEAD(&handler->h_list);
	list_add_tail(&handler->node, &input_handler_list);
	list_for_each_entry(dev, &input_dev_list, node)
		input_attach_handler(dev, handler);
	// input_wakeup_procfs_readers();
	// Firmware has no procfs readers.
	// }

	return 0;
}
// EXPORT_SYMBOL(input_register_handler);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_unregister_handler - unregisters an input handler
 * @handler: handler to be unregistered
 *
 * This function disconnects a handler from its input devices and
 * removes it from lists of known handlers.
 */
void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle, *next;

	// guard(mutex)(&input_mutex);
	// Firmware has no input_mutex; handlers unregister from the host task.
	list_for_each_entry_safe(handle, next, &handler->h_list, h_node)
		handler->disconnect(handle);
	// WARN_ON(!list_empty(&handler->h_list));
	// Firmware build keeps warnings out of this callback-driven slice.
	// list_del_init(&handler->node);
	// Port list helper set does not require reinitializing the node.
	list_del(&handler->node);
	// input_wakeup_procfs_readers();
	// Firmware has no procfs readers.
}
// EXPORT_SYMBOL(input_unregister_handler);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_handler_for_each_handle - handle iterator
 * @handler: input handler to iterate
 * @data: data for the callback
 * @fn: function to be called for each handle
 *
 * Iterate over @bus's list of devices, and call @fn for each, passing
 * it @data and stop when @fn returns a non-zero value. The function is
 * using RCU to traverse the list and therefore may be using in atomic
 * contexts. The @fn callback is invoked from RCU critical section and
 * thus must not sleep.
 */
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
// EXPORT_SYMBOL(input_handler_for_each_handle);
// Firmware links this file directly and has no Linux module symbol export.

/*
 * An implementation of input_handle's handle_events() method that simply
 * invokes handler->event() method for each event one by one.
 */
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

/*
 * An implementation of input_handle's handle_events() method that invokes
 * handler->filter() method for each event one by one and removes events
 * that were filtered out from the "vals" array.
 */
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

/*
 * An implementation of input_handle's handle_events() method that does nothing.
 */
static unsigned int input_handle_events_null(struct input_handle *handle,
					     struct input_value *vals,
					     unsigned int count)
{
	return count;
}

/*
 * Sets up appropriate handle->event_handler based on the input_handler
 * associated with the handle.
 */
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

/**
 * input_register_handle - register a new input handle
 * @handle: handle to register
 *
 * This function puts a new input handle onto device's
 * and handler's lists so that events can flow through
 * it once it is opened using input_open_device().
 *
 * This function is supposed to be called from handler's
 * connect() method.
 */
int input_register_handle(struct input_handle *handle)
{
	struct input_handler *handler = handle->handler;
	struct input_dev *dev = handle->dev;

	INIT_LIST_HEAD(&handle->d_node);
	INIT_LIST_HEAD(&handle->h_node);
	input_handle_setup_event_handler(handle);
	/*
	 * We take dev->mutex here to prevent race with
	 * input_release_device().
	 */
	// scoped_cond_guard(mutex_intr, return -EINTR, &dev->mutex) {
	// Port input core has no mutex.
	/*
	 * Filters go to the head of the list, normal handlers
	 * to the tail.
	 */
	// if (handler->filter)
	// 	list_add_rcu(&handle->d_node, &dev->h_list);
	// else
	// 	list_add_tail_rcu(&handle->d_node, &dev->h_list);
	// This port has no RCU input core; filters still go before normal handlers.
	// Callback-driven slice has no concurrent input worker; do not block in HID callbacks.
	if (handler->filter)
		list_add(&handle->d_node, &dev->h_list);
	else
		list_add_tail(&handle->d_node, &dev->h_list);
	// }
	// See nonblocking callback-driven note above.
	/*
	 * Since we are supposed to be called from ->connect()
	 * which is mutually exclusive with ->disconnect()
	 * we can't be racing with input_unregister_handle()
	 * and so separate lock is not needed here.
	 */
	// list_add_tail_rcu(&handle->h_node, &handler->h_list);
	// Port list has no RCU variant.
	list_add_tail(&handle->h_node, &handler->h_list);
	if (handler->start)
		handler->start(handle);
	return 0;
}
// EXPORT_SYMBOL(input_register_handle);
// Firmware links this file directly and has no Linux module symbol export.

/**
 * input_unregister_handle - unregister an input handle
 * @handle: handle to unregister
 *
 * This function removes input handle from device's
 * and handler's lists.
 *
 * This function is supposed to be called from handler's
 * disconnect() method.
 */
void input_unregister_handle(struct input_handle *handle)
{
	struct input_dev *dev = handle->dev;

	// list_del_rcu(&handle->h_node);
	/*
	 * Take dev->mutex to prevent race with input_release_device().
	 */
	// scoped_guard(mutex, &dev->mutex)
	// 	list_del_rcu(&handle->d_node);
	// synchronize_rcu();
	// Port input core has no mutex/RCU. Keep the pre-existing firmware
	// deletion order in this comment-only upstream-alignment pass.
	list_del(&handle->d_node);
	list_del(&handle->h_node);
}
// EXPORT_SYMBOL(input_unregister_handle);
// Firmware links this file directly and has no Linux module symbol export.

static void devm_input_device_unregister(void *data)
{
	struct input_dev *dev = data;

	dev->devres_managed = false;
	if (dev->registered)
		input_unregister_device(dev);
	else
		input_free_device(dev);
}
static void input_repeat_key(struct timer_list *t)
{
	struct input_dev *dev = timer_container_of(dev, t, timer);

	// guard(spinlock_irqsave)(&dev->event_lock);
	// Port input core has no event_lock; software repeat work is inactive in
	// the callback-driven slice.
	// Software repeat worker is not active in the callback-driven slice.
	if (!dev->inhibited &&
	    test_bit(dev->repeat_key, dev->key) &&
	    is_event_supported(dev->repeat_key, dev->keybit, KEY_MAX)) {

		// input_set_timestamp(dev, ktime_get());
		input_set_timestamp(dev, ktime_get_coarse()); // Port ktime_t is scalar milliseconds.
		input_handle_event(dev, EV_KEY, dev->repeat_key, 2);
		input_handle_event(dev, EV_SYN, SYN_REPORT, 1);

		if (dev->rep[REP_PERIOD])
			mod_timer(&dev->timer, jiffies +
					msecs_to_jiffies(dev->rep[REP_PERIOD]));
	}
	// See nonblocking callback-driven note above.
}

// /**
//  * input_get_new_minor - allocates a new input minor number
//  * @legacy_base: beginning or the legacy range to be searched
//  * @legacy_num: size of legacy range
//  * @allow_dynamic: whether we can also take ID from the dynamic range
//  *
//  * This function allocates a new device minor for from input major namespace.
//  * Caller can request legacy minor by specifying @legacy_base and @legacy_num
//  * parameters and whether ID can be allocated from dynamic range if there are
//  * no free IDs in legacy range.
//  */
// int input_get_new_minor(int legacy_base, unsigned int legacy_num,
// 			bool allow_dynamic)
// {
// 	/*
// 	 * This function should be called from input handler's ->connect()
// 	 * methods, which are serialized with input_mutex, so no additional
// 	 * locking is needed here.
// 	 */
// 	if (legacy_base >= 0) {
// 		int minor = ida_alloc_range(&input_ida, legacy_base,
// 					    legacy_base + legacy_num - 1,
// 					    GFP_KERNEL);
// 		if (minor >= 0 || !allow_dynamic)
// 			return minor;
// 	}
//
// 	return ida_alloc_range(&input_ida, INPUT_FIRST_DYNAMIC_DEV,
// 			       INPUT_MAX_CHAR_DEVICES - 1, GFP_KERNEL);
// }
// EXPORT_SYMBOL(input_get_new_minor);
//
// /**
//  * input_free_minor - release previously allocated minor
//  * @minor: minor to be released
//  *
//  * This function releases previously allocated input minor so that it can be
//  * reused later.
//  */
// void input_free_minor(unsigned int minor)
// {
// 	ida_free(&input_ida, minor);
// }
// EXPORT_SYMBOL(input_free_minor);
//
// static int __init input_init(void)
// {
// 	int err;
//
// 	err = class_register(&input_class);
// 	if (err) {
// 		pr_err("unable to register input_dev class\n");
// 		return err;
// 	}
//
// 	err = input_proc_init();
// 	if (err)
// 		goto fail1;
//
// 	err = register_chrdev_region(MKDEV(INPUT_MAJOR, 0),
// 				     INPUT_MAX_CHAR_DEVICES, "input");
// 	if (err) {
// 		pr_err("unable to register char major %d", INPUT_MAJOR);
// 		goto fail2;
// 	}
//
// 	return 0;
//
//  fail2:	input_proc_exit();
//  fail1:	class_unregister(&input_class);
// 	return err;
// }
//
// static void __exit input_exit(void)
// {
// 	input_proc_exit();
// 	unregister_chrdev_region(MKDEV(INPUT_MAJOR, 0),
// 				 INPUT_MAX_CHAR_DEVICES);
// 	class_unregister(&input_class);
// }
//
// subsys_initcall(input_init);
// module_exit(input_exit);
// Firmware has no Linux input char devices, procfs, class registration, or
// module init/exit lifecycle. Device discovery is handled by firmware glue.

struct input_dev *input_find_device_by_name(const char *name)
{
	struct input_dev *dev;

	// Upstream Linux: no equivalent; firmware HID glue needs an in-memory
	// input_dev lookup because there is no Linux device model registry.
	list_for_each_entry(dev, &input_dev_list, node)
		if (!strcmp(dev->name, name))
			return dev;

	return NULL;
}

int input_for_each_device(int (*fn)(struct input_dev *dev, void *data), void *data)
{
	struct input_dev *dev;

	// Upstream Linux: no equivalent; firmware HID glue iterates the in-memory
	// input_dev list because there is no Linux device model registry.
	list_for_each_entry(dev, &input_dev_list, node) {
		int ret = fn(dev, data);

		if (ret)
			return ret;
	}

	return 0;
}
