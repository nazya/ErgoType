#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"

#include "device.h"
#include "forward.h"
#include "hid.h"

static struct hid hid_table[MAX_DEVICES];
static size_t hid_table_sz;

static struct hid *hid_lookup(uint8_t dev_addr, uint8_t instance)
{
	for (size_t i = 0; i < hid_table_sz; i++)
		if (hid_table[i].dev_addr == dev_addr &&
		    hid_table[i].instance == instance)
			return &hid_table[i];

	return NULL;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
	if (hid_table_sz >= MAX_DEVICES)
		return;

	struct hid *hid = &hid_table[hid_table_sz];

	memset(hid, 0, sizeof(*hid));
	hid->dev_addr = dev_addr;
	hid->instance = instance;
	hid->fields = pvPortMalloc(sizeof(*hid->fields) * HOST_HID_MAX_FIELDS);
	hid->reports = pvPortMalloc(sizeof(*hid->reports) * HOST_HID_MAX_REPORTS);

	if (!hid->fields || !hid->reports) {
		vPortFree(hid->fields);
		vPortFree(hid->reports);
		memset(hid, 0, sizeof(*hid));
		return;
	}

	memset(hid->fields, 0, sizeof(*hid->fields) * HOST_HID_MAX_FIELDS);
	memset(hid->reports, 0, sizeof(*hid->reports) * HOST_HID_MAX_REPORTS);

	if (desc_report && desc_len)
		hid_parse_fields(hid->fields, &hid->field_count, &hid->has_report_id, desc_report, desc_len);

	uint8_t caps = 0;

	for (uint8_t i = 0; i < hid->field_count; i++)
		caps |= hid_field_caps(&hid->fields[i]);

	switch (tuh_hid_interface_protocol(dev_addr, instance)) {
	case HID_ITF_PROTOCOL_KEYBOARD:
		caps |= CAP_KEY | CAP_KEYBOARD;
		break;
	case HID_ITF_PROTOCOL_MOUSE:
		caps |= CAP_MOUSE;
		break;
	default:
		break;
	}

	if (!caps && !hid->field_count) {
		vPortFree(hid->fields);
		vPortFree(hid->reports);
		memset(hid, 0, sizeof(*hid));
		return;
	}

	uint16_t vid = 0;
	uint16_t pid = 0;

	tuh_vid_pid_get(dev_addr, &vid, &pid);

	hid->device.ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct device_event));
	configASSERT(hid->device.ev_queue);
	hid->device.capabilities = caps;
	snprintf(hid->device.id, sizeof(hid->device.id), "%04x:%04x", vid, pid);

	const char *name = "usb-hid";

	if ((caps & (CAP_KEYBOARD | CAP_MOUSE)) == CAP_KEYBOARD)
		name = "usb-keyboard";
	else if ((caps & (CAP_KEYBOARD | CAP_MOUSE)) == CAP_MOUSE)
		name = "usb-mouse";
	snprintf(hid->device.name, sizeof(hid->device.name), "%s", name);

	hid_table_sz++;
	device_add(&hid->device);
	tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
	struct hid *hid = hid_lookup(dev_addr, instance);

	if (!hid)
		return;

	size_t idx = (size_t)(hid - hid_table);
	forward_removed(&hid->device);
	vPortFree(hid->fields);
	vPortFree(hid->reports);

	hid_table_sz--;
	for (size_t i = idx; i < hid_table_sz; i++)
		hid_table[i] = hid_table[i + 1];
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{
	struct hid *hid = hid_lookup(dev_addr, instance);
	uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
	uint8_t protocol_mode = tuh_hid_get_protocol(dev_addr, instance);

	if (protocol_mode == HID_PROTOCOL_REPORT && hid->field_count) {
		if (hid->has_report_id) {
			if (len)
				handle_hid_report(hid, report[0], report + 1, len - 1u);
		} else {
			handle_hid_report(hid, 0, report, len);
		}
	} else {
		switch (protocol) {
		case HID_ITF_PROTOCOL_KEYBOARD:
			if (len >= sizeof(hid_keyboard_report_t))
				handle_keyboard_report(hid, (const hid_keyboard_report_t *)report);
			break;
		case HID_ITF_PROTOCOL_MOUSE:
			if (len >= sizeof(hid_mouse_report_t))
				handle_mouse_report(hid, (const hid_mouse_report_t *)report);
			break;
		default:
			break;
		}
	}

	tuh_hid_receive_report(dev_addr, instance);
}
