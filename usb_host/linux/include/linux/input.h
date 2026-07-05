#ifndef USB_HOST_LINUX_INPUT_H
#define USB_HOST_LINUX_INPUT_H

#include "hid_compat.h"
#include "../uapi/linux/input-event-codes.h"

/*
 * Reduced Linux input contract used by HID input/FF glue. Firmware proxy
 * ABI declarations live here at the boundary after Linux input processing;
 * this is not a full Linux userspace input/uapi replacement.
 */

struct input_id {
	__u16 bustype;
	__u16 vendor;
	__u16 product;
	__u16 version;
};

struct input_keymap_entry {
#define INPUT_KEYMAP_BY_INDEX	(1 << 0)
	__u8 flags;
	__u8 len;
	__u16 index;
	__u32 keycode;
	__u8 scancode[32];
};

struct input_absinfo {
	__s32 value;
	__s32 minimum;
	__s32 maximum;
	__s32 fuzz;
	__s32 flat;
	__s32 resolution;
};

struct ff_trigger {
	__u16 button;
	__u16 interval;
};

struct ff_replay {
	__u16 length;
	__u16 delay;
};

struct ff_rumble_effect {
	__u16 strong_magnitude;
	__u16 weak_magnitude;
};

struct ff_envelope {
	__u16 attack_length;
	__u16 attack_level;
	__u16 fade_length;
	__u16 fade_level;
};

struct ff_constant_effect {
	__s16 level;
	struct ff_envelope envelope;
};

struct ff_ramp_effect {
	__s16 start_level;
	__s16 end_level;
	struct ff_envelope envelope;
};

struct ff_condition_effect {
	__u16 right_saturation;
	__u16 left_saturation;
	__s16 right_coeff;
	__s16 left_coeff;
	__u16 deadband;
	__s16 center;
};

struct ff_periodic_effect {
	__u16 waveform;
	__u16 period;
	__s16 magnitude;
	__s16 offset;
	__u16 phase;
	struct ff_envelope envelope;
	__u32 custom_len;
	__s16 __user *custom_data;
};

struct ff_haptic_effect {
	__u16 hid_usage;
	__u16 vendor_id;
	__u8 vendor_waveform_page;
	__u16 intensity;
	__u16 repeat_count;
	__u16 retrigger_period;
};

struct ff_effect {
	__u16 type;
	__s16 id;
	__u16 direction;
	struct ff_trigger trigger;
	struct ff_replay replay;

	union {
		struct ff_constant_effect constant;
		struct ff_ramp_effect ramp;
		struct ff_periodic_effect periodic;
		struct ff_condition_effect condition[2];
		struct ff_rumble_effect rumble;
		struct ff_haptic_effect haptic;
	} u;
};

struct input_value {
	__u16 type;
	__u16 code;
	__s32 value;
};

/* INPUT_CLK_* order follows upstream; port ktime_t stores one scalar value. */
#define INPUT_CLK_REAL		0
#define INPUT_CLK_MONO		1
#define INPUT_CLK_BOOT		2
#define INPUT_CLK_MAX		3

#define INPUT_DEVICE_ID_EV_MAX		0x1f
#define INPUT_DEVICE_ID_KEY_MAX		0x2ff
#define INPUT_DEVICE_ID_REL_MAX		0x0f
#define INPUT_DEVICE_ID_ABS_MAX		0x3f
#define INPUT_DEVICE_ID_MSC_MAX		0x07
#define INPUT_DEVICE_ID_LED_MAX		0x0f
#define INPUT_DEVICE_ID_SND_MAX		0x07
#define INPUT_DEVICE_ID_FF_MAX		0x7f
#define INPUT_DEVICE_ID_SW_MAX		0x11
#define INPUT_DEVICE_ID_PROP_MAX	0x1f

#define INPUT_DEVICE_ID_MATCH_BUS	1
#define INPUT_DEVICE_ID_MATCH_VENDOR	2
#define INPUT_DEVICE_ID_MATCH_PRODUCT	4
#define INPUT_DEVICE_ID_MATCH_VERSION	8

#define INPUT_DEVICE_ID_MATCH_EVBIT	0x0010
#define INPUT_DEVICE_ID_MATCH_KEYBIT	0x0020
#define INPUT_DEVICE_ID_MATCH_RELBIT	0x0040
#define INPUT_DEVICE_ID_MATCH_ABSBIT	0x0080
#define INPUT_DEVICE_ID_MATCH_MSCIT	0x0100
#define INPUT_DEVICE_ID_MATCH_LEDBIT	0x0200
#define INPUT_DEVICE_ID_MATCH_SNDBIT	0x0400
#define INPUT_DEVICE_ID_MATCH_FFBIT	0x0800
#define INPUT_DEVICE_ID_MATCH_SWBIT	0x1000
#define INPUT_DEVICE_ID_MATCH_PROPBIT	0x2000

#define INPUT_DEVICE_ID_MATCH_DEVICE \
	(INPUT_DEVICE_ID_MATCH_BUS | INPUT_DEVICE_ID_MATCH_VENDOR | INPUT_DEVICE_ID_MATCH_PRODUCT)
#define INPUT_DEVICE_ID_MATCH_DEVICE_AND_VERSION \
	(INPUT_DEVICE_ID_MATCH_DEVICE | INPUT_DEVICE_ID_MATCH_VERSION)

struct input_device_id {
	kernel_ulong_t flags;

	__u16 bustype;
	__u16 vendor;
	__u16 product;
	__u16 version;

	kernel_ulong_t evbit[INPUT_DEVICE_ID_EV_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t keybit[INPUT_DEVICE_ID_KEY_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t relbit[INPUT_DEVICE_ID_REL_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t absbit[INPUT_DEVICE_ID_ABS_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t mscbit[INPUT_DEVICE_ID_MSC_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t ledbit[INPUT_DEVICE_ID_LED_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t sndbit[INPUT_DEVICE_ID_SND_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t ffbit[INPUT_DEVICE_ID_FF_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t swbit[INPUT_DEVICE_ID_SW_MAX / BITS_PER_LONG + 1];
	kernel_ulong_t propbit[INPUT_DEVICE_ID_PROP_MAX / BITS_PER_LONG + 1];

	kernel_ulong_t driver_info;
};

#define BUS_PCI			0x01
#define BUS_ISAPNP		0x02
#define BUS_USB			0x03
#define BUS_HIL			0x04
#define BUS_BLUETOOTH		0x05
#define BUS_VIRTUAL		0x06
#define BUS_ISA			0x10
#define BUS_I8042		0x11
#define BUS_XTKBD		0x12
#define BUS_RS232		0x13
#define BUS_GAMEPORT		0x14
#define BUS_PARPORT		0x15
#define BUS_AMIGA		0x16
#define BUS_ADB			0x17
#define BUS_I2C			0x18
#define BUS_HOST		0x19
#define BUS_GSC			0x1A
#define BUS_ATARI		0x1B
#define BUS_SPI			0x1C
#define BUS_RMI			0x1D
#define BUS_CEC			0x1E
#define BUS_INTEL_ISHTP		0x1F
#define BUS_AMD_SFH		0x20
#define BUS_SDW			0x21

#define FF_MAX			0x7f
#define FF_CNT			(FF_MAX+1)
#define FF_HAPTIC		0x4f
#define FF_RUMBLE		0x50
#define FF_PERIODIC		0x51
#define FF_CONSTANT		0x52
#define FF_SPRING		0x53
#define FF_FRICTION		0x54
#define FF_DAMPER		0x55
#define FF_INERTIA		0x56
#define FF_RAMP			0x57
#define FF_SQUARE		0x58
#define FF_TRIANGLE		0x59
#define FF_SINE			0x5a
#define FF_SAW_UP		0x5b
#define FF_SAW_DOWN		0x5c
#define FF_CUSTOM		0x5d
#define FF_EFFECT_MIN		FF_HAPTIC
#define FF_EFFECT_MAX		FF_RAMP
#define FF_WAVEFORM_MIN	FF_SQUARE
#define FF_WAVEFORM_MAX	FF_CUSTOM
#define FF_GAIN			0x60
#define FF_AUTOCENTER		0x61
#define FF_MAX_EFFECTS		FF_GAIN

struct ff_device;
struct hid_device;
struct hid_field;
struct hid_usage;
struct input_dev_poller;
struct input_mt;
struct input_handler;

struct input_dev {
	const char *name;
	const char *phys;
	const char *uniq;
	struct input_id id;

	unsigned long propbit[BITS_TO_LONGS(INPUT_PROP_CNT)];

	unsigned long evbit[BITS_TO_LONGS(EV_CNT)];
	unsigned long keybit[BITS_TO_LONGS(KEY_CNT)];
	unsigned long relbit[BITS_TO_LONGS(REL_CNT)];
	unsigned long absbit[BITS_TO_LONGS(ABS_CNT)];
	unsigned long mscbit[BITS_TO_LONGS(MSC_CNT)];
	unsigned long ledbit[BITS_TO_LONGS(LED_CNT)];
	unsigned long sndbit[BITS_TO_LONGS(SND_CNT)];
	unsigned long ffbit[BITS_TO_LONGS(FF_CNT)];
	unsigned long swbit[BITS_TO_LONGS(SW_CNT)];

	unsigned int hint_events_per_packet;

	unsigned int keycodemax;
	unsigned int keycodesize;
	void *keycode;

	int (*setkeycode)(struct input_dev *dev,
			  const struct input_keymap_entry *ke,
			  unsigned int *old_keycode);
	int (*getkeycode)(struct input_dev *dev,
			  struct input_keymap_entry *ke);

	struct ff_device *ff;
	struct input_dev_poller *poller;

	unsigned int repeat_key;
	struct timer_list timer;

	int rep[REP_CNT];

	struct input_mt *mt;

	struct input_absinfo *absinfo;

	unsigned long key[BITS_TO_LONGS(KEY_CNT)];
	unsigned long led[BITS_TO_LONGS(LED_CNT)];
	unsigned long snd[BITS_TO_LONGS(SND_CNT)];
	unsigned long sw[BITS_TO_LONGS(SW_CNT)];

	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);
	int (*flush)(struct input_dev *dev, struct file *file);
	int (*event)(struct input_dev *dev, unsigned int type, unsigned int code, int value);

	struct input_handle *grab;

	spinlock_t event_lock;
	// Work3 used a FreeRTOS mutex here to serialize proxy/driver-side input
	// snapshots. The callback-driven slice keeps input_port_event_lock()
	// as a no-op, so do not store a mutex handle in active input_dev state.
	// SemaphoreHandle_t port_event_lock;
	struct mutex mutex;

	unsigned int users;
	bool going_away;

	struct device dev;

	struct list_head h_list;
	struct list_head node;

	unsigned int num_vals;
	unsigned int max_vals;
	struct input_value *vals;

	bool devres_managed;
	bool registered;
	unsigned int port_proxy_id;

	ktime_t timestamp[INPUT_CLK_MAX];

	bool inhibited;
};

static inline void input_port_event_lock(struct input_dev *dev)
{
	// xSemaphoreTake(dev->port_event_lock, portMAX_DELAY);
	// Callback-driven slice has no input worker; never block HID callbacks.
	(void)dev;
}

static inline void input_port_event_unlock(struct input_dev *dev)
{
	// xSemaphoreGive(dev->port_event_lock);
	// See nonblocking callback-driven note above.
	(void)dev;
}

struct input_handler {
	void *private;

	void (*event)(struct input_handle *handle, unsigned int type, unsigned int code, int value);
	unsigned int (*events)(struct input_handle *handle,
			       struct input_value *vals, unsigned int count);
	bool (*filter)(struct input_handle *handle, unsigned int type, unsigned int code, int value);
	bool (*match)(struct input_handler *handler, struct input_dev *dev);
	int (*connect)(struct input_handler *handler, struct input_dev *dev, const struct input_device_id *id);
	void (*disconnect)(struct input_handle *handle);
	void (*start)(struct input_handle *handle);

	bool passive_observer;
	bool legacy_minors;
	int minor;
	const char *name;

	const struct input_device_id *id_table;

	struct list_head h_list;
	struct list_head node;
};

struct ff_device {
	int (*upload)(struct input_dev *dev, struct ff_effect *effect, struct ff_effect *old);
	int (*erase)(struct input_dev *dev, int effect_id);
	int (*playback)(struct input_dev *dev, int effect_id, int value);
	void (*set_gain)(struct input_dev *dev, u16 gain);
	void (*set_autocenter)(struct input_dev *dev, u16 magnitude);
	void (*destroy)(struct ff_device *ff);

	void *private;

	unsigned long ffbit[BITS_TO_LONGS(FF_CNT)];

	struct mutex mutex;

	int max_effects;
	struct ff_effect *effects;
	struct file *effect_owners[];
};

struct input_handle {
	void *private;

	int open;
	const char *name;

	struct input_dev *dev;
	struct input_handler *handler;

	unsigned int (*handle_events)(struct input_handle *handle,
				      struct input_value *vals,
				      unsigned int count);

	struct list_head d_node;
	struct list_head h_node;
};

#define to_input_dev(d) container_of(d, struct input_dev, dev)

static inline void input_set_drvdata(struct input_dev *dev, void *data)
{
	dev->dev.data = data;
}

static inline void *input_get_drvdata(struct input_dev *dev)
{
	return dev->dev.data;
}

static inline struct input_dev *input_get_device(struct input_dev *dev)
{
	// return dev ? to_input_dev(get_device(&dev->dev)) : NULL;
	// Port device core has no refcounted struct device lifetime; keep input_dev ownership explicit.
	return dev;
}

static inline void input_put_device(struct input_dev *dev)
{
	// if (dev)
	// 	put_device(&dev->dev);
	// Port device core has no refcounted struct device lifetime; input_unregister_device() frees directly.
	(void)dev;
}

struct input_dev *input_allocate_device(void);
struct input_dev *devm_input_allocate_device(struct device *dev);
void input_free_device(struct input_dev *dev);
int input_register_device(struct input_dev *dev);
void input_unregister_device(struct input_dev *dev);
int input_register_handler(struct input_handler *handler);
void input_unregister_handler(struct input_handler *handler);
bool input_match_device_id(const struct input_dev *dev,
			   const struct input_device_id *id);
int input_handler_for_each_handle(struct input_handler *handler, void *data,
				  int (*fn)(struct input_handle *handle, void *data));
int input_register_handle(struct input_handle *handle);
void input_unregister_handle(struct input_handle *handle);
int input_grab_device(struct input_handle *handle);
void input_release_device(struct input_handle *handle);
int input_open_device(struct input_handle *handle);
void input_close_device(struct input_handle *handle);
int input_flush_device(struct input_handle *handle, struct file *file);
void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value);
void input_inject_event(struct input_handle *handle, unsigned int type, unsigned int code, int value);
void input_sync(struct input_dev *dev);
void input_set_timestamp(struct input_dev *dev, ktime_t timestamp);
ktime_t *input_get_timestamp(struct input_dev *dev);
void input_reset_device(struct input_dev *dev);
void input_set_abs_params(struct input_dev *dev, unsigned int axis,
			  int min, int max, int fuzz, int flat);
void input_copy_abs(struct input_dev *dst, unsigned int dst_axis,
		    const struct input_dev *src, unsigned int src_axis);
void input_abs_set_res(struct input_dev *dev, unsigned int axis, int resolution);
void input_set_events_per_packet(struct input_dev *dev, unsigned int n_events);
void input_enable_softrepeat(struct input_dev *dev, int delay, int period);
void input_set_capability(struct input_dev *dev, unsigned int type, unsigned int code);
int input_scancode_to_scalar(const struct input_keymap_entry *ke, unsigned int *scancode);
int input_default_setkeycode(struct input_dev *dev,
			     const struct input_keymap_entry *ke,
			     unsigned int *old_keycode);
int input_ff_event(struct input_dev *dev, unsigned int type, unsigned int code, int value);
int input_ff_create_memless(struct input_dev *dev, void *data,
			    int (*play_effect)(struct input_dev *, void *, struct ff_effect *));
int input_ff_create(struct input_dev *dev, unsigned int max_effects);
void input_ff_destroy(struct input_dev *dev);
int input_ff_upload(struct input_dev *dev, struct ff_effect *effect, struct file *file);
int input_ff_erase(struct input_dev *dev, int effect_id, struct file *file);
int input_ff_flush(struct input_dev *dev, struct file *file);
struct input_dev *input_find_device_by_name(const char *name);
int input_for_each_device(int (*fn)(struct input_dev *dev, void *data), void *data);
int input_port_init(void);
int input_port_activate_hid(struct hid_device *hid);
void input_port_deactivate_hid(struct hid_device *hid);

static inline int input_abs_get_val(struct input_dev *dev, unsigned int axis)
{
	return dev->absinfo ? dev->absinfo[axis].value : 0;
}

static inline void input_report_key(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_KEY, code, !!value);
}

static inline void input_report_rel(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_REL, code, value);
}

static inline void input_report_abs(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_ABS, code, value);
}

static inline void input_report_ff_status(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_FF_STATUS, code, value);
}

static inline void input_report_switch(struct input_dev *dev, unsigned int code, int value)
{
	input_event(dev, EV_SW, code, !!value);
}

static inline void input_mt_sync(struct input_dev *dev)
{
	input_event(dev, EV_SYN, SYN_MT_REPORT, 0);
}

#endif
