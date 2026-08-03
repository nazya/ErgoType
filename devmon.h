#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include <linux/input-event-codes.h>

#define MAX_DEVICES 8
/* Bound all live and retiring length-one power-supply QueueSet members. */
#define PORT_POWER_SUPPLY_MAX 8
#define DEVICE_EVENT_QUEUE_LEN 16
/*
 * Standard hid-input MT buffering uses 60 values plus two input-core framing
 * slots. Host evdev keeps two further slots for key release and removal.
 */
#define EVDEV_EVENT_QUEUE_LEN 64
/* QueueSet storage must cover the largest possible member in every slot. */
#define DEVICE_EVENT_SET_LEN (MAX_DEVICES * EVDEV_EVENT_QUEUE_LEN + MAX_DEVICES)

#define DEVICE_INPUT_REMOVED	0xffffu
#define DEVICE_INPUT_RESET	0xfffeu
#define INPUT_BITS_PER_LONG	(sizeof(unsigned long) * 8u)
#define INPUT_BITS_TO_LONGS(nr)	(((nr) + INPUT_BITS_PER_LONG - 1u) / INPUT_BITS_PER_LONG)

#define PORT_POWER_SUPPLY_NAME_LEN 32u
#define PORT_POWER_SUPPLY_MODEL_LEN 32u
#define PORT_POWER_SUPPLY_SERIAL_LEN 64u

enum {
	PORT_POWER_SUPPLY_HAS_STATUS = 1u << 0,
	PORT_POWER_SUPPLY_HAS_ONLINE = 1u << 1,
	PORT_POWER_SUPPLY_HAS_CAPACITY = 1u << 2,
	PORT_POWER_SUPPLY_HAS_CAPACITY_LEVEL = 1u << 3,
	PORT_POWER_SUPPLY_HAS_VOLTAGE_NOW = 1u << 4,
	PORT_POWER_SUPPLY_HAS_PRESENT = 1u << 5,
};

/*
 * Same fields as Linux struct input_absinfo. Keep the snapshot type local so
 * devmon.h does not pull linux/input.h and the HID compat runtime into KeyD.
 */
struct input_absinfo_snapshot {
	int32_t value;
	int32_t minimum;
	int32_t maximum;
	int32_t fuzz;
	int32_t flat;
	int32_t resolution;
};

static inline void input_bitmap_set(unsigned int bit, unsigned long *bitmap)
{
	bitmap[bit / INPUT_BITS_PER_LONG] |= 1ul << (bit % INPUT_BITS_PER_LONG);
}

/*
 * Upstream keyd reads Linux struct input_event from an input fd. Firmware
 * queues use a separate type with only the fields keyd consumes.
 */
struct port_input_event {
	uint16_t type;
	uint16_t code;
	int32_t value;
};

struct evdev_client;
struct ff_effect;

struct evdev_writer {
	struct evdev_client *client;
	int (*write)(struct evdev_client *client,
		     const struct port_input_event *events, size_t count);
	int (*upload_ff)(struct evdev_client *client, struct ff_effect *effect);
	int (*erase_ff)(struct evdev_client *client, int effect_id);
};

/*
 * devmon_queue carries this compact snapshot by value. Producers may build it
 * from a full Linux input_dev, but KeyD only needs the bounded identity,
 * EVIOCGBIT-style bitmaps, and EVIOCGABS ranges below during device add; the
 * full input_dev pointer stays owned by the Linux input layer.
 */
struct port_input_dev {
	QueueHandle_t ev_queue;
	struct evdev_writer writer;
	uint16_t vendor;
	uint16_t product;
	const char *name;
	bool has_haptic;
	unsigned long keybit[INPUT_BITS_TO_LONGS(KEY_CNT)];
	unsigned long relbit[INPUT_BITS_TO_LONGS(REL_CNT)];
	unsigned long absbit[INPUT_BITS_TO_LONGS(ABS_CNT)];
	unsigned long propbit[INPUT_BITS_TO_LONGS(INPUT_PROP_CNT)];
	struct input_absinfo_snapshot abs_x;
	struct input_absinfo_snapshot abs_y;
};

enum port_power_supply_event_type {
	PORT_POWER_SUPPLY_ADDED,
	PORT_POWER_SUPPLY_CHANGED,
	PORT_POWER_SUPPLY_REMOVED,
};

/* Value-only power_supply event consumed by the UI task. */
struct port_power_supply_snapshot {
	enum port_power_supply_event_type event_type;
	uint32_t proxy_id;
	uint32_t fields;
	int32_t status;
	int32_t capacity;
	int32_t capacity_level;
	int32_t voltage_now_uv;
	bool present;
	bool online;
	char name[PORT_POWER_SUPPLY_NAME_LEN];
	char model[PORT_POWER_SUPPLY_MODEL_LEN];
	char serial[PORT_POWER_SUPPLY_SERIAL_LEN];
};

typedef enum {
	DEVMON_LED,
	DEVMON_HAPTIC,
} devmon_event_type_t;

/*
 * Application output requests are not Linux input_event records. Keep their
 * type domain separate and let the KeyD consumer translate it explicitly.
 */
/*
 * Virtual output requests have no source input device. is_virtual carries
 * them through the existing devmon queue into the KeyD task.
 */
struct devmon_event {
	struct port_input_dev dev;
	int32_t value;
	uint16_t code;
	devmon_event_type_t type;
	bool is_virtual;
};

extern QueueHandle_t devmon_queue;
extern QueueSetHandle_t devmon_event_set;
extern QueueSetHandle_t power_supply_event_set;
int devmon_init(void);
int devmon_add_device(const struct port_input_dev *port_dev);
int devmon_add_power_supply(QueueHandle_t event_queue);
void devmon_remove_power_supply(QueueHandle_t event_queue);
