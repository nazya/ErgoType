#ifndef USB_HOST_LINUX_HID_COMPAT_H
#define USB_HOST_LINUX_HID_COMPAT_H

#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <linux/types.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "semphr.h"
#include "task.h"
#include "tusb.h"
#include "log.h"

struct hid_device;

typedef int gfp_t;
typedef int pm_message_t;
typedef struct {
        uint8_t locked;
} spinlock_t;
typedef struct hid_compat_wait_queue {
	TaskHandle_t task;
	volatile int cancel_status;
	struct hid_compat_wait_queue *transport_next;
} wait_queue_head_t;
/* Firmware ktime_t is modulo-2^32 milliseconds; wrap ordering is not retained. */
typedef uint32_t ktime_t;
typedef long loff_t;

/*
 * Firmware-selected asynchronous host slice: keep Linux
 * descriptor/report/input parsing and enable only drivers whose transport and
 * subsystem dependencies are implemented by the port.
 */
/*
 * Upstream gets HID driver-selection symbols from generated autoconf.h.
 * This port mirrors the HID drivers currently linked from CMake.
 */
#define CONFIG_HID_GENERIC 1
#define CONFIG_HID_A4TECH 1
#define CONFIG_HID_CHICONY 1
#define CONFIG_HID_CREATIVE_SB0540 1
#define CONFIG_HID_CYPRESS 1
#define CONFIG_HID_ELECOM 1
#define CONFIG_HID_EVISION 1
#define CONFIG_HID_HOLTEK 1
#define CONFIG_HID_ITE 1
#define CONFIG_HID_KENSINGTON 1
#define CONFIG_HID_KYE 1
#define CONFIG_HID_PRIMAX 1
#define CONFIG_HID_PXRC 1
#define CONFIG_HID_RAPOO 1
#define CONFIG_HID_RAZER 1
#define CONFIG_HID_SAITEK 1
#define CONFIG_HID_TOPRE 1
#define CONFIG_HID_UCLOGIC 1
#define CONFIG_HID_ZYDACRON 1
#define CONFIG_HID_HAPTIC 1
#define CONFIG_HID_MULTITOUCH 1
#define CONFIG_HID_MAGICMOUSE 1
#define CONFIG_HID_LOGITECH_HIDPP 1
#define CONFIG_HID_LOGITECH_DJ 1
// #define CONFIG_HID_LOGITECH_DJ_ALL_RECEIVERS 1
// The current hardware stage enables only the covered 046d:c52b receiver.
/*
 * Direct-USB HID++ is enabled in measured stages. Request/reply remains the
 * transport umbrella, while identity opens only upstream pre-connect name and
 * unit-ID discovery. Broader capabilities and device families stay gated until
 * each has an independent hardware pass.
 */
#define CONFIG_HID_LOGITECH_HIDPP_DIRECT_REQUEST_REPLY 1
#define CONFIG_HID_LOGITECH_HIDPP_DIRECT_IDENTITY 1
#define CONFIG_HID_LOGITECH_HIDPP_DIRECT_BATTERY 1
// The selected DJ stage enables the upstream M705 HID++ 1.0 wheel path.
#define CONFIG_HID_LOGITECH_HIDPP_DJ_HI_RES_SCROLL_1P0 1
/*
 * The practical Unifying stage opens only the exact upstream M560, T650,
 * K400, and K750 child classes. Broad DJ matching, Bluetooth, legacy proxy
 * devices, and force feedback remain outside this stage.
 */
#define CONFIG_HID_LOGITECH_HIDPP_DJ_DEVICE_CLASSES 1
// Keep the complete pinned hid-uclogic device table in source while the active
// stages match Huion 256c:006d/006e and selected modern XP-Pen UGEE-v2 IDs.
// #define CONFIG_HID_UCLOGIC_ALL_DEVICES 1

// #define CONFIG_USB_HIDDEV 1
// Firmware has hiddev proxy code in tree, but no enabled hiddev consumer path.
#define CONFIG_HID_BATTERY_STRENGTH 1
// Generic HID battery reports use the reduced value-only firmware power_supply
// boundary; sysfs, VFS, uevents, and UI presentation remain outside the port.
// #define CONFIG_HOLTEK_FF 1
// Holtek force-feedback support is not linked/tested; only the keyboard and
// mouse descriptor-fixup drivers are enabled for this family.
// #define CONFIG_LEDS_CLASS 1
// Linux LED class proxy is deferred.
// #define CONFIG_BACKLIGHT_CLASS_DEVICE 1
// Linux backlight class proxy is deferred.
// #define CONFIG_HID_PID 1
// PID force-feedback transport is deferred.
// The standard HID Haptics Page helper is enabled above; PID and unrelated
// force-feedback driver stacks remain deferred.

/*
 * These optional drivers and driver features stay disabled until their source
 * and required subsystem proxy are both selected and tested.
 */
// #define CONFIG_DRAGONRISE_FF 1
// #define CONFIG_GREENASIA_FF 1
// #define CONFIG_HID_NTRIG 1
// #define CONFIG_HID_ACRUX_FF 1
// #define CONFIG_HID_STEELSERIES 1
// #define CONFIG_LOGIG940_FF 1
// #define CONFIG_LOGIRUMBLEPAD2_FF 1
// #define CONFIG_LOGITECH_FF 1
// #define CONFIG_NVIDIA_SHIELD_FF 1
// #define CONFIG_LOGIWHEELS_FF 1
// #define CONFIG_HID_MEGAWORLD_FF 1
// #define CONFIG_PANTHERLORD_FF 1
// #define CONFIG_SMARTJOYPLUS_FF 1
// #define CONFIG_HID_THRUSTMASTER 1
// #define CONFIG_THRUSTMASTER_FF 1
// #define CONFIG_ZEROPLUS_FF 1
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
	/* Linked firmware owners use the ordinary positive-reference contract. */
	unsigned int refcount;
};

struct mutex {
	SemaphoreHandle_t handle;
};

/*
 * PORTING DEBT: this is only zero storage, not Linux's usable static mutex.
 * No linked caller locks a DEFINE_MUTEX object; require explicit mutex_init()
 * or compile-gate the caller before enabling one.
 */
#define DEFINE_MUTEX(name) struct mutex name = { 0 }

struct semaphore {
	SemaphoreHandle_t handle;
	TaskHandle_t owner;
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
	/* Concurrent cancel_work_sync() callers must all retain the queue gate. */
	uint8_t cancel_depth;
	struct workqueue_struct *wq;
	struct work_struct *next;
};

struct timer_list {
	void (*function)(struct timer_list *timer);
	unsigned long expires;
	uint8_t pending;
	uint8_t running;
	struct timer_list *next;
};

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;
	struct workqueue_struct *wq;
	uint8_t delayed_pending;
};

#define INIT_WORK(work, fn) do { (work)->func = (fn); (work)->pending = 0; (work)->running = 0; (work)->cancel_depth = 0; (work)->wq = NULL; (work)->next = NULL; } while (0)
/*
 * Delayed work is intentionally a compile-time port boundary. The former
 * split timer/workqueue bridge could arm its timer after a synchronous cancel
 * had already returned. No linked driver uses delayed_work; keep future use
 * loud until deadlines are owned by the workqueue task itself.
 */
void hid_delayed_work_not_supported(void)
	__attribute__((error("delayed_work needs the unified firmware workqueue deadline bridge")));
#define INIT_DELAYED_WORK(dwork, fn) do { hid_delayed_work_not_supported(); } while (0)
// Upstream deferrable work can skip wakeups for idle CPUs. Firmware has no
// delayed-work bridge yet, so it retains the same explicit compile-time gate.
#define INIT_DEFERRABLE_WORK(dwork, fn) INIT_DELAYED_WORK(dwork, fn)
#define to_delayed_work(work) container_of(work, struct delayed_work, work)
extern struct workqueue_struct *system_wq;
int hid_workqueue_init(void);
void hid_workqueue_task(void *pvParameters);
bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
bool schedule_work(struct work_struct *work);
bool flush_work(struct work_struct *work);
bool cancel_work_sync(struct work_struct *work);
struct workqueue_struct *create_singlethread_workqueue(const char *name);
void destroy_workqueue(struct workqueue_struct *wq);
bool queue_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
			unsigned long delay)
	__attribute__((error("delayed_work needs the unified firmware workqueue deadline bridge")));
bool schedule_delayed_work(struct delayed_work *dwork, unsigned long delay)
	__attribute__((error("delayed_work needs the unified firmware workqueue deadline bridge")));
bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
		      unsigned long delay)
	__attribute__((error("delayed_work needs the unified firmware workqueue deadline bridge")));
bool cancel_delayed_work_sync(struct delayed_work *dwork)
	__attribute__((error("delayed_work needs the unified firmware workqueue deadline bridge")));
int hid_timer_init(void);
void hid_timer_task(void *pvParameters);

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
#define EINPROGRESS 115
#define EREMOTEIO 121
#define ETIMEDOUT 110
#define E2BIG 7
#define EEXIST 17
#define EMSGSIZE 90
// #define ECANCELED 125 /* Operation Canceled */
// Keep the upstream asm-generic errno for versioned firmware cancellation.
#define ECANCELED 125

#define BIT(n) (1u << (n))
#define BIT_ULL(n) (1ULL << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min3(x, y, z) min(min((x), (y)), (z))
// Linux flexible-array allocation helper; keep upstream driver allocation
// expressions such as struct_size(data, leds, n) unchanged.
#define struct_size(p, member, count) (sizeof(*(p)) + sizeof((p)->member[0]) * (count))
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
#define MODULE_INFO(tag, info)
#define MODULE_LICENSE(license)
#define MODULE_SOFTDEP(dep)
#define module_param(name, type, perm)
#define module_param_named(name, value, type, perm)
#define module_param_array_named(name, array, type, nump, perm)
#define module_param_cb(name, ops, arg, perm)
// Linux module parameters are not writable runtime knobs in firmware.
#define module_param_call(name, set, get, arg, perm)
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
struct kernel_param {
	void *arg;
};
struct kernel_param_ops {
	int (*set)(const char *val, const struct kernel_param *kp);
	int (*get)(char *buffer, const struct kernel_param *kp);
};
static inline int param_set_bool(const char *val, const struct kernel_param *kp)
{
	bool value;

	if (!strcmp(val, "1") || !strcmp(val, "y") || !strcmp(val, "Y") ||
	    !strcmp(val, "true")) {
		value = true;
	} else if (!strcmp(val, "0") || !strcmp(val, "n") ||
		   !strcmp(val, "N") || !strcmp(val, "false")) {
		value = false;
	} else {
		return -EINVAL;
	}

	*(bool *)kp->arg = value;
	return 0;
}
static inline int param_get_bool(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%c", *(bool *)kp->arg ? 'Y' : 'N');
}
static inline int param_get_uint(char *buffer, const struct kernel_param *kp)
{
	return sprintf(buffer, "%u", *(unsigned int *)kp->arg);
}
static inline bool try_module_get(struct module *module)
{
	// try_module_get(THIS_MODULE) pins a Linux module; firmware never unloads.
	(void)module;
	return true;
}
static inline void module_put(struct module *module)
{
	// module_put(THIS_MODULE) drops a Linux module ref; firmware never unloads.
	(void)module;
}
#define U8_MAX ((u8)~0U)
#define S16_MAX INT16_MAX
#define S16_MIN INT16_MIN
#define U16_MAX ((u16)~0U)
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#define WARN_ON(x) (x)
/*
 * Linux BUG_ON() always evaluates its condition. No linked caller needs a
 * release-build fatal policy, so keep the debug assertion without inventing a
 * silent busy-loop. Evaluate outside configASSERT() so NDEBUG cannot erase it.
 */
#define BUG_ON(x) \
	do { \
		bool __hid_bug_ok = !(x); \
		configASSERT(__hid_bug_ok); \
		(void)__hid_bug_ok; \
	} while (0)
#define unlikely(x) (x)
#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#define IS_ERR(ptr) ((uintptr_t)(ptr) >= (uintptr_t)-4095)
#define PTR_ERR(ptr) ((long)(ptr))
#define ERR_PTR(err) ((void *)(intptr_t)(err))
// Imported Linux HID code now runs in firmware tasks, but this compatibility
// layer has no printk sink and generic formatted logging would inflate every
// derived call site's stack. Selected boundaries publish bounded diagnostics
// from their owning glue task instead.
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
#define dev_err_probe(dev, err, fmt, ...) ((void)(dev), (err))
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
// #define pm_ptr(_ptr) PTR_IF(IS_ENABLED(CONFIG_PM), (_ptr))
// Firmware has no power-management core and leaves CONFIG_PM disabled, so the
// upstream expression folds to NULL without requiring the full Kconfig macro
// expansion machinery in this reduced compatibility header.
#define pm_ptr(ptr) NULL
#define max_t(type, x, y) ((type)(x) > (type)(y) ? (type)(x) : (type)(y))
#define min_t(type, x, y) ((type)(x) < (type)(y) ? (type)(x) : (type)(y))
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define DIV_ROUND_CLOSEST(x, divisor) (((x) + ((divisor) / 2)) / (divisor))
#define mult_frac(x, numer, denom) ((x) * (numer) / (denom))
#define clamp(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))
/*
 * PORTING DEBT: unlike Linux array3_size(), this does not saturate on
 * overflow. Its sole linked caller grows HID collections from a report
 * descriptor capped at HID_MAX_DESCRIPTOR_SIZE (4096), so the current
 * product is bounded; audit or replace this helper before adding callers.
 */
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
#define __ATTR(_name, _mode, _show, _store) \
	{ .attr = { .name = #_name, .mode = (_mode) }, .show = (_show), .store = (_store) }
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
#define list_last_entry(ptr, type, member) list_entry((ptr)->prev, type, member)
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, typeof(*pos), member); &pos->member != (head); pos = list_entry(pos->member.next, typeof(*pos), member))
#define list_is_last(list, head) ((list)->next == (head))
#define list_is_singular(head) (!list_empty(head) && (head)->next == (head)->prev)
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
	void *private;
	ssize_t (*read)(struct file *filp, struct kobject *kobj,
			const struct bin_attribute *attr,
			char *buf, loff_t off, size_t count);
	ssize_t (*write)(struct file *filp, struct kobject *kobj,
			 const struct bin_attribute *attr,
			 char *buf, loff_t off, size_t count);
};

struct attribute_group {
	const char *name;
	struct attribute * const *attrs;
	umode_t (*is_visible)(struct kobject *kobj, struct attribute *attr, int n);
	const struct bin_attribute * const *bin_attrs;
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

struct device_type {
	const char *name;
};

struct bus_type {
	const char *name;
	const struct attribute_group * const *dev_groups;
	const struct attribute_group * const *drv_groups;
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
	const struct attribute_group * const *dev_groups;
	const struct hid_driver *hid_driver;
	struct list_head bus_node;
};

struct class {
	const char *name;
	const struct attribute_group * const *dev_groups;
};

struct device {
	const struct bus_type *bus;
	struct device *parent;
	const struct device_type *type;
	struct device_driver *driver;
	void (*release)(struct device *dev);
	void *data;
	struct kobject kobj;
	struct list_head bus_node;
	struct list_head devres;
	unsigned int uevent_count;
	enum kobject_action last_uevent_action;
	bool wakeup_enabled;
	unsigned int refcount;
	char name[32];
};

struct device_node {
	int unused;
};

#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = { .attr = { .name = #_name, .mode = S_IRUSR }, .show = _name##_show }
#define __BIN_ATTR(_name, _mode, _read, _write, _size) { \
	.attr = { .name = #_name, .mode = (_mode) }, \
	.read = (_read), \
	.write = (_write), \
	.size = (_size), \
}
#define BIN_ATTR(_name, _mode, _read, _write, _size) \
	struct bin_attribute bin_attr_##_name = __BIN_ATTR(_name, _mode, _read, _write, _size)
#define BIN_ATTR_RO(_name, _size) \
	struct bin_attribute bin_attr_##_name = __BIN_ATTR(_name, S_IRUSR, _name##_read, NULL, _size)
#define DRIVER_ATTR_WO(_name) \
	struct driver_attribute driver_attr_##_name = { .attr = { .name = #_name, .mode = S_IWUSR }, .store = _name##_store }
#define ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group _name##_group = { .attrs = _name##_attrs }; \
	static const struct attribute_group * const _name##_groups[] = { &_name##_group, NULL }
#define __ATTRIBUTE_GROUPS(_name) \
	static const struct attribute_group * const _name##_groups[] = { &_name##_group, NULL }

static inline int atomic_inc_return(atomic_t *v)
{
	/*
	 * Linux atomic_inc_return() is a fully ordered, non-sleeping RMW.
	 * GCC atomics keep that contract without taking FreeRTOS's global task
	 * critical locks. Pico supplies the RP2040 implementation; the same
	 * compiler interface remains usable by the future ESP port.
	 */
	return __atomic_fetch_add(v, 1, __ATOMIC_SEQ_CST) + 1;
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
static inline int device_sysfs_create_groups(struct device *dev, const struct attribute_group * const *groups);
static inline void device_sysfs_remove_groups(struct device *dev, const struct attribute_group * const *groups);
static inline int kobject_uevent(struct kobject *kobj, enum kobject_action action);

static inline int device_probe(struct device *dev)
{
	const struct bus_type *bus = dev->bus;
	struct bus_type *b = (struct bus_type *)bus;
	struct device_driver *drv;
	// Linux driver core returns the probe error to its caller. Keep parser OOM
	// visible through this local shim so lifecycle rejects the partial HID.
	int error = -ENODEV;

	list_for_each_entry(drv, &b->drivers, bus_node) {
		if (bus->match && !bus->match(dev, drv))
			continue;

		dev->driver = drv;
		// if (!bus->probe || bus->probe(dev) == 0) {
		// Call probe once and retain ENOMEM if no matching driver binds.
		int probe_ret = bus->probe ? bus->probe(dev) : 0;
		if (!probe_ret) {
			/* Preserve Linux's publication point; firmware owns no sysfs tree. */
			(void)device_sysfs_create_groups(dev, drv->dev_groups);
			kobject_uevent(&dev->kobj, KOBJ_BIND);
			return 0;
		}
		if (probe_ret == -ENOMEM)
			error = -ENOMEM;
		dev->driver = NULL;
	}

	// return -ENODEV;
	// Preserve parser OOM for hid_add_device() and task-side usbhid_probe().
	return error;
}

static inline int device_sysfs_create_groups(struct device *dev, const struct attribute_group * const *groups)
{
	/* Linux publishes these groups; firmware has no sysfs object tree. */
	(void)dev;
	(void)groups;
	return 0;
}

static inline void device_sysfs_remove_groups(struct device *dev, const struct attribute_group * const *groups)
{
	/* No groups were published by device_sysfs_create_groups(). */
	(void)dev;
	(void)groups;
}

static inline void device_sysfs_remove_all(struct device *dev)
{
	/* Firmware publishes no sysfs objects, so there is no registry to drain. */
	(void)dev;
}

static inline int device_add(struct device *dev)
{
	struct bus_type *bus = (struct bus_type *)dev->bus;
	int ret;

	INIT_LIST_HEAD(&dev->bus_node);
	list_add_tail(&dev->bus_node, &bus->devices);

	/* Preserve Linux's publication point; firmware owns no sysfs tree. */
	(void)device_sysfs_create_groups(dev, bus->dev_groups);

	// if (device_probe(dev) < 0) {
	// 	list_del(&dev->bus_node);
	// 	return -ENODEV;
	// }
	// Linux device_add() registers the device even when no driver binds yet;
	// later driver_register()/bus_rescan_devices() can probe it again.
	kobject_uevent(&dev->kobj, KOBJ_ADD);
	// bus_probe_device(dev);
	// Firmware registers all linked HID drivers before TinyUSB enumeration,
	// so a synchronous probe failure has no later module bind path to recover.
	ret = device_probe(dev);
	if (ret < 0) {
		device_sysfs_remove_groups(dev, bus->dev_groups);
		// Linux driver core unwinds attributes created during a failed bind.
		// This reduced device-model shim owns that probe state directly.
		device_sysfs_remove_all(dev);
		list_del(&dev->bus_node);
		return ret;
	}

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
	const struct attribute_group * const *group;

	INIT_LIST_HEAD(&drv->bus_node);
	list_add_tail(&drv->bus_node, &bus->drivers);

	if (bus->drv_groups)
		for (group = bus->drv_groups; *group; group++)
			(void)driver_sysfs_create_group(drv, *group);

	// Linux driver_register() reaches driver_attach() through the driver core.
	driver_attach(drv);
	return 0;
}

static inline int class_register(const struct class *class)
{
	(void)class;
	return 0;
}

static inline void class_unregister(const struct class *class)
{
	(void)class;
}

static inline void driver_unregister(struct device_driver *drv)
{
	struct bus_type *bus = (struct bus_type *)drv->bus;
	const struct attribute_group * const *group;
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
		if (dev->driver)
			continue;
		if (drv->bus->match && !drv->bus->match(dev, drv))
			continue;
		dev->driver = drv;
		if (drv->bus->probe && drv->bus->probe(dev) < 0) {
			dev->driver = NULL;
			continue;
		}
		/* Preserve Linux's publication point; firmware owns no sysfs tree. */
		(void)device_sysfs_create_groups(dev, drv->dev_groups);
		kobject_uevent(&dev->kobj, KOBJ_BIND);
	}

	return 0;
}

static inline void device_initialize(struct device *dev)
{
	dev->kobj.dev = dev;
	INIT_LIST_HEAD(&dev->bus_node);
	INIT_LIST_HEAD(&dev->devres);
	dev->uevent_count = 0;
	dev->last_uevent_action = KOBJ_ADD;
	dev->wakeup_enabled = false;
	__atomic_store_n(&dev->refcount, 1u, __ATOMIC_RELAXED);
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
	unsigned int refs;

	if (!dev)
		return;
	/*
	 * Linked callers already own a positive reference. Preserve Linux's final
	 * release edge without importing refcount_t saturation/diagnostic policy.
	 */
	refs = __atomic_fetch_sub(&dev->refcount, 1u, __ATOMIC_RELEASE);
	if (refs != 1u)
		return;

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	if (dev->release)
		dev->release(dev);
}

static inline struct device *get_device(struct device *dev)
{
	/*
	 * Like Linux get_device(), this requires an existing owned reference. The
	 * linked haptic probe acquires it before work publication and teardown.
	 */
	if (!dev)
		return NULL;
	(void)__atomic_fetch_add(&dev->refcount, 1u, __ATOMIC_RELAXED);
	return dev;
}

static inline void device_enable_async_suspend(struct device *dev)
{
	/*
	 * Linux only uses this flag to let the PM core schedule this device's
	 * suspend/resume asynchronously. Firmware has no PM core and leaves
	 * CONFIG_PM disabled, so there is no scheduler-visible state to retain.
	 */
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

static inline int kstrtoul(const char *s, unsigned int base, unsigned long *res)
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
	if (v > ULONG_MAX)
		return -ERANGE;

	*res = (unsigned long)v;
	return 0;
}

static inline int kstrtoint(const char *s, unsigned int base, int *res)
{
	char *endp;
	long long v;

	if (base > 16)
		return -EINVAL;
	if (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		return -EINVAL;

	v = strtoll(s, &endp, base);
	if (endp == s || !hid_compat_kstr_end_ok(endp))
		return -EINVAL;
	if (v < INT_MIN || v > INT_MAX)
		return -ERANGE;

	*res = (int)v;
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
	struct device *dev = kobj_to_dev(kobj);

	// Linux constructs an environment for its userspace/netlink sink here.
	// Firmware has no such consumer; retain lifecycle observability without a
	// transient 2.3 KiB allocation that would only be discarded immediately.
	if (dev) {
		dev->uevent_count++;
		dev->last_uevent_action = action;
	}
	return 0;
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
	/*
	 * Linux publishes this attribute through sysfs. Firmware has no sysfs or
	 * internal attribute consumer, so retain the driver's call and report
	 * successful publication without allocating a dead registry node.
	 */
	(void)dev;
	(void)attr;
	return 0;
}

static inline void device_remove_file(struct device *dev, const struct device_attribute *attr)
{
	/* No sysfs object was published by device_create_file(). */
	(void)dev;
	(void)attr;
}

static inline int device_create_bin_file(struct device *dev, const struct bin_attribute *attr)
{
	/* See device_create_file(): binary attributes have no firmware consumer. */
	(void)dev;
	(void)attr;
	return 0;
}

static inline void device_remove_bin_file(struct device *dev, const struct bin_attribute *attr)
{
	/* No sysfs object was published by device_create_bin_file(). */
	(void)dev;
	(void)attr;
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
	/* Linux wakes sysfs pollers; firmware has no attribute reader to wake. */
	(void)kobj;
	(void)dir;
	(void)attr;
}

static inline int driver_create_file(struct device_driver *drv, const struct driver_attribute *attr)
{
	/* Firmware has no driver-attribute proxy; retain registration as a no-op. */
	(void)drv;
	(void)attr;
	return 0;
}

static inline void driver_remove_file(struct device_driver *drv, const struct driver_attribute *attr)
{
	/* No driver sysfs object was published by driver_create_file(). */
	(void)drv;
	(void)attr;
}

static inline int sysfs_create_group(struct kobject *kobj, const struct attribute_group *grp)
{
	/* Linux publishes the group; firmware intentionally has no sysfs tree. */
	(void)kobj;
	(void)grp;
	return 0;
}

static inline void sysfs_remove_group(struct kobject *kobj, const struct attribute_group *grp)
{
	/* No group was published by sysfs_create_group(). */
	(void)kobj;
	(void)grp;
}

static inline int driver_sysfs_create_group(struct device_driver *drv, const struct attribute_group *grp)
{
	/* Linux publishes the group; firmware intentionally has no sysfs tree. */
	(void)drv;
	(void)grp;
	return 0;
}

static inline void driver_sysfs_remove_group(struct device_driver *drv, const struct attribute_group *grp)
{
	/* No group was published by driver_sysfs_create_group(). */
	(void)drv;
	(void)grp;
}

/*
 * Publishing upstream sysfs calls is intentionally a success/no-op above.
 * Accessing or enumerating attributes needs a real firmware proxy and must not
 * silently observe an empty fake registry.
 */
ssize_t device_attr_show(struct device *dev, const char *name, char *buf)
	__attribute__((error("device sysfs access needs a firmware proxy")));
ssize_t device_attr_store(struct device *dev, const char *name,
			  const char *buf, size_t count)
	__attribute__((error("device sysfs access needs a firmware proxy")));
ssize_t device_bin_attr_read(struct device *dev, const char *name,
			     char *buf, loff_t off, size_t count)
	__attribute__((error("binary sysfs access needs a firmware proxy")));
ssize_t device_bin_attr_write(struct device *dev, const char *name,
			      char *buf, loff_t off, size_t count)
	__attribute__((error("binary sysfs access needs a firmware proxy")));
ssize_t driver_attr_show(struct device_driver *drv, const char *name,
			 char *buf)
	__attribute__((error("driver sysfs access needs a firmware proxy")));
ssize_t driver_attr_store(struct device_driver *drv, const char *name,
			  const char *buf, size_t count)
	__attribute__((error("driver sysfs access needs a firmware proxy")));
int device_attr_for_each(struct device *dev, device_attr_iter_fn fn, void *data)
	__attribute__((error("device sysfs iteration needs a firmware proxy")));
int device_bin_attr_for_each(struct device *dev, device_bin_attr_iter_fn fn,
			     void *data)
	__attribute__((error("binary sysfs iteration needs a firmware proxy")));
int driver_attr_for_each(struct device_driver *drv,
			 int (*fn)(struct device_driver *drv,
				   const struct driver_attribute *attr,
				   void *data),
			 void *data)
	__attribute__((error("driver sysfs iteration needs a firmware proxy")));

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

// Linux kcalloc(count, size, flags) compatibility shim.
static inline void *kcalloc(size_t count, size_t size, int flags)
{
	(void)flags;
	return pvPortCalloc(count, size);
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

/*
 * PORTING DEBT: these typed convenience products lack Linux's overflow
 * checking. Current linked counts are independently bounded; audit the
 * multiplication before using them with a new or descriptor-sized count.
 */
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
	/* PORTING DEBT: callers must bound n * size until overflow is checked. */
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

static inline void mutex_init(struct mutex *mutex)
{
	mutex->handle = xSemaphoreCreateMutex();
}

/* FreeRTOS mutex construction can fail; Linux's embedded mutex cannot. */
static inline bool mutex_initialized(const struct mutex *mutex)
{
	return mutex->handle != NULL;
}

static inline int mutex_lock_killable(struct mutex *mutex)
{
	xSemaphoreTake(mutex->handle, portMAX_DELAY);
	return 0;
}

static inline void mutex_lock(struct mutex *mutex)
{
	xSemaphoreTake(mutex->handle, portMAX_DELAY);
}

static inline void mutex_unlock(struct mutex *mutex)
{
	xSemaphoreGive(mutex->handle);
}

static inline void mutex_destroy(struct mutex *mutex)
{
	SemaphoreHandle_t handle = mutex->handle;

	mutex->handle = NULL;
	if (handle)
		vSemaphoreDelete(handle);
}

static inline bool mutex_is_locked(struct mutex *mutex)
{
	return uxSemaphoreGetCount(mutex->handle) == 0;
}

static inline int down_interruptible(struct semaphore *sem)
{
	TaskHandle_t task;

	if (xSemaphoreTake(sem->handle, portMAX_DELAY) != pdPASS)
		return -EINTR;
	task = xTaskGetCurrentTaskHandle();
	__atomic_store_n(&sem->owner, task, __ATOMIC_RELEASE);
	return 0;
}

static inline void down(struct semaphore *sem)
{
	TaskHandle_t task;

	xSemaphoreTake(sem->handle, portMAX_DELAY);
	task = xTaskGetCurrentTaskHandle();
	__atomic_store_n(&sem->owner, task, __ATOMIC_RELEASE);
}

static inline int down_trylock(struct semaphore *sem)
{
	TaskHandle_t task;

	if (xSemaphoreTake(sem->handle, 0) != pdPASS)
		return 1;
	task = xTaskGetCurrentTaskHandle();
	__atomic_store_n(&sem->owner, task, __ATOMIC_RELEASE);
	return 0;
}

static inline void up(struct semaphore *sem)
{
	__atomic_store_n(&sem->owner, NULL, __ATOMIC_RELEASE);
	xSemaphoreGive(sem->handle);
}

static inline void sema_init(struct semaphore *sem, int val)
{
	sem->handle = xSemaphoreCreateCounting((UBaseType_t)val, (UBaseType_t)val);
	__atomic_store_n(&sem->owner, NULL, __ATOMIC_RELAXED);
}

/* FreeRTOS semaphore construction can fail; expose it to port constructors. */
static inline bool sema_initialized(const struct semaphore *sem)
{
	return sem->handle != NULL;
}

static inline void sema_destroy(struct semaphore *sem)
{
	SemaphoreHandle_t handle = sem->handle;

	sem->handle = NULL;
	__atomic_store_n(&sem->owner, NULL, __ATOMIC_RELEASE);
	if (handle)
		vSemaphoreDelete(handle);
}

static inline bool sema_owned_by_task(struct semaphore *sem,
				      TaskHandle_t task)
{
	/*
	 * owner is diagnostic/control-flow metadata beside the real FreeRTOS
	 * semaphore. An atomic pointer publication avoids a second global kernel
	 * critical section on every HID report and is portable to the ESP build.
	 */
	TaskHandle_t owner = __atomic_load_n(&sem->owner, __ATOMIC_ACQUIRE);

	return task && owner == task;
}

static inline bool sema_owned_by_current(struct semaphore *sem)
{
	return sema_owned_by_task(sem, xTaskGetCurrentTaskHandle());
}

static inline void hid_compat_spin_lock_init(spinlock_t *lock)
{
	lock->locked = 0;
}

static inline bool hid_compat_scheduler_started(void)
{
        return xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED;
}

static inline void hid_compat_spin_lock(spinlock_t *lock)
{
	// Initializer-only shell; public spinlock operations are compile-gated.
	lock->locked = 1;
}

static inline void hid_compat_spin_unlock(spinlock_t *lock)
{
	// Initializer-only shell; public spinlock operations are compile-gated.
	lock->locked = 0;
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
/*
 * No linked caller relies on a compatibility spinlock for exclusion. Active
 * shared state uses an explicit task mutex or an atomic port boundary. Fail a
 * future driver at compile time instead of silently giving it the old no-op.
 */
void hid_compat_spinlock_not_supported(void)
	__attribute__((error("spinlock use needs an explicit firmware ownership bridge")));
#define spin_lock(lock) do { (void)(lock); hid_compat_spinlock_not_supported(); } while (0)
#define spin_unlock(lock) do { (void)(lock); hid_compat_spinlock_not_supported(); } while (0)
#define spin_lock_irq(lock) do { (void)(lock); hid_compat_spinlock_not_supported(); } while (0)
#define spin_unlock_irq(lock) do { (void)(lock); hid_compat_spinlock_not_supported(); } while (0)
#define spin_lock_irqsave(lock, flags) do { (void)(lock); (void)(flags); hid_compat_spinlock_not_supported(); } while (0)
#define spin_unlock_irqrestore(lock, flags) do { (void)(lock); (void)(flags); hid_compat_spinlock_not_supported(); } while (0)

#define HID_COMPAT_WAIT_NOTIFY_INDEX 1u
_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES >
	       HID_COMPAT_WAIT_NOTIFY_INDEX,
	       "HID compatibility waits require notification index 1");

static inline void init_waitqueue_head(wait_queue_head_t *wait)
{
	wait->task = NULL;
	wait->cancel_status = 0;
	wait->transport_next = NULL;
}

static inline void wake_up_interruptible(wait_queue_head_t *wait)
{
	TaskHandle_t task;

	task = __atomic_load_n(&wait->task, __ATOMIC_ACQUIRE);
	if (task)
		(void)xTaskNotifyGiveIndexed(task,
					    HID_COMPAT_WAIT_NOTIFY_INDEX);
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

/*
 * Each HID++ device serializes its sole waiter with send_mutex. Physical DJ
 * receivers may own several such child waiters, so the transport links their
 * embedded heads without another allocation. Register the task before testing
 * its durable predicate, and use notification index 1 so a blocking work item
 * does not consume the workqueue's index-0 wake edge. Firmware disconnect
 * remains a separate durable predicate inspected by the HID++ caller, so
 * wait_event_timeout() retains Linux's 0/positive return contract.
 */
int hid_compat_waitqueue_bind(wait_queue_head_t *wait, struct hid_device *hid);
void hid_compat_waitqueue_unbind(wait_queue_head_t *wait,
				 struct hid_device *hid);
void hid_compat_waitqueue_state_lock(wait_queue_head_t *wait);
void hid_compat_waitqueue_state_unlock(wait_queue_head_t *wait);

static inline void hid_compat_waitqueue_cancel(wait_queue_head_t *wait)
{
	__atomic_store_n(&wait->cancel_status, -ENODEV, __ATOMIC_RELEASE);
	wake_up(wait);
}

/*
 * Firmware-only exact-interface disconnect predicate. HID++ includes it in
 * the standard wait condition, then maps the published status outside
 * wait_event_timeout().
 */
static inline bool
hid_compat_waitqueue_cancelled(const wait_queue_head_t *wait)
{
	return __atomic_load_n(&wait->cancel_status, __ATOMIC_ACQUIRE) ==
		-ENODEV;
}

static inline void hid_compat_waitqueue_prepare(wait_queue_head_t *wait)
{
	TaskHandle_t task = xTaskGetCurrentTaskHandle();

	__atomic_store_n(&wait->task, task, __ATOMIC_RELEASE);
}

static inline void hid_compat_waitqueue_finish(wait_queue_head_t *wait)
{
	__atomic_store_n(&wait->task, NULL, __ATOMIC_RELEASE);
}

#define wait_event_timeout(wq, condition, timeout) \
	({ \
		wait_queue_head_t *__wait = &(wq); \
		TickType_t __deadline = xTaskGetTickCount() + (TickType_t)(timeout); \
		long __result; \
		hid_compat_waitqueue_prepare(__wait); \
		for (;;) { \
			TickType_t __now; \
			if (condition) { \
				__now = xTaskGetTickCount(); \
				__result = time_before((unsigned long)__now, \
						      (unsigned long)__deadline) ? \
					(long)(__deadline - __now) : 1L; \
				break; \
			} \
			__now = xTaskGetTickCount(); \
			if (!time_before((unsigned long)__now, \
					 (unsigned long)__deadline)) { \
				__result = 0; \
				break; \
			} \
			(void)ulTaskNotifyTakeIndexed( \
				HID_COMPAT_WAIT_NOTIFY_INDEX, pdTRUE, \
						     __deadline - __now); \
		} \
		hid_compat_waitqueue_finish(__wait); \
		__result; \
	})
long hid_compat_waitqueue_interruptible_not_supported(void)
	__attribute__((error("interruptible wait needs a firmware signal bridge")));
#define wait_event_interruptible_timeout(wq, condition, timeout) \
	((void)(&(wq)), (void)sizeof(condition), (void)(timeout), \
	 hid_compat_waitqueue_interruptible_not_supported())

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
	/* Linux kref_init() publishes the first owned reference. */
	__atomic_store_n(&kref->refcount, 1u, __ATOMIC_RELAXED);
}

static inline void kref_get(struct kref *kref)
{
	/* kref_get() requires an existing owned reference. */
	(void)__atomic_fetch_add(&kref->refcount, 1u, __ATOMIC_RELAXED);
}

static inline int kref_put(struct kref *kref,
			   void (*release)(struct kref *kref))
{
	unsigned int refs;

	/* Linked callers release only references they already own. */
	refs = __atomic_fetch_sub(&kref->refcount, 1u, __ATOMIC_RELEASE);
	if (refs != 1u)
		return 0;

	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	release(kref);
	return 1;
}

static inline void hid_debug_init(void)
{
	/* Firmware has no debugfs sink; keep hid-core's unconditional init shape. */
}

static inline void hid_debug_exit(void)
{
	/* Firmware has no debugfs sink; there is no debug registry to release. */
}

static inline void hid_debug_register(void *hdev, const char *name)
{
	/*
	 * Linux exposes parsed HID reports through debugfs. No debugfs consumer is
	 * linked here, so registration has no externally observable owner or data.
	 */
	(void)hdev;
	(void)name;
}

static inline void hid_debug_unregister(void *hdev)
{
	/* Paired no-op for the deliberately absent debugfs registration above. */
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

// Linux put_unaligned_le32(); local copy avoids pulling Linux unaligned headers.
static inline void put_unaligned_le32(u32 value, u8 *p)
{
	p[0] = (u8)value;
	p[1] = (u8)(value >> 8);
	p[2] = (u8)(value >> 16);
	p[3] = (u8)(value >> 24);
}

// Linux sign_extend32(); local copy avoids pulling bitops headers.
static inline s32 sign_extend32(u32 value, unsigned int index)
{
	u32 sign = 1u << index;
	return (s32)((value ^ sign) - sign);
}

/*
 * Minimal local copy of Linux bitops used by HID/input.
 *
 * Linux atomic bitops are non-sleeping and can synchronize task/timer/IRQ
 * users. GCC __atomic operations preserve that boundary without FreeRTOS task
 * critical sections. The RP2040 Pico SDK supplies their hardware-spinlock
 * implementation, while GCC/Clang ESP toolchains provide the same interface.
 */
static inline void set_bit(unsigned int nr, unsigned long *addr)
{
	(void)__atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr),
				__ATOMIC_RELAXED);
}

/* Linux __set_bit() is deliberately non-atomic; callers provide exclusion. */
static inline void __set_bit(unsigned int nr, unsigned long *addr)
{
	addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

static inline void clear_bit(unsigned int nr, unsigned long *addr)
{
	(void)__atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr),
				 __ATOMIC_RELAXED);
}

/* Linux __clear_bit() is deliberately non-atomic; callers provide exclusion. */
static inline void __clear_bit(unsigned int nr, unsigned long *addr)
{
	addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}

/* Linux __change_bit() is deliberately non-atomic; callers provide exclusion. */
static inline void __change_bit(unsigned int nr, unsigned long *addr)
{
	addr[BIT_WORD(nr)] ^= BIT_MASK(nr);
}

static inline int test_bit(unsigned int nr, const unsigned long *addr)
{
	unsigned long value = __atomic_load_n(&addr[BIT_WORD(nr)],
					      __ATOMIC_RELAXED);

	return !!(value & BIT_MASK(nr));
}

static inline int test_and_set_bit(unsigned int nr, unsigned long *addr)
{
	unsigned long old = __atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr),
					      __ATOMIC_SEQ_CST);

	return !!(old & BIT_MASK(nr));
}

static inline int __test_and_set_bit(unsigned int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *word = &addr[BIT_WORD(nr)];
	int old = !!(*word & mask);

	*word |= mask;
	return old;
}

/* Linux lock bitops acquire only when this caller changes zero to one. */
static inline int test_and_set_bit_lock(unsigned int nr, unsigned long *addr)
{
	unsigned long old = __atomic_fetch_or(&addr[BIT_WORD(nr)], BIT_MASK(nr),
					      __ATOMIC_ACQUIRE);

	return !!(old & BIT_MASK(nr));
}

// Minimal local copy of Linux bitops used by work flags.
static inline int test_and_clear_bit(unsigned int nr, unsigned long *addr)
{
	unsigned long old = __atomic_fetch_and(&addr[BIT_WORD(nr)],
					       ~BIT_MASK(nr),
					       __ATOMIC_SEQ_CST);

	return !!(old & BIT_MASK(nr));
}

static inline int __test_and_clear_bit(unsigned int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *word = &addr[BIT_WORD(nr)];
	int old = !!(*word & mask);

	*word &= ~mask;
	return old;
}

static inline void clear_bit_unlock(unsigned int nr, unsigned long *addr)
{
	(void)__atomic_fetch_and(&addr[BIT_WORD(nr)], ~BIT_MASK(nr),
				 __ATOMIC_RELEASE);
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
int timer_delete(struct timer_list *timer);
int timer_delete_sync(struct timer_list *timer);
// #define timer_delete(timer) timer_delete_sync(timer)
// Linux keeps timer_delete() non-waiting and timer_delete_sync() waiting.
// ff-memless calls timer_delete() from its timer callback after expiry.

#define jiffies ((unsigned long)xTaskGetTickCount())
#define secs_to_jiffies(sec) ((unsigned long)(sec) * (unsigned long)configTICK_RATE_HZ)
#define msecs_to_jiffies(ms) ((unsigned long)(((uint64_t)(ms) * configTICK_RATE_HZ + 999u) / 1000u))
#define jiffies_to_msecs(j) ((unsigned int)(((uint64_t)(j) * 1000u) / configTICK_RATE_HZ))
#define jiffies_to_usecs(j) ((unsigned int)(((uint64_t)(j) * 1000000u) / configTICK_RATE_HZ))
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
