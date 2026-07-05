#ifndef USB_HOST_LINUX_HID_COMPAT_H
#define USB_HOST_LINUX_HID_COMPAT_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "semphr.h"
#include "task.h"
#include "tusb.h"
#include "log.h"
#include "pico/rand.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef uint8_t __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
typedef int8_t __s8;
typedef int16_t __s16;
typedef int32_t __s32;
typedef int64_t __s64;
typedef uint16_t __le16;
typedef int gfp_t;
typedef int pm_message_t;
typedef struct {
        SemaphoreHandle_t handle;
} spinlock_t;
typedef struct {
	TaskHandle_t task;
	volatile uint32_t sequence;
} wait_queue_head_t;
typedef int ktime_t;
typedef long loff_t;
typedef unsigned long kernel_ulong_t;
typedef unsigned int umode_t;

typedef int (*hid_compat_exec_fn_t)(void *data);
int hid_compat_executor_init(void);
bool hid_compat_in_executor(void);
void hid_compat_executor_kick(void);
int hid_compat_exec_sync(hid_compat_exec_fn_t fn, void *data);
int hid_compat_exec_async(hid_compat_exec_fn_t fn, void *data);

/*
 * Temporary callback-driven host slice: keep Linux descriptor/report/input
 * parsing, but do not enable subsystems that need hardware control requests,
 * workqueue continuations, or userspace/class proxy surfaces yet.
 */
#undef CONFIG_HID_BATTERY_STRENGTH
#undef CONFIG_DRAGONRISE_FF
#undef CONFIG_GREENASIA_FF
#undef CONFIG_HID_ACRUX_FF
#undef CONFIG_HID_CORSAIR_VOID
#undef CONFIG_HID_PID
#undef CONFIG_HID_STEELSERIES
#undef CONFIG_BACKLIGHT_CLASS_DEVICE
#undef CONFIG_HOLTEK_FF
#undef CONFIG_LEDS_CLASS
#undef CONFIG_LOGIG940_FF
#undef CONFIG_LOGIRUMBLEPAD2_FF
#undef CONFIG_LOGITECH_FF
#undef CONFIG_LOGIWHEELS_FF
#undef CONFIG_PANTHERLORD_FF
#undef CONFIG_SMARTJOYPLUS_FF
#undef CONFIG_THRUSTMASTER_FF
#undef CONFIG_ZEROPLUS_FF
#define __user

struct dentry {
	int unused;
};

struct hid_device_id {
	__u16 bus;
	__u16 group;
	__u32 vendor;
	__u32 product;
	kernel_ulong_t driver_data;
};

struct module {
	int unused;
};

struct task_struct {
	const char *comm;
};

static const struct task_struct hid_compat_current = {
	.comm = "hid-compat",
};

#define current (&hid_compat_current)

static inline int task_pid_nr(const struct task_struct *task)
{
	(void)task;
	return 0;
}

static inline int signal_pending(const struct task_struct *task)
{
	(void)task;
	return 0;
}

struct kref {
	int refcount;
};

struct mutex {
        SemaphoreHandle_t handle;
        uint8_t locked;
};

#define DEFINE_MUTEX(name) struct mutex name = { NULL, 0 }

struct semaphore {
	SemaphoreHandle_t handle;
};

struct workqueue_struct {
	struct work_struct *head;
	struct work_struct *tail;
	struct workqueue_struct *next;
	uint8_t running;
	uint8_t destroying;
};

struct work_struct {
	void (*func)(struct work_struct *work);
	uint8_t pending;
	uint8_t running;
	uint8_t canceling;
	struct workqueue_struct *wq;
	struct work_struct *next;
};

struct delayed_work {
	struct work_struct work;
	struct workqueue_struct *wq;
	unsigned long due;
	uint8_t delayed_pending;
	struct delayed_work *next;
};

#define INIT_WORK(work, fn) do { (work)->func = (fn); (work)->pending = 0; (work)->running = 0; (work)->canceling = 0; (work)->wq = NULL; (work)->next = NULL; } while (0)
#define INIT_DELAYED_WORK(dwork, fn) do { INIT_WORK(&(dwork)->work, (fn)); (dwork)->wq = NULL; (dwork)->due = 0; (dwork)->delayed_pending = 0; (dwork)->next = NULL; } while (0)
#define to_delayed_work(work) container_of(work, struct delayed_work, work)
int hid_compat_workqueue_init(void);
bool hid_compat_workqueue_poll(void);
TickType_t hid_compat_workqueue_next_timeout(void);
bool schedule_work(struct work_struct *work);
bool hid_compat_queue_work(struct workqueue_struct *wq, struct work_struct *work);
bool hid_compat_flush_work(struct work_struct *work);
struct workqueue_struct *hid_compat_create_singlethread_workqueue(const char *name);
void hid_compat_destroy_workqueue(struct workqueue_struct *wq);
void cancel_work_sync(struct work_struct *work);
bool hid_compat_queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
				   unsigned long delay);
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay);
bool cancel_delayed_work(struct delayed_work *dwork);
bool cancel_delayed_work_sync(struct delayed_work *dwork);
static inline bool delayed_work_pending(struct delayed_work *dwork)
{
	return dwork->delayed_pending || dwork->work.pending;
}

struct timer_list {
	void (*function)(struct timer_list *timer);
	unsigned long expires;
	uint8_t pending;
	uint8_t running;
	struct timer_list *next;
};

struct file {
	int unused;
};

struct fasync_struct {
	int unused;
};

struct kobject {
	struct device *dev;
};

enum kobject_action {
	KOBJ_ADD,
	KOBJ_REMOVE,
	KOBJ_CHANGE,
	KOBJ_MOVE,
	KOBJ_ONLINE,
	KOBJ_OFFLINE,
	KOBJ_BIND,
	KOBJ_UNBIND,
};

typedef int atomic_t;

#define GFP_KERNEL 0
#define GFP_ATOMIC 0
#define ENOMEM 12
#define ENODEV 19
#define EBUSY 16
#define EINVAL 22
#define ERANGE 34
#define EIO 5
#define EPIPE 32
#define ENOSYS 38
#define ENOTSUPP 524
#define EINTR 4
#define ENODATA 61
#define ENOENT 2
#define ENOSPC 28
#define EPERM 1
#define EACCES 13
#define EPROTO 71
#define ENXIO 6
#define EAGAIN 11
#define EREMOTEIO 121
#define ETIMEDOUT 110
#define E2BIG 7

#define BIT(n) (1u << (n))
#define BIT_ULL(n) (1ULL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define IS_BUILTIN(option) (option)
#define IS_MODULE(option) 0
#define IS_ENABLED(option) (option)
#define IS_REACHABLE(option) (option)
#define EXPORT_SYMBOL(x)
#define EXPORT_SYMBOL_GPL(x)
#define __must_check
#define __init
#define __exit
#define THIS_MODULE NULL
#define KBUILD_MODNAME "ergotype"
#define MODULE_DEVICE_TABLE(type, name)
#define MODULE_AUTHOR(name)
#define MODULE_DESCRIPTION(desc)
#define MODULE_LICENSE(license)
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define module_param_array_named(name, array, type, nump, perm)
#define MODULE_PARM_DESC(name, desc)
typedef int (*linux_initcall_t)(void);
#define __LINUX_INITCALL_CONCAT(a, b) a##b
#define __LINUX_INITCALL_CONCAT2(a, b) __LINUX_INITCALL_CONCAT(a, b)
// Upstream module_init() enters Linux module/initcall machinery. Firmware has
// no module loader, so built-in initcalls live in the linux_initcalls section.
// Upstream module_exit() registers unload cleanup; firmware does not unload.
// Upstream module_driver() builds module init/exit wrappers. This port stores
// HID drivers through module_hid_driver() or explicit module_init() entries.
#define module_init(fn) \
	static linux_initcall_t const __LINUX_INITCALL_CONCAT2(__linux_initcall_, __COUNTER__) \
	__attribute__((used, section("linux_initcalls"))) = fn
#define module_exit(fn)
#define module_driver(__driver, __register, __unregister, ...)
#define ATOMIC_INIT(v) (v)
#define U8_MAX ((u8)~0U)
#define S16_MAX INT16_MAX
#define S16_MIN INT16_MIN
#define U16_MAX ((u16)~0U)
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#define WARN_ON(x) (x)
#define BUG_ON(x) configASSERT(!(x))
#define unlikely(x) (x)
#define min(x, y) ((x) < (y) ? (x) : (y))
#define IS_ERR(ptr) ((uintptr_t)(ptr) >= (uintptr_t)-4095)
#define PTR_ERR(ptr) ((long)(ptr))
#define ERR_PTR(err) ((void *)(intptr_t)(err))
// Imported Linux HID code can run from TinyUSB host callbacks in this slice;
// do not call the firmware logger from those paths.
#define pr_err(fmt, ...) do { } while (0)
#define pr_notice(fmt, ...) do { } while (0)
#define pr_info(fmt, ...) do { } while (0)
#define pr_debug(fmt, ...) do { } while (0)
#define pr_warn(fmt, ...) do { } while (0)
#define pr_warn_ratelimited(fmt, ...) do { } while (0)
#define dev_info(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_notice(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_dbg(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_warn(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_err(dev, fmt, ...) do { (void)(dev); } while (0)
#define dev_err_once(dev, fmt, ...) do { static bool __done; if (!__done) { __done = true; dev_err(dev, fmt, ##__VA_ARGS__); } } while (0)
#define dev_notice_once(dev, fmt, ...) do { static bool __done; if (!__done) { __done = true; dev_notice(dev, fmt, ##__VA_ARGS__); } } while (0)
#define dev_warn_once(dev, fmt, ...) do { static bool __done; if (!__done) { __done = true; dev_warn(dev, fmt, ##__VA_ARGS__); } } while (0)
#define dev_info_once(dev, fmt, ...) do { static bool __done; if (!__done) { __done = true; dev_info(dev, fmt, ##__VA_ARGS__); } } while (0)
#define dev_dbg_once(dev, fmt, ...) do { static bool __done; if (!__done) { __done = true; dev_dbg(dev, fmt, ##__VA_ARGS__); } } while (0)
#define dev_err_ratelimited(dev, fmt, ...) dev_err_once(dev, fmt, ##__VA_ARGS__)
#define dev_notice_ratelimited(dev, fmt, ...) dev_notice_once(dev, fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...) dev_warn_once(dev, fmt, ##__VA_ARGS__)
#define dev_info_ratelimited(dev, fmt, ...) dev_info_once(dev, fmt, ##__VA_ARGS__)
#define dev_dbg_ratelimited(dev, fmt, ...) dev_dbg_once(dev, fmt, ##__VA_ARGS__)
#define pm_ptr(ptr) (ptr)
#define max_t(type, x, y) ((type)(x) > (type)(y) ? (type)(x) : (type)(y))
#define min_t(type, x, y) ((type)(x) < (type)(y) ? (type)(x) : (type)(y))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, divisor) (((x) + ((divisor) / 2)) / (divisor))
#define mult_frac(x, numer, denom) ((x) * (numer) / (denom))
#define clamp(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))
#define array3_size(a, b, c) ((a) * (b) * (c))
#define swap(a, b) do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)
#define GENMASK(h, l) (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define FIELD_GET(mask, reg) (((reg) & (mask)) >> __builtin_ctzl(mask))
#define fallthrough __attribute__((fallthrough))
#define HZ configTICK_RATE_HZ
#define time_after(a, b) ((long)((b) - (a)) < 0)
#define time_before(a, b) ((long)((a) - (b)) < 0)
#define time_after_eq(a, b) ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b) ((long)((b) - (a)) >= 0)
#define BITS_PER_LONG (sizeof(unsigned long) * 8u)
#define BITS_TO_LONGS(nr) DIV_ROUND_UP((nr), BITS_PER_LONG)
#define BIT_MASK(nr) (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr) ((nr) / BITS_PER_LONG)
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]
#define clamp_val(val, lo, hi) clamp(val, lo, hi)
#define PAGE_SIZE 4096
#define __must_hold(x) do { (void)(x); } while (0)
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IROTH 0004
#define S_IRUGO (S_IRUSR | S_IRGRP | S_IROTH)
#define DEVICE_ATTR(_name, _mode, _show, _store) \
	struct device_attribute dev_attr_##_name = { .attr = { .name = #_name, .mode = (_mode) }, .show = (_show), .store = (_store) }
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { .name = #_name, .mode = S_IRUSR | S_IWUSR }, .show = _name##_show, .store = _name##_store }
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { .name = #_name, .mode = S_IWUSR }, .store = _name##_store }
#define UEVENT_NUM_ENVP 64
#define UEVENT_BUFFER_SIZE 2048

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) struct list_head name = LIST_HEAD_INIT(name)
#define POISON_POINTER_DELTA 0
#define LIST_POISON1 ((void *)0x100 + POISON_POINTER_DELTA)
#define LIST_POISON2 ((void *)0x122 + POISON_POINTER_DELTA)

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void __list_add(struct list_head *entry, struct list_head *prev, struct list_head *next)
{
	next->prev = entry;
	entry->next = next;
	entry->prev = prev;
	prev->next = entry;
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void list_add_tail(struct list_head *entry, struct list_head *head)
{
	__list_add(entry, head->prev, head);
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void list_add(struct list_head *entry, struct list_head *head)
{
	__list_add(entry, head, head->next);
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = LIST_POISON1;
	entry->prev = LIST_POISON2;
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline void list_replace(struct list_head *old, struct list_head *new)
{
	new->next = old->next;
	new->next->prev = new;
	new->prev = old->prev;
	new->prev->next = new;
}

// Minimal local copy of Linux include/linux/list.h behavior.
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define list_entry(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
#define list_first_entry(ptr, type, member) list_entry((ptr)->next, type, member)
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); pos = list_entry(pos->member.next, typeof(*pos), member))
#define list_is_last(list, head) ((list)->next == (head))
#define list_first_entry_or_null(ptr, type, member) \
	(list_empty(ptr) ? NULL : list_entry((ptr)->next, type, member))
#define list_next_entry_or_null(pos, head, member) \
	(pos->member.next == (head) ? NULL : list_entry(pos->member.next, typeof(*pos), member))
#define list_for_each_entry_safe(pos, n, head, member) \
	for (pos = list_first_entry_or_null(head, typeof(*pos), member), n = pos ? list_next_entry_or_null(pos, head, member) : NULL; \
	     pos; \
	     pos = n, n = pos ? list_next_entry_or_null(pos, head, member) : NULL)

struct attribute {
	const char *name;
	unsigned int mode;
};

struct bin_attribute {
	struct attribute attr;
	size_t size;
	ssize_t (*read)(struct file *filp, struct kobject *kobj,
			const struct bin_attribute *attr,
			char *buf, loff_t off, size_t count);
	ssize_t (*write)(struct file *filp, struct kobject *kobj,
			 const struct bin_attribute *attr,
			 char *buf, loff_t off, size_t count);
};

struct attribute_group {
	struct attribute **attrs;
	umode_t (*is_visible)(struct kobject *kobj, struct attribute *attr, int n);
	const struct bin_attribute **bin_attrs;
};

struct device_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
};

typedef int (*device_attr_iter_fn)(struct device *dev,
				   const struct device_attribute *attr,
				   void *data);
typedef int (*device_bin_attr_iter_fn)(struct device *dev,
				       const struct bin_attribute *attr,
				       void *data);

struct device_driver;

struct driver_attribute {
	struct attribute attr;
	ssize_t (*show)(struct device_driver *drv, char *buf);
	ssize_t (*store)(struct device_driver *drv, const char *buf, size_t count);
};

struct kobj_uevent_env {
	char *argv[3];
	char *envp[UEVENT_NUM_ENVP];
	int envp_idx;
	char buf[UEVENT_BUFFER_SIZE];
	int buflen;
};

struct device;
struct hid_driver;

struct bus_type {
	const char *name;
	const struct attribute_group **dev_groups;
	const struct attribute_group **drv_groups;
	int (*match)(struct device *dev, const struct device_driver *drv);
	int (*probe)(struct device *dev);
	void (*remove)(struct device *dev);
	int (*uevent)(const struct device *dev, struct kobj_uevent_env *env);
	struct list_head devices;
	struct list_head drivers;
};

struct device_driver {
	const char *name;
	const struct bus_type *bus;
	struct module *owner;
	const char *mod_name;
	const struct attribute_group **dev_groups;
	const struct hid_driver *hid_driver;
	struct list_head bus_node;
	struct list_head attrs;
};

struct device_attr_entry {
	struct list_head node;
	const struct device_attribute *attr;
};

struct device_bin_attr_entry {
	struct list_head node;
	const struct bin_attribute *attr;
};

struct driver_attr_entry {
	struct list_head node;
	const struct driver_attribute *attr;
};

struct device {
	const struct bus_type *bus;
	struct device *parent;
	struct device_driver *driver;
	void (*release)(struct device *dev);
	void *data;
	struct kobject kobj;
	struct list_head bus_node;
	struct list_head devres;
	struct list_head attrs;
	struct list_head bin_attrs;
	unsigned int uevent_count;
	enum kobject_action last_uevent_action;
	unsigned int sysfs_notify_count;
	const char *last_sysfs_notify_dir;
	const char *last_sysfs_notify_attr;
	bool wakeup_enabled;
	char name[32];
};

struct device_node {
	int unused;
};

#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { .name = #_name, .mode = S_IRUSR }, .show = _name##_show }
#define BIN_ATTR_RO(_name, _size) \
	struct bin_attribute bin_attr_##_name = { .attr = { .name = #_name, .mode = S_IRUSR }, .size = (_size), .read = _name##_read }
#define DRIVER_ATTR_WO(_name) \
	struct driver_attribute driver_attr_##_name = { .attr = { .name = #_name, .mode = S_IWUSR }, .store = _name##_store }
#define ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group _name##_group = { .attrs = _name##_attrs }; \
	static const struct attribute_group *_name##_groups[] = { &_name##_group, NULL }
#define __ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group *_name##_groups[] = { &_name##_group, NULL }

static inline int atomic_inc_return(atomic_t *v)
{
	int ret;

	taskENTER_CRITICAL();
	ret = ++*v;
	taskEXIT_CRITICAL();
	return ret;
}

static inline int bus_register(const struct bus_type *bus)
{
	struct bus_type *b = (struct bus_type *)bus;

	INIT_LIST_HEAD(&b->devices);
	INIT_LIST_HEAD(&b->drivers);
	return 0;
}

static inline void bus_unregister(const struct bus_type *bus)
{
	(void)bus;
}

static inline int bus_for_each_drv(const struct bus_type *bus, void *start, void *data,
				   int (*fn)(struct device_driver *, void *))
{
	struct bus_type *b = (struct bus_type *)bus;
	struct device_driver *drv;

	(void)start;
	list_for_each_entry(drv, &b->drivers, bus_node) {
		int ret = fn(drv, data);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int bus_for_each_dev(const struct bus_type *bus, void *start, void *data,
				   int (*fn)(struct device *, void *))
{
	struct bus_type *b = (struct bus_type *)bus;
	struct device *dev;

	(void)start;
	list_for_each_entry(dev, &b->devices, bus_node) {
		int ret = fn(dev, data);
		if (ret)
			return ret;
	}

	return 0;
}

static inline int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp);
static inline void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp);
static inline int sysfs_create_bin_file(struct kobject *kobj, const struct bin_attribute *attr);
static inline void sysfs_remove_bin_file(struct kobject *kobj, const struct bin_attribute *attr);
static inline int driver_sysfs_create_group(struct device_driver *drv, const struct attribute_group *grp);
static inline void driver_sysfs_remove_group(struct device_driver *drv, const struct attribute_group *grp);
static inline int device_sysfs_create_groups(struct device *dev, const struct attribute_group **groups);
static inline void device_sysfs_remove_groups(struct device *dev, const struct attribute_group **groups);
static inline int kobject_uevent(struct kobject *kobj, enum kobject_action action);

static inline int device_probe(struct device *dev)
{
	const struct bus_type *bus = dev->bus;
	struct bus_type *b = (struct bus_type *)bus;
	struct device_driver *drv;

	list_for_each_entry(drv, &b->drivers, bus_node) {
		if (bus->match && !bus->match(dev, drv))
			continue;

		dev->driver = drv;
		if (!bus->probe || bus->probe(dev) == 0) {
			int ret = device_sysfs_create_groups(dev, drv->dev_groups);

			if (!ret) {
				kobject_uevent(&dev->kobj, KOBJ_BIND);
				return 0;
			}
			if (bus->remove)
				bus->remove(dev);
		}
		dev->driver = NULL;
	}

	return -ENODEV;
}

static inline int device_sysfs_create_groups(struct device *dev, const struct attribute_group **groups)
{
	const struct attribute_group **group;

	if (!groups)
		return 0;

	for (group = groups; *group; group++) {
		int ret = sysfs_create_group(&dev->kobj, *group);

		if (ret) {
			while (group != groups) {
				group--;
				sysfs_remove_group(&dev->kobj, *group);
			}
			return ret;
		}
	}

	return 0;
}

static inline void device_sysfs_remove_groups(struct device *dev, const struct attribute_group **groups)
{
	const struct attribute_group **group;

	if (!groups)
		return;

	for (group = groups; *group; group++)
		sysfs_remove_group(&dev->kobj, *group);
}

static inline void device_sysfs_remove_all(struct device *dev)
{
	struct device_attr_entry *attr, *attr_next;
	struct device_bin_attr_entry *bin_attr, *bin_attr_next;

	list_for_each_entry_safe(attr, attr_next, &dev->attrs, node) {
		list_del(&attr->node);
		vPortFree(attr);
	}

	list_for_each_entry_safe(bin_attr, bin_attr_next, &dev->bin_attrs, node) {
		list_del(&bin_attr->node);
		vPortFree(bin_attr);
	}
}

static inline int device_add(struct device *dev)
{
	struct bus_type *bus = (struct bus_type *)dev->bus;
	int ret;

	INIT_LIST_HEAD(&dev->bus_node);
	list_add_tail(&dev->bus_node, &bus->devices);

	ret = device_sysfs_create_groups(dev, bus->dev_groups);
	if (ret) {
		list_del(&dev->bus_node);
		return ret;
	}

	// if (device_probe(dev) < 0) {
	// 	list_del(&dev->bus_node);
	// 	return -ENODEV;
	// }
	// Linux device_add() registers the device even when no driver binds yet;
	// later driver_register()/bus_rescan_devices() can probe it again.
	kobject_uevent(&dev->kobj, KOBJ_ADD);
	// bus_probe_device(dev);
	// Reduced local driver core probes synchronously after the upstream
	// KOBJ_ADD event point.
	device_probe(dev);

	return 0;
}

static inline void device_del(struct device *dev)
{
	kobject_uevent(&dev->kobj, KOBJ_REMOVE);
	if (dev->driver)
		device_sysfs_remove_groups(dev, dev->driver->dev_groups);
	if (dev->driver && dev->bus && dev->bus->remove)
		dev->bus->remove(dev);
	if (dev->bus)
		device_sysfs_remove_groups(dev, dev->bus->dev_groups);
	device_sysfs_remove_all(dev);
	list_del(&dev->bus_node);
	dev->driver = NULL;
}

static inline int device_reprobe(struct device *dev)
{
	if (dev->driver)
		device_sysfs_remove_groups(dev, dev->driver->dev_groups);
	if (dev->driver && dev->bus && dev->bus->remove)
		dev->bus->remove(dev);
	if (dev->driver)
		kobject_uevent(&dev->kobj, KOBJ_UNBIND);
	dev->driver = NULL;
	return device_probe(dev);
}

static inline int bus_rescan_devices(const struct bus_type *bus)
{
	struct bus_type *b = (struct bus_type *)bus;
	struct device *dev;

	list_for_each_entry(dev, &b->devices, bus_node)
		if (!dev->driver)
			device_probe(dev);

	return 0;
}

static inline int driver_attach(struct device_driver *drv);

static inline int driver_register(struct device_driver *drv)
{
	struct bus_type *bus = (struct bus_type *)drv->bus;
	const struct attribute_group **group;

	INIT_LIST_HEAD(&drv->bus_node);
	INIT_LIST_HEAD(&drv->attrs);
	list_add_tail(&drv->bus_node, &bus->drivers);

	if (bus->drv_groups)
		for (group = bus->drv_groups; *group; group++) {
			int ret = driver_sysfs_create_group(drv, *group);

			if (ret) {
				while (group != bus->drv_groups) {
					group--;
					driver_sysfs_remove_group(drv, *group);
				}
				list_del(&drv->bus_node);
				return ret;
			}
		}

	// Linux driver_register() reaches driver_attach() through the driver core.
	driver_attach(drv);
	return 0;
}

static inline void driver_unregister(struct device_driver *drv)
{
	struct bus_type *bus = (struct bus_type *)drv->bus;
	const struct attribute_group **group;
	struct device *dev;

	list_for_each_entry(dev, &bus->devices, bus_node) {
		if (dev->driver != drv)
			continue;
		device_sysfs_remove_groups(dev, drv->dev_groups);
		if (bus->remove)
			bus->remove(dev);
		kobject_uevent(&dev->kobj, KOBJ_UNBIND);
		dev->driver = NULL;
	}

	if (bus->drv_groups)
		for (group = bus->drv_groups; *group; group++)
			driver_sysfs_remove_group(drv, *group);
	list_del(&drv->bus_node);
}

static inline int driver_attach(struct device_driver *drv)
{
	struct bus_type *bus = (struct bus_type *)drv->bus;
	struct device *dev;

	list_for_each_entry(dev, &bus->devices, bus_node) {
		int ret;

		if (dev->driver)
			continue;
		if (drv->bus->match && !drv->bus->match(dev, drv))
			continue;
		dev->driver = drv;
		if (drv->bus->probe && drv->bus->probe(dev) < 0) {
			dev->driver = NULL;
			continue;
		}
		ret = device_sysfs_create_groups(dev, drv->dev_groups);
		if (ret) {
			if (drv->bus->remove)
				drv->bus->remove(dev);
			dev->driver = NULL;
		} else {
			kobject_uevent(&dev->kobj, KOBJ_BIND);
		}
	}

	return 0;
}

static inline void device_initialize(struct device *dev)
{
	dev->kobj.dev = dev;
	INIT_LIST_HEAD(&dev->bus_node);
	INIT_LIST_HEAD(&dev->devres);
	INIT_LIST_HEAD(&dev->attrs);
	INIT_LIST_HEAD(&dev->bin_attrs);
	dev->uevent_count = 0;
	dev->last_uevent_action = KOBJ_ADD;
	dev->sysfs_notify_count = 0;
	dev->last_sysfs_notify_dir = NULL;
	dev->last_sysfs_notify_attr = NULL;
	dev->wakeup_enabled = false;
}

static inline void *dev_get_drvdata(const struct device *dev)
{
	return dev->data;
}

static inline void dev_set_drvdata(struct device *dev, void *data)
{
	dev->data = data;
}

static inline void put_device(struct device *dev)
{
	if (dev->release)
		dev->release(dev);
}

static inline struct device *get_device(struct device *dev)
{
	return dev;
}

static inline void device_enable_async_suspend(struct device *dev)
{
	(void)dev;
}

static inline bool device_may_wakeup(struct device *dev)
{
	return dev->wakeup_enabled;
}

static inline int device_set_wakeup_enable(struct device *dev, bool enable)
{
	dev->wakeup_enabled = enable;
	return 0;
}

static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return kobj->dev;
}

static inline int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int ret;

	if (!size)
		return 0;

	ret = vsnprintf(buf, size, fmt, args);
	if ((size_t)ret < size)
		return ret;
	return (int)size - 1;
}

static inline int sysfs_emit(char *buf, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vscnprintf(buf, PAGE_SIZE, fmt, ap);
	va_end(ap);
	return ret;
}

static inline int sysfs_emit_at(char *buf, int at, const char *fmt, ...)
{
	va_list ap;
	int ret;

	if (at < 0 || at >= PAGE_SIZE)
		return 0;

	va_start(ap, fmt);
	ret = vscnprintf(buf + at, PAGE_SIZE - at, fmt, ap);
	va_end(ap);
	return ret;
}

static inline int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vscnprintf(buf, size, fmt, ap);
	va_end(ap);
	return ret;
}

static inline unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	return strtoul(cp, endp, base);
}

static inline bool hid_compat_kstr_end_ok(const char *endp)
{
	return *endp == '\0' || (*endp == '\n' && endp[1] == '\0');
}

static inline int kstrtouint(const char *s, unsigned int base, unsigned int *res)
{
	char *endp;
	unsigned long long v;

	if (base > 16)
		return -EINVAL;
	if (*s == '+')
		s++;
	if (*s == '-' || *s == ' ' || (*s >= '\t' && *s <= '\r'))
		return -EINVAL;

	v = strtoull(s, &endp, base);
	if (endp == s || !hid_compat_kstr_end_ok(endp))
		return -EINVAL;
	if (v > (unsigned long long)~0u)
		return -ERANGE;

	*res = (unsigned int)v;
	return 0;
}

static inline int kstrtou8(const char *s, unsigned int base, u8 *res)
{
	unsigned int v;
	int ret = kstrtouint(s, base, &v);

	if (ret)
		return ret;
	if (v > U8_MAX)
		return -ERANGE;

	*res = (u8)v;
	return 0;
}

static inline int add_uevent_var(struct kobj_uevent_env *env, const char *fmt, ...)
{
	va_list args;
	int len;

	if (env->envp_idx >= ARRAY_SIZE(env->envp))
		return -ENOMEM;

	va_start(args, fmt);
	len = vsnprintf(&env->buf[env->buflen], sizeof(env->buf) - env->buflen, fmt, args);
	va_end(args);

	if (len < 0 || (size_t)len >= sizeof(env->buf) - env->buflen)
		return -ENOMEM;

	env->envp[env->envp_idx++] = &env->buf[env->buflen];
	env->buflen += len + 1;
	return 0;
}

static inline int kobject_uevent_env(struct kobject *kobj, enum kobject_action action, char *envp[])
{
	static const char * const actions[] = {
		[KOBJ_ADD] = "add",
		[KOBJ_REMOVE] = "remove",
		[KOBJ_CHANGE] = "change",
		[KOBJ_MOVE] = "move",
		[KOBJ_ONLINE] = "online",
		[KOBJ_OFFLINE] = "offline",
		[KOBJ_BIND] = "bind",
		[KOBJ_UNBIND] = "unbind",
	};
	struct kobj_uevent_env *env;
	struct device *dev = kobj_to_dev(kobj);
	int ret = 0;

	env = pvPortCalloc(1, sizeof(*env));
	if (!env)
		return -ENOMEM;

	if ((unsigned int)action < ARRAY_SIZE(actions) && actions[action])
		ret = add_uevent_var(env, "ACTION=%s", actions[action]);

	if (!ret && dev && dev->bus && dev->bus->uevent)
		ret = dev->bus->uevent(dev, env);

	if (!ret && envp)
		for (char **p = envp; *p; p++) {
			ret = add_uevent_var(env, "%s", *p);
			if (ret)
				break;
		}

	if (!ret && dev) {
		dev->uevent_count++;
		dev->last_uevent_action = action;
	}

	// Linux sends env through userspace/netlink here. This port only preserves
	// the upstream construction path; there is no userspace uevent sink.
	vPortFree(env);
	return ret;
}

static inline int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	return kobject_uevent_env(kobj, action, NULL);
}

static inline void dev_set_name(struct device *dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(dev->name, sizeof(dev->name), fmt, ap);
	va_end(ap);
}

static inline const char *dev_name(const struct device *dev)
{
	return dev->name;
}

struct devres_node {
	struct list_head list;
	void (*release)(struct devres_node *node);
};

static inline void *devres_open_group(struct device *dev, void *id, gfp_t flags)
{
	(void)id;
	(void)flags;
	return dev;
}

static inline void devres_release_group(struct device *dev, void *id)
{
	struct devres_node *node;

	(void)id;
	while (!list_empty(&dev->devres)) {
		node = list_entry(dev->devres.prev, struct devres_node, list);
		list_del(&node->list);
		if (node->release)
			node->release(node);
		else
			vPortFree(node);
	}
}

typedef void (*devm_action_fn)(void *);

struct devres_action {
	struct devres_node node;
	devm_action_fn action;
	void *data;
};

static inline void devres_action_release(struct devres_node *node)
{
	struct devres_action *res = container_of(node, struct devres_action, node);

	res->action(res->data);
	vPortFree(res);
}

static inline int devm_add_action_or_reset(struct device *dev, devm_action_fn action, void *data)
{
	struct devres_action *res = pvPortMalloc(sizeof(*res));

	if (!res) {
		action(data);
		return -ENOMEM;
	}

	res->node.release = devres_action_release;
	res->action = action;
	res->data = data;
	list_add_tail(&res->node.list, &dev->devres);
	return 0;
}

static inline int devm_release_action(struct device *dev, devm_action_fn action, void *data)
{
	struct devres_node *node;

	list_for_each_entry(node, &dev->devres, list) {
		struct devres_action *res;

		if (node->release != devres_action_release)
			continue;

		res = container_of(node, struct devres_action, node);
		if (res->action != action || res->data != data)
			continue;

		list_del(&node->list);
		devres_action_release(node);
		return 0;
	}

	return -ENOENT;
}

static inline int device_create_file(struct device *dev, const struct device_attribute *attr)
{
	struct device_attr_entry *entry = pvPortMalloc(sizeof(*entry));

	if (!entry)
		return -ENOMEM;

	entry->attr = attr;
	list_add_tail(&entry->node, &dev->attrs);
	return 0;
}

static inline void device_remove_file(struct device *dev, const struct device_attribute *attr)
{
	struct device_attr_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &dev->attrs, node)
		if (entry->attr == attr) {
			list_del(&entry->node);
			vPortFree(entry);
			return;
		}
}

static inline int device_create_bin_file(struct device *dev, const struct bin_attribute *attr)
{
	struct device_bin_attr_entry *entry = pvPortMalloc(sizeof(*entry));

	if (!entry)
		return -ENOMEM;

	entry->attr = attr;
	list_add_tail(&entry->node, &dev->bin_attrs);
	return 0;
}

static inline void device_remove_bin_file(struct device *dev, const struct bin_attribute *attr)
{
	struct device_bin_attr_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &dev->bin_attrs, node)
		if (entry->attr == attr) {
			list_del(&entry->node);
			vPortFree(entry);
			return;
		}
}

static inline int sysfs_create_bin_file(struct kobject *kobj, const struct bin_attribute *attr)
{
	return device_create_bin_file(kobj_to_dev(kobj), attr);
}

static inline void sysfs_remove_bin_file(struct kobject *kobj, const struct bin_attribute *attr)
{
	device_remove_bin_file(kobj_to_dev(kobj), attr);
}

static inline void sysfs_notify(struct kobject *kobj, const char *dir, const char *attr)
{
	struct device *dev = kobj_to_dev(kobj);

	dev->sysfs_notify_count++;
	dev->last_sysfs_notify_dir = dir;
	dev->last_sysfs_notify_attr = attr;
}

static inline int driver_create_file(struct device_driver *drv, const struct driver_attribute *attr)
{
	struct driver_attr_entry *entry = pvPortMalloc(sizeof(*entry));

	if (!entry)
		return -ENOMEM;

	entry->attr = attr;
	list_add_tail(&entry->node, &drv->attrs);
	return 0;
}

static inline void driver_remove_file(struct device_driver *drv, const struct driver_attribute *attr)
{
	struct driver_attr_entry *entry, *next;

	list_for_each_entry_safe(entry, next, &drv->attrs, node)
		if (entry->attr == attr) {
			list_del(&entry->node);
			vPortFree(entry);
			return;
		}
}

static inline int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp)
{
	struct device *dev = kobj_to_dev(kobj);
	struct attribute **attr;
	const struct bin_attribute **bin_attr;

	if (grp->attrs)
		for (attr = grp->attrs; *attr; attr++) {
			int n = (int)(attr - grp->attrs);
			int ret;

			if (grp->is_visible && !grp->is_visible(kobj, *attr, n))
				continue;

			ret = device_create_file(dev, container_of(*attr, struct device_attribute, attr));
			if (ret) {
				while (attr != grp->attrs) {
					attr--;
					device_remove_file(dev, container_of(*attr, struct device_attribute, attr));
				}
				return ret;
			}
		}

	if (grp->bin_attrs)
		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++) {
			int ret = device_create_bin_file(dev, *bin_attr);

			if (ret) {
				while (bin_attr != grp->bin_attrs) {
					bin_attr--;
					device_remove_bin_file(dev, *bin_attr);
				}
				if (grp->attrs)
					for (attr = grp->attrs; *attr; attr++)
						device_remove_file(dev, container_of(*attr, struct device_attribute, attr));
				return ret;
			}
		}

	return 0;
}

static inline void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp)
{
	struct device *dev = kobj_to_dev(kobj);
	struct attribute **attr;
	const struct bin_attribute **bin_attr;

	if (grp->attrs)
		for (attr = grp->attrs; *attr; attr++)
			device_remove_file(dev, container_of(*attr, struct device_attribute, attr));
	if (grp->bin_attrs)
		for (bin_attr = grp->bin_attrs; *bin_attr; bin_attr++)
			device_remove_bin_file(dev, *bin_attr);
}

static inline int driver_sysfs_create_group(struct device_driver *drv, const struct attribute_group *grp)
{
	struct attribute **attr;

	for (attr = grp->attrs; *attr; attr++) {
		int ret = driver_create_file(drv, container_of(*attr, struct driver_attribute, attr));

		if (ret) {
			while (attr != grp->attrs) {
				attr--;
				driver_remove_file(drv, container_of(*attr, struct driver_attribute, attr));
			}
			return ret;
		}
	}

	return 0;
}

static inline void driver_sysfs_remove_group(struct device_driver *drv, const struct attribute_group *grp)
{
	struct attribute **attr;

	for (attr = grp->attrs; *attr; attr++)
		driver_remove_file(drv, container_of(*attr, struct driver_attribute, attr));
}

static inline ssize_t device_attr_show(struct device *dev, const char *name, char *buf)
{
	struct device_attr_entry *entry;

	list_for_each_entry(entry, &dev->attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->show)
				return -EPERM;
			return entry->attr->show(dev, (struct device_attribute *)entry->attr, buf);
		}

	return -ENOENT;
}

static inline ssize_t device_attr_store(struct device *dev, const char *name, const char *buf, size_t count)
{
	struct device_attr_entry *entry;

	list_for_each_entry(entry, &dev->attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->store)
				return -EPERM;
			return entry->attr->store(dev, (struct device_attribute *)entry->attr, buf, count);
		}

	return -ENOENT;
}

static inline ssize_t device_bin_attr_read(struct device *dev, const char *name,
					   char *buf, loff_t off, size_t count)
{
	struct device_bin_attr_entry *entry;

	list_for_each_entry(entry, &dev->bin_attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->read)
				return -EPERM;
			return entry->attr->read(NULL, &dev->kobj, entry->attr, buf, off, count);
		}

	return -ENOENT;
}

static inline ssize_t device_bin_attr_write(struct device *dev, const char *name,
					    char *buf, loff_t off, size_t count)
{
	struct device_bin_attr_entry *entry;

	list_for_each_entry(entry, &dev->bin_attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->write)
				return -EPERM;
			return entry->attr->write(NULL, &dev->kobj, entry->attr, buf, off, count);
		}

	return -ENOENT;
}

static inline ssize_t driver_attr_show(struct device_driver *drv, const char *name, char *buf)
{
	struct driver_attr_entry *entry;

	list_for_each_entry(entry, &drv->attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->show)
				return -EPERM;
			return entry->attr->show(drv, buf);
		}

	return -ENOENT;
}

static inline ssize_t driver_attr_store(struct device_driver *drv, const char *name, const char *buf, size_t count)
{
	struct driver_attr_entry *entry;

	list_for_each_entry(entry, &drv->attrs, node)
		if (!strcmp(entry->attr->attr.name, name)) {
			if (!entry->attr->store)
				return -EPERM;
			return entry->attr->store(drv, buf, count);
		}

	return -ENOENT;
}

static inline int device_attr_for_each(struct device *dev, device_attr_iter_fn fn, void *data)
{
	struct device_attr_entry *entry;

	list_for_each_entry(entry, &dev->attrs, node) {
		int ret = fn(dev, entry->attr, data);

		if (ret)
			return ret;
	}

	return 0;
}

static inline int device_bin_attr_for_each(struct device *dev, device_bin_attr_iter_fn fn, void *data)
{
	struct device_bin_attr_entry *entry;

	list_for_each_entry(entry, &dev->bin_attrs, node) {
		int ret = fn(dev, entry->attr, data);

		if (ret)
			return ret;
	}

	return 0;
}

static inline int driver_attr_for_each(struct device_driver *drv,
				       int (*fn)(struct device_driver *drv,
						 const struct driver_attribute *attr,
						 void *data),
				       void *data)
{
	struct driver_attr_entry *entry;

	list_for_each_entry(entry, &drv->attrs, node) {
		int ret = fn(drv, entry->attr, data);

		if (ret)
			return ret;
	}

	return 0;
}

// Linux kmalloc(size, flags) compatibility shim.
static inline void *kmalloc(size_t size, int flags)
{
	(void)flags;
	return pvPortMalloc(size);
}

// Linux kzalloc(size, flags) compatibility shim.
static inline void *kzalloc(size_t size, int flags)
{
	(void)flags;
	return pvPortCalloc(1, size);
}

// Linux vzalloc(size) compatibility shim.
static inline void *vzalloc(size_t size)
{
	return kzalloc(size, GFP_KERNEL);
}

// Linux kvzalloc(size, flags) compatibility shim.
static inline void *kvzalloc(size_t size, int flags)
{
	return kzalloc(size, flags);
}

#define kzalloc_objs(obj, count) kzalloc(sizeof(obj) * (count), GFP_KERNEL)
#define kzalloc_obj(obj) kzalloc(sizeof(obj), GFP_KERNEL)
#define kzalloc_flex(obj, member, count) kzalloc(sizeof(obj) + sizeof((obj).member[0]) * (count), GFP_KERNEL)
#define kmalloc_obj(obj) kmalloc(sizeof(obj), GFP_KERNEL)

// Linux kmemdup(src, size, flags) compatibility shim.
static inline void *kmemdup(const void *src, size_t size, int flags)
{
	(void)flags;
	void *dst = pvPortMalloc(size);
	if (dst)
		memcpy(dst, src, size);
	return dst;
}

// Linux kfree(ptr) compatibility shim.
static inline void kfree(const void *ptr)
{
	vPortFree((void *)ptr);
}

static inline void *devm_kmalloc(struct device *dev, size_t size, gfp_t flags)
{
	struct devres_node *node;

	(void)flags;
	node = pvPortMalloc(sizeof(*node) + size);
	if (!node)
		return NULL;

	node->release = NULL;
	list_add_tail(&node->list, &dev->devres);
	return node + 1;
}

static inline void *devm_kzalloc(struct device *dev, size_t size, gfp_t flags)
{
	void *ptr = devm_kmalloc(dev, size, flags);

	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

static inline void *devm_kcalloc(struct device *dev, size_t n, size_t size, gfp_t flags)
{
	return devm_kzalloc(dev, n * size, flags);
}

static inline void *devm_kmemdup(struct device *dev, const void *src, size_t size, gfp_t flags)
{
	void *dst = devm_kmalloc(dev, size, flags);

	if (dst)
		memcpy(dst, src, size);
	return dst;
}

static inline void devm_kfree(struct device *dev, const void *ptr)
{
	struct devres_node *node = (struct devres_node *)ptr - 1;

	(void)dev;
	list_del(&node->list);
	vPortFree(node);
}

static inline void kvfree(const void *ptr)
{
	kfree(ptr);
}

static inline void kvfreep(void *ptr)
{
	kvfree(*(void **)ptr);
}

static inline void kfreep(void *ptr)
{
	kfree(*(void **)ptr);
}

#define __free(fn) __attribute__((cleanup(fn##p)))
#define no_free_ptr(ptr) ({ typeof(ptr) __ptr = (ptr); (ptr) = NULL; __ptr; })

static inline SemaphoreHandle_t hid_compat_mutex_handle(struct mutex *mutex)
{
        SemaphoreHandle_t handle;

        if (mutex->handle)
                return mutex->handle;

        // Linux DEFINE_MUTEX produces a ready static mutex. FreeRTOS mutexes need
        // runtime construction, so static mutexes are created on first use.
	        handle = xSemaphoreCreateMutex();
	        if (!handle) {
	                return NULL;
	        }

        taskENTER_CRITICAL();
        if (!mutex->handle) {
                mutex->handle = handle;
                handle = NULL;
        }
        taskEXIT_CRITICAL();

        if (handle)
                vSemaphoreDelete(handle);

        return mutex->handle;
}

static inline void mutex_init(struct mutex *mutex)
{
        mutex->handle = xSemaphoreCreateMutex();
        mutex->locked = 0;
}

static inline int mutex_lock_killable(struct mutex *mutex)
{
        SemaphoreHandle_t handle = hid_compat_mutex_handle(mutex);

        if (!handle)
                return -ENOMEM;

        xSemaphoreTake(handle, portMAX_DELAY);
        mutex->locked = 1;
        return 0;
}

static inline void mutex_lock(struct mutex *mutex)
{
        SemaphoreHandle_t handle = hid_compat_mutex_handle(mutex);

        if (!handle)
                return;

        xSemaphoreTake(handle, portMAX_DELAY);
        mutex->locked = 1;
}

static inline void mutex_unlock(struct mutex *mutex)
{
        SemaphoreHandle_t handle = mutex->handle;

        mutex->locked = 0;
        if (handle)
                xSemaphoreGive(handle);
}

static inline void mutex_destroy(struct mutex *mutex)
{
        SemaphoreHandle_t handle = mutex->handle;

        mutex->handle = NULL;
        mutex->locked = 0;
        if (handle)
                vSemaphoreDelete(handle);
}

static inline bool mutex_is_locked(struct mutex *mutex)
{
	return mutex->locked;
}

static inline int down_interruptible(struct semaphore *sem)
{
	return xSemaphoreTake(sem->handle, portMAX_DELAY) == pdPASS ? 0 : -EINTR;
}

static inline void down(struct semaphore *sem)
{
	xSemaphoreTake(sem->handle, portMAX_DELAY);
}

static inline int down_trylock(struct semaphore *sem)
{
	return xSemaphoreTake(sem->handle, 0) == pdPASS ? 0 : 1;
}

static inline void up(struct semaphore *sem)
{
	xSemaphoreGive(sem->handle);
}

static inline void sema_init(struct semaphore *sem, int val)
{
	sem->handle = xSemaphoreCreateCounting(val, val);
}

static inline void sema_destroy(struct semaphore *sem)
{
	vSemaphoreDelete(sem->handle);
}

static inline SemaphoreHandle_t hid_compat_spin_handle(spinlock_t *lock)
{
        SemaphoreHandle_t handle;

        if (lock->handle)
                return lock->handle;

	        handle = xSemaphoreCreateMutex();
	        if (!handle) {
	                abort();
	        }

        if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
                if (!lock->handle) {
                        lock->handle = handle;
                        handle = NULL;
                }
                if (handle)
                        vSemaphoreDelete(handle);
                return lock->handle;
        }

        taskENTER_CRITICAL();
        if (!lock->handle) {
                lock->handle = handle;
                handle = NULL;
        }
        taskEXIT_CRITICAL();

        if (handle)
                vSemaphoreDelete(handle);

        return lock->handle;
}

static inline void hid_compat_spin_lock_init(spinlock_t *lock)
{
	        lock->handle = xSemaphoreCreateMutex();
	        if (!lock->handle) {
	                abort();
	        }
}

static inline bool hid_compat_scheduler_started(void)
{
        return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

static inline void hid_compat_spin_lock(spinlock_t *lock)
{
        TickType_t wait = hid_compat_scheduler_started() ? portMAX_DELAY : 0;

	        if (xSemaphoreTake(hid_compat_spin_handle(lock), wait) != pdPASS) {
	                abort();
	        }
}

static inline void hid_compat_spin_unlock(spinlock_t *lock)
{
        xSemaphoreGive(lock->handle);
}

static inline unsigned long hid_compat_spin_lock_irqsave(spinlock_t *lock)
{
        hid_compat_spin_lock(lock);
        return 0;
}

static inline void hid_compat_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags)
{
        (void)flags;
        hid_compat_spin_unlock(lock);
}

#define DEFINE_SPINLOCK(name) spinlock_t name = { NULL }
#define spin_lock_init(lock) hid_compat_spin_lock_init(lock)
#define spin_lock(lock) hid_compat_spin_lock(lock)
#define spin_unlock(lock) hid_compat_spin_unlock(lock)
#define spin_lock_irq(lock) hid_compat_spin_lock(lock)
#define spin_unlock_irq(lock) hid_compat_spin_unlock(lock)
#define spin_lock_irqsave(lock, flags) do { (flags) = hid_compat_spin_lock_irqsave(lock); } while (0)
#define spin_unlock_irqrestore(lock, flags) hid_compat_spin_unlock_irqrestore(lock, flags)

static inline void init_waitqueue_head(wait_queue_head_t *wait)
{
	wait->task = NULL;
	wait->sequence = 0;
}

static inline void wake_up_interruptible(wait_queue_head_t *wait)
{
	wait->sequence++;
	if (wait->task)
		xTaskNotifyGive(wait->task);
}

static inline void wake_up(wait_queue_head_t *wait)
{
	wake_up_interruptible(wait);
}

static inline void hid_compat_wait_until(TickType_t deadline)
{
	TickType_t now = xTaskGetTickCount();

	if (time_before((unsigned long)now, (unsigned long)deadline))
		ulTaskNotifyTake(pdTRUE, deadline - now);
}

static inline void hid_compat_usb_host_delay(unsigned int msecs)
{
	vTaskDelay(pdMS_TO_TICKS(msecs));
}

	#define wait_event_timeout(wq, condition, timeout) \
		({ \
			TickType_t __timeout = (TickType_t)(timeout); \
			TickType_t __deadline = xTaskGetTickCount() + __timeout; \
			wait_queue_head_t *__wait = &(wq); \
			TaskHandle_t __task = xTaskGetCurrentTaskHandle(); \
			long __ret = 0; \
			__wait->task = __task; \
			if (condition) { \
				__ret = __timeout ? (long)__timeout : 1; \
			} else { \
				while (time_before((unsigned long)xTaskGetTickCount(), (unsigned long)__deadline)) { \
					hid_compat_wait_until(__deadline); \
					if (condition) { \
						TickType_t __now = xTaskGetTickCount(); \
						__ret = time_before((unsigned long)__now, (unsigned long)__deadline) ? \
							(long)(__deadline - __now) : 1; \
						break; \
					} \
				} \
				if (!__ret && (condition)) \
					__ret = 1; \
			} \
			if (__wait->task == __task) \
				__wait->task = NULL; \
			__ret; \
		})

#define wait_event_interruptible_timeout(wq, condition, timeout) \
	wait_event_timeout((wq), (condition), (timeout))

static inline void get_random_bytes(void *buf, size_t len)
{
	uint8_t *bytes = buf;
	uint32_t word = 0;
	unsigned int left = 0;

	for (size_t i = 0; i < len; i++) {
		if (!left) {
			word = get_rand_32();
			left = sizeof(word);
		}
		bytes[i] = (uint8_t)word;
		word >>= 8;
		left--;
	}
}

#define SIGIO 29
#define POLL_IN 1
#define EPOLLIN 0x00000001u
#define EPOLLOUT 0x00000004u
#define EPOLLERR 0x00000008u
#define EPOLLHUP 0x00000010u
#define EPOLLRDNORM 0x00000040u
#define EPOLLWRNORM 0x00000100u

static inline void kill_fasync(struct fasync_struct **fasync, int sig, int band)
{
	(void)fasync;
	(void)sig;
	(void)band;
}

static inline void kref_init(struct kref *kref)
{
	kref->refcount = 1;
}

static inline void kref_get(struct kref *kref)
{
	kref->refcount++;
}

static inline void kref_put(struct kref *kref, void (*release)(struct kref *kref))
{
	kref->refcount--;
	if (!kref->refcount)
		release(kref);
}

static inline void hid_debug_init(void)
{
}

static inline void hid_debug_exit(void)
{
}

static inline void hid_debug_register(void *hdev, const char *name)
{
	(void)hdev;
	(void)name;
}

static inline void hid_debug_unregister(void *hdev)
{
	(void)hdev;
}

// Linux kasprintf(flags, fmt, ...) compatibility shim.
static inline char *kasprintf(gfp_t flags, const char *fmt, ...)
{
	(void)flags;
	va_list ap;
	va_list ap_copy;
	va_start(ap, fmt);
	va_copy(ap_copy, ap);
	int len = vsnprintf(NULL, 0, fmt, ap_copy);
	va_end(ap_copy);
	if (len < 0) {
		va_end(ap);
		return NULL;
	}

	char *s = pvPortMalloc((size_t)len + 1u);
	if (s)
		vsnprintf(s, (size_t)len + 1u, fmt, ap);
	va_end(ap);
	return s;
}

static inline ktime_t ktime_get_coarse(void);

static inline ktime_t ktime_get_coarse(void)
{
	return (ktime_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static inline ktime_t ktime_add_ms(ktime_t kt, unsigned int ms)
{
	return kt + (ktime_t)ms;
}

static inline bool ktime_after(ktime_t a, ktime_t b)
{
	return a > b;
}

static inline char *devm_kasprintf(struct device *dev, gfp_t flags, const char *fmt, ...)
{
	va_list ap;
	va_list ap_copy;
	int len;
	char *s;

	(void)flags;
	va_start(ap, fmt);
	va_copy(ap_copy, ap);
	len = vsnprintf(NULL, 0, fmt, ap_copy);
	va_end(ap_copy);
	if (len < 0) {
		va_end(ap);
		return NULL;
	}

	s = devm_kmalloc(dev, (size_t)len + 1u, GFP_KERNEL);
	if (s)
		vsnprintf(s, (size_t)len + 1u, fmt, ap);
	va_end(ap);
	return s;
}

// Linux get_unaligned_le16(); local copy avoids pulling Linux unaligned headers.
static inline u16 get_unaligned_le16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

// Linux get_unaligned_be16(); local copy avoids pulling Linux unaligned headers.
static inline u16 get_unaligned_be16(const u8 *p)
{
	return ((u16)p[0] << 8) | (u16)p[1];
}

// Linux get_unaligned_le32(); local copy avoids pulling Linux unaligned headers.
static inline u32 get_unaligned_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// Linux sign_extend32(); local copy avoids pulling bitops headers.
static inline s32 sign_extend32(u32 value, unsigned int index)
{
	u32 sign = 1u << index;
	return (s32)((value ^ sign) - sign);
}

// Minimal local copy of Linux bitops used by hid-input.
static inline void set_bit(unsigned int nr, unsigned long *addr)
{
	taskENTER_CRITICAL();
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
	taskEXIT_CRITICAL();
}

// Minimal local copy of Linux bitops used by hid-input.
static inline void __set_bit(unsigned int nr, unsigned long *addr)
{
	set_bit(nr, addr);
}

// Minimal local copy of Linux bitops used by hid-input.
static inline void clear_bit(unsigned int nr, unsigned long *addr)
{
	taskENTER_CRITICAL();
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
	taskEXIT_CRITICAL();
}

// Minimal local copy of Linux bitops used by hid-input.
static inline void __clear_bit(unsigned int nr, unsigned long *addr)
{
	clear_bit(nr, addr);
}

// Minimal local copy of Linux bitops used by hid-input.
static inline int test_bit(unsigned int nr, const unsigned long *addr)
{
	return !!(addr[BIT_WORD(nr)] & BIT_MASK(nr));
}

// Minimal local copy of Linux bitops used by hid-input.
static inline int test_and_set_bit(unsigned int nr, unsigned long *addr)
{
	int old;

	taskENTER_CRITICAL();
	old = !!(addr[BIT_WORD(nr)] & BIT_MASK(nr));
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
	taskEXIT_CRITICAL();
	return old;
}

static inline int __test_and_set_bit(unsigned int nr, unsigned long *addr)
{
	return test_and_set_bit(nr, addr);
}

// Minimal local copy of Linux bitops used by work flags.
static inline int test_and_clear_bit(unsigned int nr, unsigned long *addr)
{
	int old;

	taskENTER_CRITICAL();
	old = !!(addr[BIT_WORD(nr)] & BIT_MASK(nr));
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
	taskEXIT_CRITICAL();
	return old;
}

static inline int __test_and_clear_bit(unsigned int nr, unsigned long *addr)
{
	return test_and_clear_bit(nr, addr);
}

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = 0; (bit) < (size); (bit)++) \
		if (test_bit((bit), (addr)))

// Minimal local copy of Linux bitmap_subset() behavior used by input device id matching.
static inline int bitmap_subset(const unsigned long *src1, const unsigned long *src2, unsigned int nbits)
{
	for (unsigned int i = 0; i < nbits; i++)
		if (test_bit(i, src1) && !test_bit(i, src2))
			return 0;

	return 1;
}

static inline void timer_setup(struct timer_list *timer, void (*function)(struct timer_list *), unsigned int flags)
{
	(void)flags;
	timer->function = function;
	timer->expires = 0;
	timer->pending = 0;
	timer->running = 0;
	timer->next = NULL;
}
int mod_timer(struct timer_list *timer, unsigned long expires);
int timer_delete_sync(struct timer_list *timer);
#define timer_delete(timer) timer_delete_sync(timer)

#define jiffies ((unsigned long)xTaskGetTickCount())
#define secs_to_jiffies(sec) ((unsigned long)(sec) * (unsigned long)configTICK_RATE_HZ)
#define msecs_to_jiffies(ms) ((unsigned long)(((uint64_t)(ms) * configTICK_RATE_HZ + 999u) / 1000u))
#define jiffies_to_msecs(j) ((unsigned int)(((uint64_t)(j) * 1000u) / configTICK_RATE_HZ))
#define timer_container_of(var, timer, member) container_of(timer, typeof(*var), member)
#define le16_to_cpu(x) (x)

static inline ssize_t strscpy(char *dest, const char *src, size_t count)
{
	size_t len;

	if (!count)
		return -E2BIG;

	len = strlen(src);
	if (len >= count) {
		memcpy(dest, src, count - 1u);
		dest[count - 1u] = '\0';
		return -E2BIG;
	}

	memcpy(dest, src, len + 1u);
	return (ssize_t)len;
}

// Minimal local copy of Linux find_next_zero_bit() used by duplicate HID usage handling.
static inline unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long offset)
{
	while (offset < size && test_bit(offset, addr))
		offset++;

	return offset;
}

#endif
