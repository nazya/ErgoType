// SPDX-License-Identifier: GPL-2.0-only
/*
 * Firmware boundary for the reduced Linux power_supply contract.
 *
 * Linux publishes properties through the power-supply class, sysfs, uevents,
 * and notifier chains. Firmware retains driver registration/get_property
 * semantics, coalesces updates on the existing HID work task, and publishes
 * detached value snapshots through one length-one queue per power supply. No
 * driver pointer or devres-owned string crosses that boundary.
 */
#include <string.h>

#include "devmon.h"
#include "linux/include/linux/power_supply.h"

struct port_power_supply {
	struct power_supply psy;
	QueueHandle_t event_queue;
};

static uint32_t power_supply_next_proxy_id;

static bool power_supply_has_property(const struct power_supply *psy,
				      enum power_supply_property property)
{
	size_t i;

	for (i = 0; i < psy->desc->num_properties; ++i)
		if (psy->desc->properties[i] == property)
			return true;
	return false;
}

static bool power_supply_read_property(struct power_supply *psy,
				       enum power_supply_property property,
				       union power_supply_propval *value)
{
	if (!power_supply_has_property(psy, property))
		return false;
	memset(value, 0, sizeof(*value));
	return psy->desc->get_property(psy, property, value) == 0;
}

static void power_supply_build_snapshot(
	struct power_supply *psy,
	struct port_power_supply_snapshot *snapshot,
	enum port_power_supply_event_type event_type)
{
	union power_supply_propval value;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->event_type = event_type;
	snapshot->proxy_id = psy->port_proxy_id;
	strscpy(snapshot->name, psy->desc->name, sizeof(snapshot->name));

	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_STATUS, &value)) {
		snapshot->status = value.intval;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_STATUS;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_PRESENT, &value)) {
		snapshot->present = value.intval != 0;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_PRESENT;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_ONLINE, &value)) {
		snapshot->online = value.intval != 0;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_ONLINE;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_CAPACITY, &value)) {
		snapshot->capacity = value.intval;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_CAPACITY;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_CAPACITY_LEVEL,
				       &value)) {
		snapshot->capacity_level = value.intval;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_CAPACITY_LEVEL;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_VOLTAGE_NOW,
				       &value)) {
		snapshot->voltage_now_uv = value.intval;
		snapshot->fields |= PORT_POWER_SUPPLY_HAS_VOLTAGE_NOW;
	}
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_MODEL_NAME,
				       &value))
		strscpy(snapshot->model, value.strval,
			sizeof(snapshot->model));
	if (power_supply_read_property(psy, POWER_SUPPLY_PROP_SERIAL_NUMBER,
				       &value))
		strscpy(snapshot->serial, value.strval,
			sizeof(snapshot->serial));
}

static bool power_supply_build_stable_snapshot(
	struct power_supply *psy,
	struct port_power_supply_snapshot *snapshot,
	enum port_power_supply_event_type event_type)
{
	uint32_t count_before =
		__atomic_load_n(&psy->changed_count, __ATOMIC_ACQUIRE);

	power_supply_build_snapshot(psy, snapshot, event_type);
	/*
	 * A higher-priority report task updates the properties, increments this
	 * counter, and queues the rerun before the worker can resume here.
	 */
	return count_before ==
		__atomic_load_n(&psy->changed_count, __ATOMIC_ACQUIRE);
}

static void power_supply_publish(
	struct port_power_supply *port,
	const struct port_power_supply_snapshot *snapshot)
{
	struct power_supply *psy = &port->psy;

	(void)xQueueOverwrite(port->event_queue, snapshot);
	psy->published = true;
}

static void power_supply_changed_work(struct work_struct *work)
{
	struct power_supply *psy =
		container_of(work, struct power_supply, changed_work);
	struct port_power_supply *port =
		container_of(psy, struct port_power_supply, psy);
	struct port_power_supply_snapshot snapshot;
	enum port_power_supply_event_type event_type;

	event_type = psy->published ? PORT_POWER_SUPPLY_CHANGED :
				     PORT_POWER_SUPPLY_ADDED;
	if (!power_supply_build_stable_snapshot(psy, &snapshot,
						 event_type))
		return;
	power_supply_publish(port, &snapshot);
}

struct power_supply *power_supply_register(
	struct device *parent, const struct power_supply_desc *desc,
	const struct power_supply_config *cfg)
{
	struct port_power_supply *port;
	struct power_supply *psy;
	int ret;

	port = kzalloc_obj(struct port_power_supply);
	if (!port)
		return ERR_PTR(-ENOMEM);
	port->event_queue =
		xQueueCreate(1, sizeof(struct port_power_supply_snapshot));
	if (!port->event_queue) {
		kfree(port);
		return ERR_PTR(-ENOMEM);
	}
	ret = devmon_add_power_supply(port->event_queue);
	if (ret) {
		vQueueDelete(port->event_queue);
		kfree(port);
		return ERR_PTR(ret);
	}
	psy = &port->psy;
	psy->desc = desc;
	psy->parent = parent;
	psy->drv_data = cfg->drv_data;
	psy->supplied_to = cfg->supplied_to;
	psy->num_supplicants = cfg->num_supplicants;
	device_initialize(&psy->dev);
	psy->dev.parent = parent;
	dev_set_name(&psy->dev, "%s", desc->name);
	INIT_WORK(&psy->changed_work, power_supply_changed_work);

	psy->port_proxy_id =
		__atomic_add_fetch(&power_supply_next_proxy_id, 1u,
				   __ATOMIC_RELAXED);

	return psy;
}

static void power_supply_devm_unregister(void *data)
{
	power_supply_unregister(data);
}

struct power_supply *devm_power_supply_register(
	struct device *parent, const struct power_supply_desc *desc,
	const struct power_supply_config *cfg)
{
	struct power_supply *psy;
	int ret;

	psy = power_supply_register(parent, desc, cfg);
	if (IS_ERR(psy))
		return psy;

	ret = devm_add_action_or_reset(parent,
				       power_supply_devm_unregister, psy);
	if (ret)
		return ERR_PTR(ret);
	return psy;
}

void power_supply_unregister(struct power_supply *psy)
{
	struct port_power_supply *port;
	struct port_power_supply_snapshot removed = {
		.event_type = PORT_POWER_SUPPLY_REMOVED,
	};

	cancel_work_sync(&psy->changed_work);
	port = container_of(psy, struct port_power_supply, psy);
	removed.proxy_id = psy->port_proxy_id;
	(void)xQueueOverwrite(port->event_queue, &removed);
	kfree(port);
}

void *power_supply_get_drvdata(struct power_supply *psy)
{
	return psy->drv_data;
}

int power_supply_powers(struct power_supply *psy, struct device *dev)
{
	psy->powered_dev = dev;
	return 0;
}

void power_supply_changed(struct power_supply *psy)
{
	(void)__atomic_add_fetch(&psy->changed_count, 1u, __ATOMIC_ACQ_REL);
	(void)schedule_work(&psy->changed_work);
}

void power_supply_external_power_changed(struct power_supply *psy)
{
	if (psy->desc->external_power_changed)
		psy->desc->external_power_changed(psy);
}

int power_supply_get_property(struct power_supply *psy,
			      enum power_supply_property property,
			      union power_supply_propval *value)
{
	return psy->desc->get_property(psy, property, value);
}
