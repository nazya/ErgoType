#ifndef USB_HOST_LINUX_POWER_SUPPLY_H
#define USB_HOST_LINUX_POWER_SUPPLY_H

#include "hid_compat.h"

// Upstream power_supply is device-core/sysfs infrastructure. Firmware keeps
// the property contract for HID battery drivers and proxy snapshots.

enum {
	POWER_SUPPLY_STATUS_UNKNOWN = 0,
	POWER_SUPPLY_STATUS_CHARGING,
	POWER_SUPPLY_STATUS_DISCHARGING,
	POWER_SUPPLY_STATUS_NOT_CHARGING,
	POWER_SUPPLY_STATUS_FULL,
};

enum power_supply_charge_type {
	POWER_SUPPLY_CHARGE_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_CHARGE_TYPE_NONE,
	POWER_SUPPLY_CHARGE_TYPE_TRICKLE,
	POWER_SUPPLY_CHARGE_TYPE_FAST,
	POWER_SUPPLY_CHARGE_TYPE_STANDARD,
	POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE,
	POWER_SUPPLY_CHARGE_TYPE_CUSTOM,
	POWER_SUPPLY_CHARGE_TYPE_LONGLIFE,
	POWER_SUPPLY_CHARGE_TYPE_BYPASS,
};

enum {
	POWER_SUPPLY_HEALTH_UNKNOWN = 0,
	POWER_SUPPLY_HEALTH_GOOD,
	POWER_SUPPLY_HEALTH_OVERHEAT,
	POWER_SUPPLY_HEALTH_DEAD,
	POWER_SUPPLY_HEALTH_OVERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNDERVOLTAGE,
	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,
	POWER_SUPPLY_HEALTH_COLD,
	POWER_SUPPLY_HEALTH_WATCHDOG_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_SAFETY_TIMER_EXPIRE,
	POWER_SUPPLY_HEALTH_OVERCURRENT,
	POWER_SUPPLY_HEALTH_CALIBRATION_REQUIRED,
	POWER_SUPPLY_HEALTH_WARM,
	POWER_SUPPLY_HEALTH_COOL,
	POWER_SUPPLY_HEALTH_HOT,
	POWER_SUPPLY_HEALTH_NO_BATTERY,
	POWER_SUPPLY_HEALTH_BLOWN_FUSE,
	POWER_SUPPLY_HEALTH_CELL_IMBALANCE,
};

enum {
	POWER_SUPPLY_TECHNOLOGY_UNKNOWN = 0,
	POWER_SUPPLY_TECHNOLOGY_NiMH,
	POWER_SUPPLY_TECHNOLOGY_LION,
	POWER_SUPPLY_TECHNOLOGY_LIPO,
	POWER_SUPPLY_TECHNOLOGY_LiFe,
	POWER_SUPPLY_TECHNOLOGY_NiCd,
	POWER_SUPPLY_TECHNOLOGY_LiMn,
};

enum {
	POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN = 0,
	POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL,
	POWER_SUPPLY_CAPACITY_LEVEL_LOW,
	POWER_SUPPLY_CAPACITY_LEVEL_NORMAL,
	POWER_SUPPLY_CAPACITY_LEVEL_HIGH,
	POWER_SUPPLY_CAPACITY_LEVEL_FULL,
};

enum {
	POWER_SUPPLY_SCOPE_UNKNOWN = 0,
	POWER_SUPPLY_SCOPE_SYSTEM,
	POWER_SUPPLY_SCOPE_DEVICE,
};

enum power_supply_property {
	POWER_SUPPLY_PROP_STATUS = 0,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CHARGE_TYPES,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_AUTHENTIC,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_BOOT,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_BOOT,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_EMPTY,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD,
	POWER_SUPPLY_PROP_CHARGE_BEHAVIOUR,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_INPUT_POWER_LIMIT,
	POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_EMPTY_DESIGN,
	POWER_SUPPLY_PROP_ENERGY_FULL,
	POWER_SUPPLY_PROP_ENERGY_EMPTY,
	POWER_SUPPLY_PROP_ENERGY_NOW,
	POWER_SUPPLY_PROP_ENERGY_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MIN,
	POWER_SUPPLY_PROP_CAPACITY_ALERT_MAX,
	POWER_SUPPLY_PROP_CAPACITY_ERROR_MARGIN,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_MAX,
	POWER_SUPPLY_PROP_TEMP_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_ALERT_MAX,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MIN,
	POWER_SUPPLY_PROP_TEMP_AMBIENT_ALERT_MAX,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_PRECHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_MANUFACTURE_YEAR,
	POWER_SUPPLY_PROP_MANUFACTURE_MONTH,
	POWER_SUPPLY_PROP_MANUFACTURE_DAY,
	POWER_SUPPLY_PROP_INTERNAL_RESISTANCE,
	POWER_SUPPLY_PROP_STATE_OF_HEALTH,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_SERIAL_NUMBER,
};

enum power_supply_type {
	POWER_SUPPLY_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_TYPE_BATTERY,
	POWER_SUPPLY_TYPE_UPS,
	POWER_SUPPLY_TYPE_MAINS,
	POWER_SUPPLY_TYPE_USB,
	POWER_SUPPLY_TYPE_USB_DCP,
	POWER_SUPPLY_TYPE_USB_CDP,
	POWER_SUPPLY_TYPE_USB_ACA,
	POWER_SUPPLY_TYPE_USB_TYPE_C,
	POWER_SUPPLY_TYPE_USB_PD,
	POWER_SUPPLY_TYPE_USB_PD_DRP,
	POWER_SUPPLY_TYPE_APPLE_BRICK_ID,
	POWER_SUPPLY_TYPE_WIRELESS,
};

enum power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_UNKNOWN = 0,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_ACA,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_PD_PPS,
	POWER_SUPPLY_USB_TYPE_PD_SPR_AVS,
	POWER_SUPPLY_USB_TYPE_PD_PPS_SPR_AVS,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID,
};

enum power_supply_charge_behaviour {
	POWER_SUPPLY_CHARGE_BEHAVIOUR_AUTO = 0,
	POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE,
	POWER_SUPPLY_CHARGE_BEHAVIOUR_INHIBIT_CHARGE_AWAKE,
	POWER_SUPPLY_CHARGE_BEHAVIOUR_FORCE_DISCHARGE,
};

enum power_supply_notifier_events {
	PSY_EVENT_PROP_CHANGED,
};

union power_supply_propval {
	int intval;
	const char *strval;
};

#define POWER_SUPPLY_PROXY_ENUM_BEGIN ((__u32)-1)
#define POWER_SUPPLY_PROXY_STRING 64

struct power_supply;

struct power_supply_config {
	void *drv_data;
	char **supplied_to;
	size_t num_supplicants;
};

struct power_supply_desc {
	const char *name;
	enum power_supply_type type;
	const enum power_supply_property *properties;
	size_t num_properties;
	int use_for_apm;
	int (*get_property)(struct power_supply *psy,
			    enum power_supply_property psp,
			    union power_supply_propval *val);
	void (*external_power_changed)(struct power_supply *psy);
};

struct power_supply {
	const struct power_supply_desc *desc;
	char **supplied_to;
	size_t num_supplicants;
	char **supplied_from;
	size_t num_supplies;
	void *drv_data;
	struct device dev;
	struct work_struct changed_work;
	struct device *parent;
	struct device *powered_dev;
	bool changed;
	bool published;
	unsigned int changed_count;
	ktime_t last_changed;
	__u32 port_proxy_id;
	struct power_supply *next;
	struct power_supply *prev;
};

struct power_supply_proxy_snapshot {
	__u32 proxy_id;
	__u32 type;
	__u32 num_properties;
	__u32 num_supplicants;
	__u32 num_supplies;
	__u32 changed_count;
	ktime_t last_changed;
	__u8 changed;
	__u8 has_get_property;
	__u8 has_external_power_changed;
	char name[POWER_SUPPLY_PROXY_STRING];
	char dev_name[POWER_SUPPLY_PROXY_STRING];
};

struct power_supply *devm_power_supply_register(struct device *parent,
						const struct power_supply_desc *desc,
						const struct power_supply_config *cfg);
struct power_supply *power_supply_register(struct device *parent,
					   const struct power_supply_desc *desc,
					   const struct power_supply_config *cfg);
void power_supply_unregister(struct power_supply *psy);
void *power_supply_get_drvdata(struct power_supply *psy);
int power_supply_powers(struct power_supply *psy, struct device *dev);
void power_supply_changed(struct power_supply *psy);
void power_supply_external_power_changed(struct power_supply *psy);
int power_supply_get_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      union power_supply_propval *val);
struct power_supply *power_supply_find_by_name(const char *name);
struct power_supply *power_supply_get_by_name(const char *name);
void power_supply_put(struct power_supply *psy);
int power_supply_for_each_psy(void *data, int (*fn)(struct power_supply *psy, void *data));
int power_supply_for_each(int (*fn)(struct power_supply *psy, void *data), void *data);
int power_supply_proxy_get_snapshot(__u32 proxy_id,
				    struct power_supply_proxy_snapshot *snapshot);
int power_supply_proxy_get_next_snapshot(__u32 after_proxy_id,
					 struct power_supply_proxy_snapshot *snapshot);
int power_supply_proxy_get_property(__u32 proxy_id,
				    enum power_supply_property psp,
				    union power_supply_propval *val);

#endif
