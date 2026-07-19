#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "tusb.h"
#include "host/hcd.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "rtos/freertos_hook.h"
#include "usbhid.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"
#include "stdio_tusb_cdc.h"
#include "linux/include/linux/hiddev.h"
#include "linux/include/linux/usb.h"

/*
 * TinyUSB-to-Linux USB HID transport glue.
 *
 * This is not a line-preserving port of drivers/hid/usbhid/hid-core.c. It owns
 * the firmware boundary that upstream usbhid normally gets from Linux USB core:
 * device/interface snapshots before hid_add_device(), HID ll_driver callbacks,
 * async report/control submission, and disconnect handoff out of TinyUSB
 * callbacks.
 */

#define HID_HOST_MAX_DEVICES CFG_TUH_HID
#define HID_HOST_RAW_INTERFACE_MAX HID_HOST_MAX_DEVICES
#define USBHID_LIFECYCLE_QUEUE_LEN 1
#define USBHID_STRING_LANGID 0x0409u
#define USBHID_USB_DEVICE_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
/* One bounded spare lets a fast replug coexist with one retiring cache entry. */
#define USBHID_USB_DEVICE_SLOTS (USBHID_USB_DEVICE_MAX + 1)
#define USBHID_USB_MAXCHILD 31
#define USBHID_REPORT_DESCRIPTOR_SLOTS HID_HOST_MAX_DEVICES
#define USBHID_REPORT_DESCRIPTOR_MAX CFG_TUH_ENUMERATION_BUFSIZE

static const struct hid_ll_driver usb_hid_driver;
const struct device_type usb_device_type = {
	.name = "usb_device",
};
const struct device_type usb_if_device_type = {
	.name = "usb_interface",
};
static struct hid_device *usbhid_devices[HID_HOST_MAX_DEVICES];
static QueueHandle_t usbhid_lifecycle_queue;

enum usbhid_lifecycle_kind {
	USBHID_LIFECYCLE_WAKE,
};

struct usbhid_lifecycle_event {
	enum usbhid_lifecycle_kind kind;
};

struct usbhid_sync_request {
	TaskHandle_t task;
	u8 *buf;
	size_t bufsize;
	size_t actual_len;
	int status;
	volatile bool done;
};

/* Padded GET buffer owned from .request enqueue through parser completion. */
struct usbhid_control_input {
	TaskHandle_t parser_owner;
	u16 bufsize;
	u8 data[];
};

enum usbhid_report_descriptor_state {
	USBHID_REPORT_DESCRIPTOR_FREE,
	USBHID_REPORT_DESCRIPTOR_WAIT_PREPROBE,
	USBHID_REPORT_DESCRIPTOR_READY,
	USBHID_REPORT_DESCRIPTOR_PROBING,
};

struct usbhid_report_descriptor_slot {
	enum usbhid_report_descriptor_state state;
	u8 dev_addr;
	u8 instance;
	u16 len;
	u32 generation;
	u32 serial;
	u8 data[USBHID_REPORT_DESCRIPTOR_MAX];
};

struct usbhid_probe_token {
	u8 descriptor_slot;
	u8 dev_addr;
	u8 instance;
	u16 len;
	u32 device_generation;
	u32 descriptor_serial;
};

enum usbhid_transport_fault {
	USBHID_FAULT_DEVICE_CACHE_FULL = 1u << 0,
	USBHID_FAULT_REPORT_DESCRIPTOR = 1u << 1,
	USBHID_FAULT_ASYNC_CANCEL = 1u << 2,
	USBHID_FAULT_REPORT_DROP = 1u << 3,
	USBHID_FAULT_PROTOCOL_BOOT = 1u << 4,
	USBHID_FAULT_RX_REARM = 1u << 5,
	USBHID_FAULT_RAW_INTERFACE = 1u << 6,
	USBHID_FAULT_TOPOLOGY = 1u << 7,
	USBHID_FAULT_RX_STALL = 1u << 8,
	USBHID_FAULT_RX_XFER = 1u << 9,
};

enum usbhid_preprobe_stage {
	USBHID_PREPROBE_DEVICE_DESC,
	USBHID_PREPROBE_LANGID,
	USBHID_PREPROBE_PRODUCT,
	USBHID_PREPROBE_MANUFACTURER,
	USBHID_PREPROBE_SERIAL,
	USBHID_PREPROBE_DONE,
};

struct usbhid_usb_device {
	bool valid;
	bool retiring;
	bool have_device_desc;
	bool device_desc_requested;
	u32 generation;
	u32 interface_mask;
	enum usbhid_preprobe_stage preprobe_stage;
	uint16_t string_langid;
	struct usb_device dev;
	struct usbhid_usb_device *retired_next;
};

struct usbhid_raw_interface {
	bool valid;
	uint8_t dev_addr;
	uint8_t ifnum;
	uint8_t subclass;
	uint8_t protocol;
	uint8_t endpoint_count;
	u8 hid_descriptor[sizeof(struct hid_descriptor)];
	struct usb_host_endpoint endpoint[USB_HOST_ENDPOINT_MAX];
};

struct usbhid_transport_pool {
	struct usbhid_usb_device devices[USBHID_USB_DEVICE_SLOTS];
	struct usbhid_report_descriptor_slot
		report_descriptors[USBHID_REPORT_DESCRIPTOR_SLOTS];
};

static struct usb_device usbhid_root_hub;
static bool usbhid_root_hub_valid;
static struct usbhid_transport_pool *usbhid_transport_pool;
static struct usbhid_usb_device *usbhid_usb_devices;
static struct usbhid_usb_device *usbhid_retired_devices;
static struct usbhid_report_descriptor_slot *usbhid_report_descriptors;
static struct usbhid_raw_interface usbhid_raw_interfaces[HID_HOST_RAW_INTERFACE_MAX];
static u32 usbhid_generation;
static u32 usbhid_report_descriptor_serial;
static u32 usbhid_transport_faults;
static bool usbhid_lifecycle_wake_pending;
static void usbhid_io_wait_idle(struct hid_device *hid);
static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype);
static void usbhid_lifecycle_kick(void);
static void usbhid_transport_fault(enum usbhid_transport_fault fault);
static void usbhid_backend_device_detach(uint8_t dev_addr);

static u32 usbhid_next_generation(void)
{
	u32 generation;

	taskENTER_CRITICAL();
	generation = ++usbhid_generation;
	if (!generation)
		generation = ++usbhid_generation;
	taskEXIT_CRITICAL();
	return generation;
}

static void usbhid_usb_device_init_config(struct usb_device *dev)
{
	dev->config = &dev->config_storage;
	dev->actconfig = &dev->config_storage;
}

static void usbhid_usb_device_apply_topology(struct usb_device *dev)
{
	hcd_devtree_info_t info;

	/*
	 * Upstream Linux USB core fills bus/devpath/topology before usbhid_probe().
	 * TinyUSB keeps the same data in its device tree; copy it into the local
	 * usb_device shim so imported HID drivers see stable USB identity fields.
	 */
	hcd_devtree_get_info(dev->dev_addr, &info);
	dev->rhport = info.rhport;
	dev->hub_addr = info.hub_addr;
	dev->hub_port = info.hub_port;
	dev->portnum = info.hub_port;
	dev->speed = info.speed;
	dev->topology_valid = true;
}

static struct usb_device *usbhid_usb_root_hub(uint8_t rhport)
{
	if (!usbhid_root_hub_valid) {
		device_initialize(&usbhid_root_hub.dev);
		usbhid_root_hub.dev.type = &usb_device_type;
		usbhid_usb_device_init_config(&usbhid_root_hub);
		usbhid_root_hub.maxchild = USBHID_USB_MAXCHILD;
		usbhid_root_hub_valid = true;
	}

	usbhid_root_hub.rhport = rhport;
	usbhid_root_hub.topology_valid = true;
	return &usbhid_root_hub;
}

static struct usbhid_usb_device *usbhid_usb_device_find(uint8_t dev_addr)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->valid && entry->dev.dev_addr == dev_addr)
			return entry;
	}

	return NULL;
}

static struct usbhid_usb_device *usbhid_usb_device_slot(uint8_t dev_addr)
{
	struct usbhid_usb_device *free_slot = NULL;

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->valid && entry->dev.dev_addr == dev_addr)
			return entry;
		if (!entry->valid && !entry->retiring && !free_slot)
			free_slot = entry;
	}

	return free_slot;
}

static struct usb_device *usbhid_usb_device_parent(
		const struct usb_device *dev)
{
	struct usbhid_usb_device *hub;

	if (!dev->hub_addr)
		return usbhid_usb_root_hub(dev->rhport);

	hub = usbhid_usb_device_find(dev->hub_addr);
	return hub ? &hub->dev : NULL;
}

static void usbhid_usb_device_copy(struct usbhid_usb_device *entry,
				   const struct usb_device *src,
				   struct usb_device *parent)
{
	struct usb_device *dst = &entry->dev;

	if (!entry->have_device_desc)
		dst->descriptor = src->descriptor;
	if (src->actconfig && src->actconfig->desc.bNumInterfaces)
		dst->config_storage.desc = src->actconfig->desc;
	usbhid_usb_device_init_config(dst);
	dst->dev_addr = src->dev_addr;
	dst->rhport = src->rhport;
	dst->hub_addr = src->hub_addr;
	dst->hub_port = src->hub_port;
	dst->portnum = src->portnum;
	dst->speed = src->speed;
	dst->topology_valid = src->topology_valid;
	dst->maxchild = USBHID_USB_MAXCHILD;
	dst->parent = parent;
	if (src->product && src->product[0]) {
		strscpy(dst->product_buf, src->product, sizeof(dst->product_buf));
		dst->product = dst->product_buf;
	}
}

static struct usbhid_usb_device *
usbhid_usb_device_upsert(const struct usb_device *src)
{
	struct usbhid_usb_device *entry;
	struct usb_device *parent = usbhid_usb_device_parent(src);

	/* A child without its live hub epoch must never masquerade as root-owned. */
	if (!parent) {
		usbhid_transport_fault(USBHID_FAULT_TOPOLOGY);
		return NULL;
	}

	entry = usbhid_usb_device_slot(src->dev_addr);
	if (!entry) {
		usbhid_transport_fault(USBHID_FAULT_DEVICE_CACHE_FULL);
		return NULL;
	}

	if (!entry->valid) {
		memset(entry, 0, sizeof(*entry));
		device_initialize(&entry->dev.dev);
		entry->dev.dev.type = &usb_device_type;
		usbhid_usb_device_init_config(&entry->dev);
		entry->generation = usbhid_next_generation();
	}
	entry->valid = true;
	usbhid_usb_device_copy(entry, src, parent);
	return entry;
}

static struct usbhid_usb_device *usbhid_usb_device_prepare(uint8_t dev_addr)
{
	struct usb_device dev = { 0 };
	uint16_t vid;
	uint16_t pid;

	dev.dev_addr = dev_addr;
	usbhid_usb_device_apply_topology(&dev);
	dev.maxchild = USBHID_USB_MAXCHILD;
	if (tuh_vid_pid_get(dev_addr, &vid, &pid)) {
		dev.descriptor.idVendor = vid;
		dev.descriptor.idProduct = pid;
	}

	return usbhid_usb_device_upsert(&dev);
}

static void usbhid_report_descriptor_free(
		struct usbhid_report_descriptor_slot *slot)
{
	slot->state = USBHID_REPORT_DESCRIPTOR_FREE;
	slot->len = 0;
}

static u32 usbhid_next_report_descriptor_serial_locked(void)
{
	u32 serial = ++usbhid_report_descriptor_serial;

	if (!serial)
		serial = ++usbhid_report_descriptor_serial;
	return serial;
}

static void usbhid_report_descriptor_invalidate(uint8_t dev_addr,
						 u32 generation,
						 bool match_generation,
						 int instance)
{
	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		struct usbhid_report_descriptor_slot *slot =
			&usbhid_report_descriptors[i];

		if (slot->dev_addr != dev_addr ||
		    (match_generation && slot->generation != generation) ||
		    (instance >= 0 && slot->instance != (u8)instance))
			continue;

		/*
		 * PROBING already owns a task-side copy. Rotating the serial revokes
		 * its token while making callback ingress capacity reusable at once.
		 */
		slot->serial = usbhid_next_report_descriptor_serial_locked();
		usbhid_report_descriptor_free(slot);
	}
	taskEXIT_CRITICAL();
}

static void usbhid_usb_device_drop_pending_hid(struct usbhid_usb_device *entry)
{
	usbhid_report_descriptor_invalidate(entry->dev.dev_addr,
					    entry->generation, true, -1);
}

static void usbhid_usb_device_drop_pending_hid_instance(
		uint8_t dev_addr, uint8_t instance)
{
	usbhid_report_descriptor_invalidate(dev_addr, 0, false, instance);
}

static struct usbhid_usb_device *
usbhid_usb_device_mark_retiring(uint8_t dev_addr)
{
	struct usbhid_usb_device *entry = NULL;

	/*
	 * Make the old cache epoch invisible and non-reusable before transport
	 * cancellation. It is not published to the lifecycle task yet, so its state
	 * remains alive across the split cutover even if another core is running.
	 */
	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *candidate = &usbhid_usb_devices[i];

		if (candidate->valid &&
		    candidate->dev.dev_addr == dev_addr) {
			entry = candidate;
			break;
		}
	}
	if (entry) {
		entry->valid = false;
		entry->retiring = true;
	}
	taskEXIT_CRITICAL();

	return entry;
}

static void
usbhid_usb_device_publish_retired(struct usbhid_usb_device *entry)
{
	if (!entry)
		return;

	taskENTER_CRITICAL();
	entry->retired_next = usbhid_retired_devices;
	usbhid_retired_devices = entry;
	taskEXIT_CRITICAL();
}

static struct usbhid_usb_device *usbhid_usb_device_take_retired(void)
{
	struct usbhid_usb_device *entry;

	taskENTER_CRITICAL();
	entry = usbhid_retired_devices;
	if (entry) {
		usbhid_retired_devices = entry->retired_next;
		entry->retired_next = NULL;
	}
	taskEXIT_CRITICAL();
	return entry;
}

static bool usbhid_usb_device_has_live_children(
		const struct usbhid_usb_device *parent)
{
	bool found = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *child = &usbhid_usb_devices[i];

		if (child != parent && (child->valid || child->retiring) &&
		    child->dev.parent == &parent->dev) {
			found = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return found;
}

static bool usbhid_usb_device_has_live_hids(
		const struct usbhid_usb_device *entry)
{
	bool found = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		const struct hid_device *hid = usbhid_devices[i];
		const struct usbhid_device *usbhid =
			hid ? hid->driver_data : NULL;

		if (usbhid && interface_to_usbdev(usbhid->intf) == &entry->dev) {
			found = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return found;
}

static void usbhid_usb_device_requeue_retired(
		struct usbhid_usb_device *entries)
{
	taskENTER_CRITICAL();
	while (entries) {
		struct usbhid_usb_device *entry = entries;

		entries = entry->retired_next;
		entry->retired_next = usbhid_retired_devices;
		usbhid_retired_devices = entry;
	}
	taskEXIT_CRITICAL();
}

static int usbhid_usb_device_release_retired(void)
{
	struct usbhid_usb_device *entry;
	struct usbhid_usb_device *blocked = NULL;
	bool released = false;
	int ret;

	while ((entry = usbhid_usb_device_take_retired())) {
		/* TinyUSB closes a hub before its subtree; reuse cache leaves first. */
		if (usbhid_usb_device_has_live_children(entry) ||
		    usbhid_usb_device_has_live_hids(entry)) {
			entry->retired_next = blocked;
			blocked = entry;
			continue;
		}

		ret = hid_async_synchronize_preprobe();
		if (ret) {
			entry->retired_next = blocked;
			blocked = entry;
			usbhid_usb_device_requeue_retired(blocked);
			return ret;
		}

		usbhid_usb_device_drop_pending_hid(entry);
		taskENTER_CRITICAL();
		entry->retiring = false;
		taskEXIT_CRITICAL();
		released = true;
	}
	usbhid_usb_device_requeue_retired(blocked);
	if (blocked && released)
		usbhid_lifecycle_kick();

	return 0;
}

struct usb_device *usb_hub_find_child(struct usb_device *hdev, int port1)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->valid && entry->dev.parent == hdev &&
		    entry->dev.portnum == port1)
			return &entry->dev;
	}

	return NULL;
}

static bool usbhid_raw_interface_init(void)
{
	memset(usbhid_raw_interfaces, 0, sizeof(usbhid_raw_interfaces));
	return true;
}

static bool usbhid_raw_interface_deinit(void)
{
	memset(usbhid_raw_interfaces, 0, sizeof(usbhid_raw_interfaces));
	return true;
}

static void usbhid_usb_device_count_interfaces(uint8_t rhport, uint8_t dev_addr,
					       tusb_desc_interface_t const *desc,
					       uint16_t max_len)
{
	struct usbhid_usb_device *entry;
	struct usb_device dev = { 0 };
	const uint8_t *p = (const uint8_t *)desc;
	const uint8_t *end = p + max_len;
	u32 interface_mask = 0;

	dev.dev_addr = dev_addr;
	usbhid_usb_device_apply_topology(&dev);
	dev.rhport = rhport;
	entry = usbhid_usb_device_upsert(&dev);
	if (!entry)
		return;

	while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
		/*
		 * TinyUSB passes the untrusted configuration byte stream here before
		 * its class driver opens the interface. Do not cast a short descriptor.
		 */
		if (p[1] == TUSB_DESC_INTERFACE &&
		    p[0] >= sizeof(tusb_desc_interface_t)) {
			const tusb_desc_interface_t *interface =
				(const tusb_desc_interface_t *)p;

			if (interface->bInterfaceNumber < 32)
				interface_mask |= 1u << interface->bInterfaceNumber;
		}

		p += p[0];
	}
	taskENTER_CRITICAL();
	entry->interface_mask |= interface_mask;
	entry->dev.config_storage.desc.bNumInterfaces =
		(u8)__builtin_popcount(entry->interface_mask);
	taskEXIT_CRITICAL();
}

static void usbhid_raw_interface_store(uint8_t dev_addr,
				       tusb_desc_interface_t const *desc,
				       uint16_t max_len)
{
	struct usbhid_raw_interface snapshot = {
		.valid = true,
		.dev_addr = dev_addr,
		.ifnum = desc->bInterfaceNumber,
		.subclass = desc->bInterfaceSubClass,
		.protocol = desc->bInterfaceProtocol,
	};
	struct usbhid_raw_interface *free_slot = NULL;
	const uint8_t *p;
	const uint8_t *end;

	p = (const uint8_t *)desc + desc->bLength;
	end = (const uint8_t *)desc + max_len;
	while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
		if (p[1] == TUSB_DESC_INTERFACE)
			break;

		if (p[1] == HID_DT_HID && !snapshot.hid_descriptor[1]) {
			/*
			 * Upstream USB core gives usbhid_parse() the HID class
			 * descriptor. Preserve its identity fields before TinyUSB's
			 * HID driver consumes the same configuration stream. Copy the
			 * short descriptor too, so parse() can reject it without reading
			 * beyond the configuration bytes.
			 */
			memcpy(snapshot.hid_descriptor, p,
			       min_t(size_t, p[0],
				     sizeof(snapshot.hid_descriptor)));
		}

		/* The raw configuration stream may contain a malformed short endpoint. */
		if (p[1] == USB_DT_ENDPOINT &&
		    p[0] >= sizeof(tusb_desc_endpoint_t) &&
		    snapshot.endpoint_count < USB_HOST_ENDPOINT_MAX) {
			const tusb_desc_endpoint_t *ep =
				(const tusb_desc_endpoint_t *)p;
			struct usb_host_endpoint *host_ep =
				&snapshot.endpoint[snapshot.endpoint_count++];

			host_ep->desc.bLength = ep->bLength;
			host_ep->desc.bDescriptorType = ep->bDescriptorType;
			host_ep->desc.bEndpointAddress = ep->bEndpointAddress;
			host_ep->desc.bmAttributes = ep->bmAttributes.xfer;
			host_ep->desc.wMaxPacketSize = ep->wMaxPacketSize;
			host_ep->desc.bInterval = ep->bInterval;
		}

		p += p[0];
	}

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (!raw->valid) {
			if (!free_slot)
				free_slot = raw;
			continue;
		}

		if (raw->dev_addr == dev_addr &&
		    raw->ifnum == desc->bInterfaceNumber) {
			free_slot = raw;
			break;
		}
	}

	if (!free_slot) {
		taskEXIT_CRITICAL();
		usbhid_transport_fault(USBHID_FAULT_RAW_INTERFACE);
		return;
	}

	*free_slot = snapshot;
	taskEXIT_CRITICAL();
}

static bool usbhid_raw_interface_copy(uint8_t dev_addr, uint8_t ifnum,
				      struct usbhid_raw_interface *snapshot)
{
	bool found = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		const struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr && raw->ifnum == ifnum) {
			*snapshot = *raw;
			found = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return found;
}

static void usbhid_raw_interface_close(uint8_t dev_addr)
{
	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr)
			raw->valid = false;
	}
	taskEXIT_CRITICAL();

	/* TinyUSB omits tuh_umount_cb() for hubs but closes every app driver. */
	usbhid_backend_device_detach(dev_addr);
}

static bool usbhid_raw_interface_open(uint8_t rhport, uint8_t dev_addr,
				      tusb_desc_interface_t const *desc,
				      uint16_t max_len)
{
	// Upstream Linux USB core has already parsed the active configuration
	// before usbhid_probe(). The TinyUSB app driver sees each interface
	// descriptor during config parsing, so build the small config metadata
	// imported HID drivers inspect from that stream.
	usbhid_usb_device_count_interfaces(rhport, dev_addr, desc, max_len);
	if (desc->bInterfaceClass == TUSB_CLASS_HID)
		usbhid_raw_interface_store(dev_addr, desc, max_len);

	/*
	 * Upstream Linux: no equivalent; Linux USB core passes the original
	 * usb_interface to usbhid_probe(). TinyUSB normalizes HID protocol in
	 * its HID class driver, so this app driver snapshots raw interface
	 * fields first and returns false to leave normal TinyUSB HID open active.
	 */
	return false;
}

static const usbh_class_driver_t usbhid_raw_interface_driver[] = {
	{
		.name = "HID raw interface snapshot",
		.init = usbhid_raw_interface_init,
		.deinit = usbhid_raw_interface_deinit,
		.open = usbhid_raw_interface_open,
		.close = usbhid_raw_interface_close,
	},
};

usbh_class_driver_t const *usbhid_backend_app_driver_get(uint8_t *driver_count)
{
	*driver_count = TU_ARRAY_SIZE(usbhid_raw_interface_driver);
	return usbhid_raw_interface_driver;
}

static struct hid_device *usbhid_lookup(uint8_t dev_addr, uint8_t instance)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && usbhid->dev_addr == dev_addr &&
		    usbhid->instance == instance)
			return hid;
	}

	return NULL;
}

static int usbhid_insert(struct hid_device *hid)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (!usbhid_devices[i]) {
			usbhid_devices[i] = hid;
			return 0;
		}
	}

	return -ENOMEM;
}

static void usbhid_remove_slot(struct hid_device *hid)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (usbhid_devices[i] == hid) {
			usbhid_devices[i] = NULL;
			return;
		}
	}
}

static int usbhid_insert_if_generation(struct hid_device *hid,
					 u32 device_generation,
					 const struct usbhid_probe_token *token)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_usb_device *entry;
	struct usbhid_report_descriptor_slot *slot = NULL;
	int ret = -ENODEV;

	taskENTER_CRITICAL();
	entry = usbhid_usb_device_find(usbhid->dev_addr);
	if (token && token->descriptor_slot < USBHID_REPORT_DESCRIPTOR_SLOTS)
		slot = &usbhid_report_descriptors[token->descriptor_slot];
	if (entry && entry->generation == device_generation && slot &&
	    usbhid->intf->dev.parent == &entry->dev.dev &&
	    token->dev_addr == usbhid->dev_addr &&
	    token->instance == usbhid->instance &&
	    slot->state == USBHID_REPORT_DESCRIPTOR_PROBING &&
	    slot->dev_addr == token->dev_addr &&
	    slot->instance == token->instance &&
	    slot->generation == token->device_generation &&
	    slot->serial == token->descriptor_serial &&
	    tuh_hid_mounted(usbhid->dev_addr, usbhid->instance))
		ret = usbhid_insert(hid);
	taskEXIT_CRITICAL();
	return ret;
}

static int usbhid_probe(struct usbhid_usb_device *usb_entry,
			uint8_t instance, uint8_t *desc_report, uint16_t desc_len,
			const struct usbhid_probe_token *token);
static void usbhid_usb_device_descriptor_complete(const struct hid_async_request *req,
						  int status);

static int usbhid_usb_device_store_pending_probe(struct usbhid_usb_device *entry,
						 uint8_t instance,
						 uint8_t const *desc_report,
						 uint16_t desc_len)
{
	struct usbhid_report_descriptor_slot *slot = NULL;
	bool already_probing = false;
	bool stored = false;

	if (!desc_report || !desc_len)
		return -ENODEV;
	if (desc_len > USBHID_REPORT_DESCRIPTOR_MAX)
		return -E2BIG;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		struct usbhid_report_descriptor_slot *candidate =
			&usbhid_report_descriptors[i];

		if (candidate->state != USBHID_REPORT_DESCRIPTOR_FREE &&
		    candidate->dev_addr == entry->dev.dev_addr &&
		    candidate->instance == instance &&
		    candidate->generation == entry->generation) {
			if (candidate->state ==
				    USBHID_REPORT_DESCRIPTOR_PROBING) {
				already_probing = true;
				break;
			}
			slot = candidate;
			break;
		}
		if (candidate->state == USBHID_REPORT_DESCRIPTOR_FREE && !slot)
			slot = candidate;
	}

	if (slot && entry->valid) {
		slot->state = USBHID_REPORT_DESCRIPTOR_WAIT_PREPROBE;
		slot->dev_addr = entry->dev.dev_addr;
		slot->instance = instance;
		slot->len = desc_len;
		slot->generation = entry->generation;
		slot->serial = usbhid_next_report_descriptor_serial_locked();
		memcpy(slot->data, desc_report, desc_len);
		stored = true;
		if (entry->have_device_desc &&
		    entry->preprobe_stage == USBHID_PREPROBE_DONE) {
			slot->state = USBHID_REPORT_DESCRIPTOR_READY;
		}
	}
	taskEXIT_CRITICAL();

	/* TinyUSB does not normally repeat mount for a live interface. */
	if (already_probing)
		return 0;
	if (!stored)
		return -ENOMEM;
	/* READY work and deferred pre-probe submission are both authoritative. */
	usbhid_lifecycle_kick();
	return 0;
}

static bool usbhid_report_descriptor_token_current(
		const struct usbhid_probe_token *token)
{
	const struct usbhid_report_descriptor_slot *slot;
	bool valid_token = false;

	if (!token || token->descriptor_slot >= USBHID_REPORT_DESCRIPTOR_SLOTS)
		return false;

	taskENTER_CRITICAL();
	slot = &usbhid_report_descriptors[token->descriptor_slot];
	valid_token = slot->state == USBHID_REPORT_DESCRIPTOR_PROBING &&
		  slot->dev_addr == token->dev_addr &&
		  slot->instance == token->instance &&
		  slot->generation == token->device_generation &&
		  slot->serial == token->descriptor_serial;
	taskEXIT_CRITICAL();
	return valid_token;
}

static int usbhid_report_descriptor_take_ready(
		struct usbhid_probe_token *token, u8 **desc_report)
{
	struct usbhid_report_descriptor_slot *slot = NULL;
	u8 *copy;
	bool valid_token;

	memset(token, 0, sizeof(*token));
	*desc_report = NULL;
	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		if (usbhid_report_descriptors[i].state !=
		    USBHID_REPORT_DESCRIPTOR_READY)
			continue;
		slot = &usbhid_report_descriptors[i];
		token->descriptor_slot = (u8)i;
		token->dev_addr = slot->dev_addr;
		token->instance = slot->instance;
		token->len = slot->len;
		token->device_generation = slot->generation;
		token->descriptor_serial = slot->serial;
		break;
	}
	taskEXIT_CRITICAL();

	if (!slot)
		return 0;

	copy = kmalloc(token->len, GFP_KERNEL);
	if (!copy)
		return -ENOMEM;

	taskENTER_CRITICAL();
	slot = &usbhid_report_descriptors[token->descriptor_slot];
	valid_token = slot->state == USBHID_REPORT_DESCRIPTOR_READY &&
		  slot->dev_addr == token->dev_addr &&
		  slot->instance == token->instance &&
		  slot->generation == token->device_generation &&
		  slot->serial == token->descriptor_serial &&
		  slot->len == token->len;
	if (valid_token) {
		memcpy(copy, slot->data, token->len);
		slot->state = USBHID_REPORT_DESCRIPTOR_PROBING;
		slot->len = 0;
	}
	taskEXIT_CRITICAL();

	if (!valid_token) {
		kfree(copy);
		return -EAGAIN;
	}

	*desc_report = copy;
	return 1;
}

static void usbhid_report_descriptor_finish_probe(
		const struct usbhid_probe_token *token)
{
	struct usbhid_report_descriptor_slot *slot;

	if (!token || token->descriptor_slot >= USBHID_REPORT_DESCRIPTOR_SLOTS)
		return;

	taskENTER_CRITICAL();
	slot = &usbhid_report_descriptors[token->descriptor_slot];
	if (slot->state == USBHID_REPORT_DESCRIPTOR_PROBING &&
	    slot->dev_addr == token->dev_addr &&
	    slot->instance == token->instance &&
	    slot->generation == token->device_generation &&
	    slot->serial == token->descriptor_serial)
		usbhid_report_descriptor_free(slot);
	taskEXIT_CRITICAL();
}

static bool usbhid_usb_device_has_string_indexes(const struct usbhid_usb_device *entry)
{
	return entry->dev.descriptor.iProduct ||
	       entry->dev.descriptor.iManufacturer ||
	       entry->dev.descriptor.iSerialNumber;
}

// Upstream Linux keeps this decode inside drivers/usb/core/message.c::usb_string().
// Local disabled USB-core shim has this extracted as usb_string_decode() in
// usb_host/linux/include/linux/usb.h.
// The blocking usb_string()/usb_control_msg() path stays disabled in this port;
// pre-probe only needs the decode step after hid_async fetches the descriptor.
static int usb_string_decode(const u8 *desc, uint16_t actual,
			     char *buf, size_t size)
{
	size_t out = 0;
	u8 len;

	if (!size)
		return -EINVAL;

	buf[0] = '\0';

	if (actual < 2 || desc[1] != USB_DT_STRING)
		return -EINVAL;

	len = desc[0];
	if (len > actual)
		len = (u8)actual;

	for (size_t i = 2; i + 1u < len; i += 2u) {
		u16 c = (u16)desc[i] | ((u16)desc[i + 1u] << 8);

		if (c >= 0xd800u && c <= 0xdfffu)
			c = '?';

		if (c < 0x80u) {
			if (out + 1u >= size)
				break;
			buf[out++] = (char)c;
		} else if (c < 0x800u) {
			if (out + 2u >= size)
				break;
			buf[out++] = (char)(0xc0u | (c >> 6));
			buf[out++] = (char)(0x80u | (c & 0x3fu));
		} else {
			if (out + 3u >= size)
				break;
			buf[out++] = (char)(0xe0u | (c >> 12));
			buf[out++] = (char)(0x80u | ((c >> 6) & 0x3fu));
			buf[out++] = (char)(0x80u | (c & 0x3fu));
		}
	}

	buf[out] = '\0';
	return (int)out;
}

static int usbhid_usb_device_queue_string(struct usbhid_usb_device *entry,
					  uint8_t index, u32 generation)
{
	return hid_async_queue_string_descriptor(entry->dev.dev_addr, index,
						 entry->string_langid, generation,
						 usbhid_usb_device_descriptor_complete,
						 entry);
}

static void usbhid_usb_device_finish_preprobe(struct usbhid_usb_device *entry)
{
	bool ready = false;

	taskENTER_CRITICAL();
	if (entry->valid) {
		entry->preprobe_stage = USBHID_PREPROBE_DONE;
		for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
			struct usbhid_report_descriptor_slot *slot =
				&usbhid_report_descriptors[i];

			if (slot->state !=
				    USBHID_REPORT_DESCRIPTOR_WAIT_PREPROBE ||
			    slot->dev_addr != entry->dev.dev_addr ||
			    slot->generation != entry->generation)
				continue;
			slot->state = USBHID_REPORT_DESCRIPTOR_READY;
			ready = true;
		}
	}
	taskEXIT_CRITICAL();

	if (ready)
		usbhid_lifecycle_kick();
}

static void usbhid_usb_device_queue_next_string(struct usbhid_usb_device *entry,
						u32 generation)
{
	int ret;

	while (entry->preprobe_stage != USBHID_PREPROBE_DONE) {
		switch (entry->preprobe_stage) {
		case USBHID_PREPROBE_LANGID:
			if (!usbhid_usb_device_has_string_indexes(entry)) {
				usbhid_usb_device_finish_preprobe(entry);
				return;
			}
			ret = hid_async_queue_string_descriptor(entry->dev.dev_addr,
							       0, 0, generation,
							       usbhid_usb_device_descriptor_complete,
							       entry);
			break;
		case USBHID_PREPROBE_PRODUCT:
			if (!entry->dev.descriptor.iProduct) {
				entry->preprobe_stage = USBHID_PREPROBE_MANUFACTURER;
				continue;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iProduct,
							     generation);
			break;
		case USBHID_PREPROBE_MANUFACTURER:
			if (!entry->dev.descriptor.iManufacturer) {
				entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
				continue;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iManufacturer,
							     generation);
			break;
		case USBHID_PREPROBE_SERIAL:
			if (!entry->dev.descriptor.iSerialNumber) {
				usbhid_usb_device_finish_preprobe(entry);
				return;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iSerialNumber,
							     generation);
			break;
		default:
			usbhid_usb_device_finish_preprobe(entry);
			return;
		}

		if (ret == -ENODEV)
			return;
		if (ret) {
			async_msg("WARN: HID_STRING_Q_FAIL");
			usbhid_usb_device_finish_preprobe(entry);
		}
		return;
	}
}

static void usbhid_usb_device_store_string(struct usbhid_usb_device *entry,
					   const struct hid_async_request *req,
					   int status)
{
	if (entry->preprobe_stage == USBHID_PREPROBE_LANGID) {
		entry->string_langid = USBHID_STRING_LANGID;
		if (status >= 0 && req->actual_len >= 4 &&
		    req->data[1] == USB_DT_STRING)
			entry->string_langid = (uint16_t)req->data[2] |
					       ((uint16_t)req->data[3] << 8);
		entry->dev.string_langid = entry->string_langid;
		entry->dev.have_langid = 1;
		entry->preprobe_stage = USBHID_PREPROBE_PRODUCT;
		return;
	}

	if (status < 0 || req->actual_len < 2 ||
	    req->data[1] != USB_DT_STRING)
		goto next;

	switch (entry->preprobe_stage) {
	case USBHID_PREPROBE_PRODUCT:
		if (usb_string_decode(req->data, req->actual_len,
				      entry->dev.product_buf,
				      sizeof(entry->dev.product_buf)) >= 0)
			entry->dev.product = entry->dev.product_buf;
		break;
	case USBHID_PREPROBE_MANUFACTURER:
		if (usb_string_decode(req->data, req->actual_len,
				      entry->dev.manufacturer_buf,
				      sizeof(entry->dev.manufacturer_buf)) >= 0)
			entry->dev.manufacturer = entry->dev.manufacturer_buf;
		break;
	case USBHID_PREPROBE_SERIAL:
		if (usb_string_decode(req->data, req->actual_len,
				      entry->dev.serial_buf,
				      sizeof(entry->dev.serial_buf)) >= 0)
			entry->dev.serial = entry->dev.serial_buf;
		break;
	default:
		break;
	}

next:
	if (entry->preprobe_stage == USBHID_PREPROBE_PRODUCT)
		entry->preprobe_stage = USBHID_PREPROBE_MANUFACTURER;
	else if (entry->preprobe_stage == USBHID_PREPROBE_MANUFACTURER)
		entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
	else
		entry->preprobe_stage = USBHID_PREPROBE_DONE;
}

static void usbhid_log_device_desc_error(const char *reason,
					 const struct hid_async_request *req)
{
	char msg[ASYNC_MSG_BUFSIZE];

	snprintf(msg, sizeof(msg), "ERR: HID_DEV_DESC_%s r%u l%u",
		 reason, req->xfer_result, req->actual_len);
	_async_msg(msg);
}

static void usbhid_usb_device_descriptor_complete(const struct hid_async_request *req,
						  int status)
{
	struct usbhid_usb_device *entry = req->context;
	tusb_desc_device_t descriptor;

	if (!entry || !entry->valid || req->dev_addr != entry->dev.dev_addr)
		return;

	if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR) {
		usbhid_usb_device_store_string(entry, req, status);
		if (entry->preprobe_stage == USBHID_PREPROBE_DONE) {
			usbhid_usb_device_finish_preprobe(entry);
			return;
		}
		usbhid_usb_device_queue_next_string(entry, req->generation);
		return;
	}

	if (status < 0) {
		taskENTER_CRITICAL();
		entry->device_desc_requested = false;
		taskEXIT_CRITICAL();
		if (req->xfer_result == XFER_RESULT_INVALID)
			usbhid_log_device_desc_error("SUB", req);
		else
			usbhid_log_device_desc_error("XFER", req);
		usbhid_usb_device_drop_pending_hid(entry);
		return;
	}

	if (req->actual_len < sizeof(descriptor)) {
		taskENTER_CRITICAL();
		entry->device_desc_requested = false;
		taskEXIT_CRITICAL();
		usbhid_log_device_desc_error("SHORT", req);
		usbhid_usb_device_drop_pending_hid(entry);
		return;
	}

	memcpy(&descriptor, req->data, sizeof(descriptor));
	taskENTER_CRITICAL();
	if (!entry->valid || req->dev_addr != entry->dev.dev_addr) {
		taskEXIT_CRITICAL();
		return;
	}
	entry->dev.descriptor.idVendor = descriptor.idVendor;
	entry->dev.descriptor.idProduct = descriptor.idProduct;
	entry->dev.descriptor.bcdDevice = descriptor.bcdDevice;
	entry->dev.descriptor.bMaxPacketSize0 = descriptor.bMaxPacketSize0;
	entry->dev.descriptor.iManufacturer = descriptor.iManufacturer;
	entry->dev.descriptor.iProduct = descriptor.iProduct;
	entry->dev.descriptor.iSerialNumber = descriptor.iSerialNumber;
	entry->have_device_desc = true;
	entry->device_desc_requested = false;
	entry->string_langid = USBHID_STRING_LANGID;
	entry->preprobe_stage = USBHID_PREPROBE_LANGID;
	taskEXIT_CRITICAL();
	usbhid_usb_device_queue_next_string(entry, req->generation);
}

static void usbhid_usb_device_queue_descriptor(struct usbhid_usb_device *entry)
{
	int ret;

	taskENTER_CRITICAL();
	if (!entry->valid || entry->have_device_desc ||
	    entry->device_desc_requested) {
		taskEXIT_CRITICAL();
		return;
	}

	entry->preprobe_stage = USBHID_PREPROBE_DEVICE_DESC;
	entry->device_desc_requested = true;
	taskEXIT_CRITICAL();
	ret = hid_async_queue_device_descriptor(entry->dev.dev_addr,
						usbhid_usb_device_descriptor_complete,
						entry);
	if (ret) {
		taskENTER_CRITICAL();
		entry->device_desc_requested = false;
		taskEXIT_CRITICAL();
		usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
		return;
	}
}

static bool usbhid_usb_device_has_waiting_hid(
		const struct usbhid_usb_device *entry)
{
	bool waiting = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		const struct usbhid_report_descriptor_slot *slot =
			&usbhid_report_descriptors[i];

		if (slot->state != USBHID_REPORT_DESCRIPTOR_FREE &&
		    slot->dev_addr == entry->dev.dev_addr &&
		    slot->generation == entry->generation) {
			waiting = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return waiting;
}

static bool usbhid_usb_device_old_epoch_retiring(
		const struct usbhid_usb_device *entry)
{
	bool retiring = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *old = &usbhid_usb_devices[i];

		if (old != entry && old->retiring &&
		    old->dev.dev_addr == entry->dev.dev_addr) {
			retiring = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return retiring;
}

static void usbhid_usb_device_retry_preprobes(void)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (!entry->valid || entry->have_device_desc ||
		    entry->device_desc_requested ||
		    !usbhid_usb_device_has_waiting_hid(entry) ||
		    usbhid_usb_device_old_epoch_retiring(entry))
			continue;
		usbhid_usb_device_queue_descriptor(entry);
	}
}

struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev, unsigned int ifnum)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && interface_to_usbdev(usbhid->intf) == dev &&
		    usbhid->intf->cur_altsetting &&
		    usbhid->intf->cur_altsetting->desc.bInterfaceNumber == ifnum)
			return usbhid->intf;
	}

	return NULL;
}

int usbhid_lifecycle_init(void)
{
	usbhid_transport_pool = kzalloc(sizeof(*usbhid_transport_pool), GFP_KERNEL);
	if (!usbhid_transport_pool)
		return -ENOMEM;
	usbhid_usb_devices = usbhid_transport_pool->devices;
	usbhid_report_descriptors =
		usbhid_transport_pool->report_descriptors;

	usbhid_lifecycle_queue = xQueueCreate(USBHID_LIFECYCLE_QUEUE_LEN,
					      sizeof(struct usbhid_lifecycle_event));
	if (!usbhid_lifecycle_queue) {
		kfree(usbhid_transport_pool);
		usbhid_transport_pool = NULL;
		usbhid_usb_devices = NULL;
		usbhid_report_descriptors = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void usbhid_lifecycle_kick(void)
{
	struct usbhid_lifecycle_event event = {
		.kind = USBHID_LIFECYCLE_WAKE,
	};
	bool send = false;

	/*
	 * Lifecycle flags/cache state are authoritative. If the queue is full, an
	 * existing event already guarantees that the task will rescan that state.
	 */
	taskENTER_CRITICAL();
	if (!usbhid_lifecycle_wake_pending) {
		usbhid_lifecycle_wake_pending = true;
		send = true;
	}
	taskEXIT_CRITICAL();

	if (send && !usbhid_lifecycle_queue) {
		taskENTER_CRITICAL();
		usbhid_lifecycle_wake_pending = false;
		taskEXIT_CRITICAL();
	} else if (send) {
		/* A full one-entry queue already contains the coalesced wakeup. */
		(void)xQueueSendToBack(usbhid_lifecycle_queue, &event, 0);
	}
}

static void usbhid_transport_fault(enum usbhid_transport_fault fault)
{
	taskENTER_CRITICAL();
	usbhid_transport_faults |= (u32)fault;
	taskEXIT_CRITICAL();
	usbhid_lifecycle_kick();
}

void usbhid_backend_rx_rearm_failed(void)
{
	usbhid_transport_fault(USBHID_FAULT_RX_REARM);
}

void usbhid_backend_rx_transfer_failed(uint8_t xfer_result)
{
	usbhid_transport_fault(xfer_result == XFER_RESULT_STALLED ?
		USBHID_FAULT_RX_STALL : USBHID_FAULT_RX_XFER);
}

static void usbhid_lifecycle_log_transport_faults(void)
{
	u32 faults;

	taskENTER_CRITICAL();
	faults = usbhid_transport_faults;
	usbhid_transport_faults = 0;
	taskEXIT_CRITICAL();

	if (faults & USBHID_FAULT_DEVICE_CACHE_FULL)
		async_msg("ERR: HID_USB_DEV_ALLOC_FAIL");
	if (faults & USBHID_FAULT_REPORT_DESCRIPTOR)
		async_msg("ERR: HID_PROBE_DEFER_FAIL");
	if (faults & USBHID_FAULT_ASYNC_CANCEL)
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	if (faults & USBHID_FAULT_REPORT_DROP)
		async_msg("ERR: HID_REPORT_SKIP");
	if (faults & USBHID_FAULT_PROTOCOL_BOOT)
		async_msg("WARN: HID_PROTOCOL_BOOT");
	if (faults & USBHID_FAULT_RX_REARM)
		async_msg("ERR: HID_RX_REARM_FAIL");
	if (faults & USBHID_FAULT_RAW_INTERFACE)
		async_msg("ERR: HID_RAW_INTERFACE_FULL");
	if (faults & USBHID_FAULT_TOPOLOGY)
		async_msg("ERR: HID_USB_PARENT_MISSING");
	if (faults & USBHID_FAULT_RX_STALL)
		async_msg("ERR: HID_RX_STALL");
	if (faults & USBHID_FAULT_RX_XFER)
		async_msg("ERR: HID_RX_XFER_FAIL");
}

static void usbhid_lifecycle_disconnect_hid(struct hid_device *hid,
					    u32 generation)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

	if (!usbhid || (generation && usbhid->generation != generation))
		return;

	usbhid_report_unplug(hid);
	if (hid_async_cancel_device_sync(usbhid->dev_addr, usbhid->instance))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_report_wait_idle(hid);
	usbhid_io_wait_idle(hid);
	usbhid_remove_slot(hid);
	hid_destroy_device(hid);
	kfree(usbhid);
}

static void usbhid_lifecycle_drain_disconnects(void)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid;
		u32 generation = 0;

		taskENTER_CRITICAL();
		hid = usbhid_devices[i];
		if (hid) {
			struct usbhid_device *usbhid = hid->driver_data;

			if (usbhid->disconnect_queued)
				generation = usbhid->generation;
			else
				hid = NULL;
		}
		taskEXIT_CRITICAL();

		if (hid)
			usbhid_lifecycle_disconnect_hid(hid, generation);
	}
}

static int usbhid_lifecycle_process_ready_probes(void)
{
	for (;;) {
		struct usbhid_probe_token token;
		struct usbhid_usb_device *entry;
		u8 *desc_report;
		int ret;

		ret = usbhid_report_descriptor_take_ready(&token, &desc_report);
		if (ret <= 0) {
			if (ret == -EAGAIN)
				continue;
			return ret;
		}

		entry = usbhid_usb_device_find(token.dev_addr);
		if (!entry || entry->generation != token.device_generation ||
		    !usbhid_report_descriptor_token_current(&token) ||
		    !tuh_hid_mounted(token.dev_addr, token.instance)) {
			kfree(desc_report);
			usbhid_report_descriptor_finish_probe(&token);
			continue;
		}

		/* usbhid_probe() consumes the task-owned descriptor on every path. */
		(void)usbhid_probe(entry, token.instance, desc_report, token.len,
				    &token);
		usbhid_report_descriptor_finish_probe(&token);
	}
}

void usbhid_lifecycle_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct usbhid_lifecycle_event event;
		int ret;

		/* Flags/cache slots own work; queue entries are only bounded wakeups. */
		usbhid_lifecycle_drain_disconnects();
		if (usbhid_usb_device_release_retired()) {
			async_msg("ERR: HID_RETIRE_SYNC_FAIL");
			vTaskDelay(1);
			continue;
		}
		usbhid_usb_device_retry_preprobes();
		ret = usbhid_lifecycle_process_ready_probes();
		if (ret == -ENOMEM) {
			async_msg("ERR: HID_DESC_ALLOC_FAIL");
			vTaskDelay(1);
			continue;
		}
		usbhid_lifecycle_log_transport_faults();

		if (xQueueReceive(usbhid_lifecycle_queue, &event,
				  portMAX_DELAY) == pdPASS) {
			taskENTER_CRITICAL();
			usbhid_lifecycle_wake_pending = false;
			taskEXIT_CRITICAL();
		}
	}
}

// static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
// TinyUSB mount callback provides dev_addr/instance/report descriptor instead
// of Linux usb_interface; build the local usb_interface shim before hid_add_device().
static int usbhid_probe(struct usbhid_usb_device *usb_entry,
			uint8_t instance, uint8_t *desc_report, uint16_t desc_len,
			const struct usbhid_probe_token *token)
{
	struct usb_device *dev = &usb_entry->dev;
	// struct usb_endpoint_descriptor *ep;
	// TinyUSB owns endpoint descriptors; the interface shim records the same
	// mandatory interrupt-IN endpoint below.
	struct usbhid_device *usbhid;
	struct hid_device *hid;
	u32 device_generation = usb_entry->generation;
	u8 dev_addr = dev->dev_addr;
	uint8_t *rdesc = desc_report;
	struct usbhid_raw_interface raw_snapshot;
	const struct usbhid_raw_interface *raw;
	uint16_t vid = 0;
	uint16_t pid = 0;
	tuh_itf_info_t itf_info;
	size_t len;
	u32 malloc_failures_before;
	int ret;

	if (!desc_report || !desc_len) {
		kfree(desc_report);
		async_msg("ERR: HID_DESC_MISSING");
		return -ENODEV;
	}

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		kfree(rdesc);
		async_msg("ERR: HID_ALLOC_FAIL");
		return PTR_ERR(hid);
	}

	usbhid = kzalloc_obj(*usbhid);
	if (!usbhid) {
		kfree(rdesc);
		hid_destroy_device(hid);
		async_msg("ERR: HID_ALLOC_FAIL");
		return -ENOMEM;
	}
	hid->driver_data = usbhid;
	usbhid->hid = hid;

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// The callback copied TinyUSB's ephemeral report descriptor into bounded
	// ingress storage. The lifecycle task now passes this owned snapshot into
	// Linux-shaped probe context, so no callback allocation or second copy is
	// needed here.

	memset(&itf_info, 0, sizeof(itf_info));
	if (!tuh_vid_pid_get(dev_addr, &vid, &pid) ||
	    !tuh_hid_itf_get_info(dev_addr, instance, &itf_info)) {
		kfree(rdesc);
		hid_destroy_device(hid);
		kfree(usbhid);
		async_msg("ERR: HID_ENUM_GONE");
		return -ENODEV;
	}

	device_initialize(&usbhid->usb_intf.dev);
	// Upstream USB core owns the physical usb_device and usb_interface.
	// The lifecycle cache owns the former; this HID owns only its interface shim.
	usbhid->usb_intf.dev.type = &usb_if_device_type;

	// hid->dev.parent = &intf->dev;
	// TinyUSB mount callback builds a local usb_interface shim for this HID instance.
	usbhid->dev_addr = dev_addr;
	usbhid->instance = instance;
	usbhid->generation = usbhid_next_generation();

	usbhid->usb_altsetting.desc.bInterfaceNumber = itf_info.desc.bInterfaceNumber;
	raw = usbhid_raw_interface_copy(dev_addr,
					 itf_info.desc.bInterfaceNumber,
					 &raw_snapshot) ? &raw_snapshot : NULL;
	if (raw) {
		memcpy(usbhid->hid_descriptor, raw->hid_descriptor,
		       sizeof(usbhid->hid_descriptor));
		usbhid->usb_altsetting.desc.bInterfaceSubClass = raw->subclass;
		usbhid->usb_altsetting.desc.bInterfaceProtocol = raw->protocol;
		usbhid->usb_altsetting.desc.bNumEndpoints = raw->endpoint_count;
		memcpy(usbhid->usb_altsetting.endpoint, raw->endpoint,
		       sizeof(usbhid->usb_altsetting.endpoint));
		for (u8 i = 0; i < raw->endpoint_count; i++) {
			const struct usb_endpoint_descriptor *ep =
				&usbhid->usb_altsetting.endpoint[i].desc;

			if (!usbhid->usb_altsetting.has_interrupt_in &&
			    (ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN &&
			    usb_endpoint_xfer_int(ep)) {
				usbhid->usb_altsetting.has_interrupt_in = true;
				usbhid->usb_altsetting.interrupt_in_endpoint =
					ep->bEndpointAddress;
			}
			if (!usbhid->usb_altsetting.has_interrupt_out &&
			    (ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT &&
			    usb_endpoint_xfer_int(ep)) {
				usbhid->usb_altsetting.has_interrupt_out = true;
				usbhid->usb_altsetting.interrupt_out_endpoint =
					ep->bEndpointAddress;
			}
		}
	} else {
		usbhid->usb_altsetting.desc.bInterfaceSubClass = itf_info.desc.bInterfaceSubClass;
		usbhid->usb_altsetting.desc.bInterfaceProtocol = itf_info.desc.bInterfaceProtocol;
		usbhid->usb_altsetting.desc.bNumEndpoints = itf_info.desc.bNumEndpoints;
	}
	usbhid->usb_intf.altsetting = &usbhid->usb_altsetting;
	usbhid->usb_intf.cur_altsetting = &usbhid->usb_altsetting;
	usbhid->usb_intf.dev.parent = &dev->dev;
	hid->dev.parent = &usbhid->usb_intf.dev;
	usb_set_intfdata(&usbhid->usb_intf, hid);
	usbhid->intf = &usbhid->usb_intf;
	usbhid->ifnum = usbhid->usb_altsetting.desc.bInterfaceNumber;

	// ret = usb_find_int_in_endpoint(interface, &ep);
	// if (ret) {
	// 	hid_err(intf, "couldn't find an input interrupt endpoint\n");
	// 	return -ENODEV;
	// }
	// Linux USB core searches the parsed usb_host_interface before allocating
	// hid_device. The TinyUSB boundary constructs that interface locally, so
	// perform the same mandatory-channel check before publishing the HID.
	if (!usbhid->usb_altsetting.has_interrupt_in) {
		hid_err(usbhid->intf,
			"couldn't find an input interrupt endpoint\n");
		async_msg("ERR: HID_INT_IN_MISSING");
		ret = -ENODEV;
		goto fail;
	}

	hid->ll_driver = &usb_hid_driver;
// #ifdef CONFIG_USB_HIDDEV
// 	hid->hiddev_connect = hiddev_connect;
// 	hid->hiddev_disconnect = hiddev_disconnect;
// 	hid->hiddev_hid_event = hiddev_hid_event;
// 	hid->hiddev_report_event = hiddev_report_event;
// #endif
// Firmware provides the CONFIG_USB_HIDDEV callbacks through a bounded hiddev
// proxy ring instead of Linux usb_register_dev()/file operations.
#ifdef CONFIG_USB_HIDDEV
	hid->hiddev_connect = hiddev_connect;
	hid->hiddev_disconnect = hiddev_disconnect;
	hid->hiddev_hid_event = hiddev_hid_event;
	hid->hiddev_report_event = hiddev_report_event;
#endif
	usbhid->rdesc = rdesc;
	usbhid->rsize = desc_len;
	init_waitqueue_head(&usbhid->wait);

	hid->bus = BUS_USB;
	hid->vendor = le16_to_cpu(dev->descriptor.idVendor);
	hid->product = le16_to_cpu(dev->descriptor.idProduct);
	hid->version = le16_to_cpu(dev->descriptor.bcdDevice);
	hid->name[0] = 0;
	if (usbhid->intf->cur_altsetting->desc.bInterfaceProtocol ==
			USB_INTERFACE_PROTOCOL_MOUSE)
		hid->type = HID_TYPE_USBMOUSE;
	else if (usbhid->intf->cur_altsetting->desc.bInterfaceProtocol == 0)
		hid->type = HID_TYPE_USBNONE;

	if (dev->manufacturer)
		strscpy(hid->name, dev->manufacturer, sizeof(hid->name));

	if (dev->product) {
		if (dev->manufacturer)
			strlcat(hid->name, " ", sizeof(hid->name));
		strlcat(hid->name, dev->product, sizeof(hid->name));
	}

	if (!strlen(hid->name))
		snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x",
			 le16_to_cpu(dev->descriptor.idVendor),
			 le16_to_cpu(dev->descriptor.idProduct));

	usb_make_path(dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input", sizeof(hid->phys));
	len = strlen(hid->phys);
	if (len < sizeof(hid->phys) - 1)
		snprintf(hid->phys + len, sizeof(hid->phys) - len,
			 "%d", usbhid->intf->altsetting[0].desc.bInterfaceNumber);

	// if (usb_string(dev, dev->descriptor.iSerialNumber, hid->uniq, 64) <= 0)
	// 	hid->uniq[0] = 0;
	// The async pre-probe already decoded the shared USB serial string.
	if (dev->serial)
		strscpy(hid->uniq, dev->serial, sizeof(hid->uniq));
	else
		hid->uniq[0] = 0;

	ret = usbhid_insert_if_generation(hid, device_generation, token);
	if (ret < 0) {
		async_msg(ret == -ENOMEM ? "ERR: HID_TABLE_FULL" :
			  "ERR: HID_ENUM_GONE");
		goto fail;
	}

	malloc_failures_before = freertos_malloc_failure_count();
	ret = hid_add_device(hid);
	/*
	 * Linux normally reports deep probe allocation failures through errno.
	 * Some HID parser paths intentionally omit a field on allocation failure;
	 * surface that firmware constraint here without logging from the heap hook.
	 */
	if (freertos_malloc_failure_count() != malloc_failures_before)
		async_msg("ERR: HID_PROBE_NOMEM");
	if (ret < 0) {
		usbhid_report_stop(hid);
		if (hid_async_cancel_device_sync(usbhid->dev_addr,
						 usbhid->instance))
			async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
		usbhid_report_wait_idle(hid);
		usbhid_io_wait_idle(hid);
		usbhid_remove_slot(hid);
		async_msg(ret == -ENODEV ? "WARN: HID_IGNORED" : "ERR: HID_ADD_FAIL");
		goto fail;
	}

	/*
	 * Direct interrupt IN starts from usbhid_open()/usbhid_start() after the
	 * Linux HID device binds, matching upstream hid_start_in() lifecycle
	 * instead of probe-time report delivery.
	 */
	usbhid->rdesc = NULL;
	usbhid->rsize = 0;
	kfree(rdesc);
	return 0;

fail:
	usbhid->rdesc = NULL;
	usbhid->rsize = 0;
	kfree(rdesc);
	hid_destroy_device(hid);
	kfree(usbhid);
	return ret;
}

// static void usbhid_disconnect(struct usb_interface *intf)
// TinyUSB unmount callback identifies the HID interface by dev_addr + instance.
static void usbhid_disconnect(uint8_t dev_addr, uint8_t instance)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	int ret;

	if (hid) {
		taskENTER_CRITICAL();
		if (usbhid->disconnect_queued) {
			taskEXIT_CRITICAL();
			return;
		}
		usbhid->disconnect_queued = true;
		taskEXIT_CRITICAL();

		usbhid_report_unplug(hid);
	}

	ret = hid_async_cancel_device(dev_addr, instance);

	if (ret)
		usbhid_transport_fault(USBHID_FAULT_ASYNC_CANCEL);
	if (!hid)
		return;
	/*
	 * Upstream Linux runs usbhid_disconnect() from USB core process
	 * context and can call hid_destroy_device() directly. TinyUSB calls
	 * this hook from unmount callback context, so driver remove is handed
	 * to a firmware task before any Linux-style flush/cancel waits run.
	 */
	usbhid_lifecycle_kick();
}

void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      uint8_t const *desc_report, uint16_t desc_len)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_prepare(dev_addr);
	int ret;

	if (!entry) {
		return;
	}

	ret = usbhid_usb_device_store_pending_probe(entry, instance, desc_report,
						    desc_len);
	if (ret) {
		usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
		return;
	}
}

void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance)
{
	usbhid_usb_device_drop_pending_hid_instance(dev_addr, instance);
	usbhid_disconnect(dev_addr, instance);
}

void usbhid_backend_device_mount(uint8_t dev_addr)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_prepare(dev_addr);

	if (!entry) {
		return;
	}

	if (usbhid_usb_device_old_epoch_retiring(entry))
		usbhid_lifecycle_kick();
	else
		usbhid_usb_device_queue_descriptor(entry);
}

static void usbhid_backend_device_detach(uint8_t dev_addr)
{
	struct usbhid_usb_device *retired;
	int ret;

	retired = usbhid_usb_device_mark_retiring(dev_addr);
	if (!retired)
		return;

	/* Revoke pending storage and any task-owned probe token immediately. */
	usbhid_usb_device_drop_pending_hid(retired);
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && usbhid->dev_addr == dev_addr)
			usbhid_report_unplug(hid);
	}

	ret = hid_async_cancel_dev_addr(dev_addr);
	/* Publish only after the old transport epoch can no longer grow work. */
	usbhid_usb_device_publish_retired(retired);

	if (ret)
		usbhid_transport_fault(USBHID_FAULT_ASYNC_CANCEL);
	usbhid_lifecycle_kick();
}

void usbhid_backend_device_umount(uint8_t dev_addr)
{
	usbhid_backend_device_detach(dev_addr);
}

void usbhid_backend_report_completed(uint8_t dev_addr, uint8_t instance,
				     uint32_t generation,
				     uint8_t const *report, uint16_t bufsize,
				     uint32_t len, uint8_t xfer_result)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	uint8_t protocol_mode = HID_PROTOCOL_REPORT;
	bool parse = xfer_result == XFER_RESULT_SUCCESS;
	int ret;

	/* A fenced completion from an older TinyUSB address epoch is expected. */
	if (!usbhid || usbhid->generation != generation)
		return;
	if (parse) {
		protocol_mode = tuh_hid_get_protocol(dev_addr, instance);
		if (protocol_mode == HID_PROTOCOL_BOOT) {
			usbhid_transport_fault(USBHID_FAULT_PROTOCOL_BOOT);
			parse = false;
		}
	}

	ret = usbhid_report_submit(hid, report, bufsize, len, xfer_result,
				   parse);
	if (ret < 0)
		usbhid_transport_fault(USBHID_FAULT_REPORT_DROP);
}

/*
 * Reset LEDs which BIOS might have left on. For now, just NumLock (0x01).
 */
static int hid_find_field_early(struct hid_device *hid, unsigned int page,
				unsigned int hid_code,
				struct hid_field **pfield)
{
	struct hid_report *report;
	struct hid_field *field;
	struct hid_usage *usage;
	int i, j;

	list_for_each_entry(report,
			    &hid->report_enum[HID_OUTPUT_REPORT].report_list,
			    list) {
		for (i = 0; i < report->maxfield; i++) {
			field = report->field[i];
			for (j = 0; j < field->maxusage; j++) {
				usage = &field->usage[j];
				if ((usage->hid & HID_USAGE_PAGE) == page &&
				    (usage->hid & 0xFFFF) == hid_code) {
					*pfield = field;
					return j;
				}
			}
		}
	}

	return -1;
}

static void usbhid_set_leds(struct hid_device *hid)
{
	struct hid_field *field;
	int offset;

	if ((offset = hid_find_field_early(hid, HID_UP_LED, 0x01,
					   &field)) != -1) {
		hid_set_field(field, offset, 0);
		// usbhid_submit_report(hid, field->report, USB_DIR_OUT);
		// TinyUSB submits the upstream request through the async owner task.
		usbhid_request(hid, field->report, HID_REQ_SET_REPORT);
	}
}

static int usbhid_start(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	// struct usb_device *dev = interface_to_usbdev(intf);
	// struct usbhid_device *usbhid = hid->driver_data;
	// TinyUSB owns the endpoint objects; this reduced start path only needs the
	// upstream interface descriptor view before arming the async transport.
	int ret = 0;

	if (usbhid_report_is_stopping(hid))
		return -ENODEV;

	/*
	 * Firmware allocates no Linux URBs here. TinyUSB interrupt IN starts here
	 * only for ALWAYS_POLL, matching upstream hid_start_in(); boot-keyboard LED
	 * reset uses the same queued output/control routing as upstream usbhid.
	 */
	if (hid->quirks & HID_QUIRK_ALWAYS_POLL)
		ret = usbhid_report_start(hid);
	if (ret)
		return ret;

	/* Some keyboards don't work until their LEDs have been set. */
	if (interface->desc.bInterfaceSubClass ==
			USB_INTERFACE_SUBCLASS_BOOT &&
	    interface->desc.bInterfaceProtocol ==
			USB_INTERFACE_PROTOCOL_KEYBOARD) {
		usbhid_set_leds(hid);
		// device_set_wakeup_enable(&dev->dev, 1);
		// Firmware has no Linux PM wakeup policy at this transport boundary.
	}

	return 0;
}

static void usbhid_stop(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	// usb_kill_urb(usbhid->urbin);
	// usb_kill_urb(usbhid->urbout);
	// usb_kill_urb(usbhid->urbctrl);
	//
	// hid_cancel_delayed_stuff(usbhid);
	// Firmware has no URBs/reset_work object. Stop and the async cancel barrier
	// drain the equivalent IN, OUT/control, retry-timer, and clear-halt owners.
	usbhid_report_stop(hid);
	if (hid_async_cancel_device_sync(usbhid->dev_addr, usbhid->instance))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_report_wait_idle(hid);
	usbhid_io_wait_idle(hid);
	usbhid_report_release(hid);
}

static int usbhid_open(struct hid_device *hid)
{
	if (usbhid_report_is_stopping(hid))
		return -ENODEV;

	/*
	 * Upstream usbhid_open() calls hid_start_in() after hidinput opens the
	 * device. Do the TinyUSB receive submit here, not from probe, so unbound
	 * or ignored devices do not feed reports into hid_input_report().
	 */
	if (hid->quirks & HID_QUIRK_ALWAYS_POLL)
		return 0;

	return usbhid_report_start(hid);
}

static void usbhid_close(struct hid_device *hid)
{
	// if (!(hid->quirks & HID_QUIRK_ALWAYS_POLL)) {
	// 	hid_cancel_delayed_stuff(usbhid);
	// 	usb_kill_urb(usbhid->urbin);
	// 	usbhid->intf->needs_remote_wakeup = 0;
	// }
	// There is no Linux PM wake flag. report_close() cancels the retry timer and
	// fences an armed TinyUSB transfer; concurrent reopen is serialized there.
	if (!(hid->quirks & HID_QUIRK_ALWAYS_POLL))
		usbhid_report_close(hid);
}

static int usbhid_parse(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	// struct usb_device *dev = interface_to_usbdev(intf);
	// TinyUSB already fetched the report descriptor asynchronously; no USB
	// request is issued from this parser callback.
	struct usbhid_device *usbhid = hid->driver_data;
	struct hid_descriptor hdesc_storage;
	struct hid_descriptor *hdesc;
	struct hid_class_descriptor *hcdesc;
	__u8 fixed_opt_descriptors_size;
	u32 quirks = 0;
	unsigned long transport_quirks = 0;
	unsigned int rsize = 0;
	// char *rdesc;
	// The lifecycle task owns the TinyUSB report-descriptor snapshot in usbhid.
	int ret;

	quirks = hid_lookup_quirk(hid);

	if (quirks & HID_QUIRK_IGNORE)
		return -ENODEV;

	/* Many keyboards and mice don't like to be polled for reports,
	 * so we will always set the HID_QUIRK_NOGET flag for them. */
	// if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
	// 	if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
	// 		interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
	// 			quirks |= HID_QUIRK_NOGET;
	// }
	// hid_device_probe() recomputes table quirks after parse(). Preserve the
	// transport-added upstream boot-device quirk across that reset.
	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
		if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
		    interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE) {
			quirks |= HID_QUIRK_NOGET;
			transport_quirks |= HID_QUIRK_NOGET;
		}
	}

	// if (usb_get_extra_descriptor(interface, HID_DT_HID, &hdesc) &&
	//     (!interface->desc.bNumEndpoints ||
	//      usb_get_extra_descriptor(&interface->endpoint[0], HID_DT_HID,
	//                               &hdesc))) {
	// 	dbg_hid("class descriptor not present\n");
	// 	return -ENODEV;
	// }
	// Linux USB core exposes descriptor extra bytes on interface/endpoint
	// objects. The TinyUSB raw-config driver saved the same fixed HID header
	// before its HID class driver consumed the configuration stream.
	hdesc = &hdesc_storage;
	memcpy(hdesc, usbhid->hid_descriptor, sizeof(*hdesc));
	if (hdesc->bDescriptorType != HID_DT_HID)
		return -ENODEV;

	if (!hdesc->bNumDescriptors ||
	    hdesc->bLength != sizeof(*hdesc) +
			      (hdesc->bNumDescriptors - 1) * sizeof(*hcdesc)) {
		dbg_hid("hid descriptor invalid, bLen=%hhu bNum=%hhu\n",
			hdesc->bLength, hdesc->bNumDescriptors);

		/*
		 * Some devices may expose a wrong number of descriptors compared
		 * to the provided length.
		 * However, we ignore the optional hid class descriptors entirely
		 * so we can safely recompute the proper field.
		 */
		if (hdesc->bLength >= sizeof(*hdesc)) {
			fixed_opt_descriptors_size =
				hdesc->bLength - sizeof(*hdesc);

			hid_warn(intf, "fixing wrong optional hid class descriptors count\n");
			// Kernel dev_warn() is silent in firmware; retain a bounded diagnostic.
			async_msg("WARN: HID_DESC_COUNT");
			hdesc->bNumDescriptors =
				fixed_opt_descriptors_size / sizeof(*hcdesc) + 1;
		} else {
			return -EINVAL;
		}
	}

	hid->version = le16_to_cpu(hdesc->bcdHID);
	hid->country = hdesc->bCountryCode;

	if (hdesc->rpt_desc.bDescriptorType == HID_DT_REPORT)
		rsize = le16_to_cpu(hdesc->rpt_desc.wDescriptorLength);

	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE) {
		dbg_hid("weird size of report descriptor (%u)\n", rsize);
		return -EINVAL;
	}

	/*
	 * Linux USB core requests exactly the class-declared size. TinyUSB owns
	 * that request here, so reject a callback buffer from a different
	 * descriptor contract before hid_parse_report() can read it.
	 */
	if (!usbhid->rdesc || rsize != usbhid->rsize) {
		async_msg("ERR: HID_RDESC_SIZE");
		return -EINVAL;
	}

	// rdesc = kmalloc(rsize, GFP_KERNEL);
	// if (!rdesc)
	// 	return -ENOMEM;
	//
	// hid_set_idle(dev, interface->desc.bInterfaceNumber, 0, 0);
	// TinyUSB performs enumeration SET_IDLE before mount; the owned report
	// descriptor arrives through the lifecycle task rather than a blocking
	// parser-side USB request.
	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// if (ret < 0) {
	// 	dbg_hid("reading report descriptor failed\n");
	// 	kfree(rdesc);
	// 	goto err;
	// }
	// TinyUSB supplies the report descriptor to tuh_hid_mount_cb(); this
	// ll_driver consumes it here so hid_add_device() keeps upstream parse flow.
	ret = hid_parse_report(hid, usbhid->rdesc, rsize);
	// kfree(rdesc);
	// usbhid_probe() releases the task-owned descriptor after hid_add_device().
	if (ret) {
		dbg_hid("parsing report descriptor failed\n");
		goto err;
	}

	if (hdesc->bNumDescriptors > 1) {
		hid_warn(intf,
			 "%u unsupported optional hid class descriptors\n",
			 (int)(hdesc->bNumDescriptors - 1));
		// Kernel dev_warn() is silent in firmware; retain a bounded diagnostic.
		async_msg("WARN: HID_OPT_DESC");
	}

	// hid_device_probe() recomputes table quirks after parse(); keep the
	// transport-added boot NOGET bit in initial_quirks as well.
	hid->initial_quirks |= transport_quirks;
	hid->quirks |= quirks;

	return 0;
err:
	return ret;
}

static void usbhid_sync_complete(const struct hid_async_request *req, int status)
{
	struct usbhid_sync_request *sync = req->context;
	TaskHandle_t waiter = sync->task;

	if (status >= 0 && sync->buf) {
		sync->actual_len = min_t(size_t, req->actual_len, sync->bufsize);
		memcpy(sync->buf, req->data, sync->actual_len);
	} else {
		sync->actual_len = req->actual_len;
	}
	sync->status = status;
	taskENTER_CRITICAL();
	sync->done = true;
	taskEXIT_CRITICAL();
	/* Publishing done releases the stack-owned sync; do not touch it again. */
	xTaskNotifyGive(waiter);
}

static int usbhid_sync_wait(struct usbhid_sync_request *sync, int ret)
{
	bool done;

	if (ret)
		return ret;

	do {
		taskENTER_CRITICAL();
		done = sync->done;
		taskEXIT_CRITICAL();
		if (!done)
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	} while (!done);
	return sync->status;
}

static bool usbhid_io_get(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool acquired = false;

	taskENTER_CRITICAL();
	if (!usbhid->transport_stopping) {
		usbhid->io_pending++;
		acquired = true;
	}
	taskEXIT_CRITICAL();
	return acquired;
}

static void usbhid_io_put(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	taskENTER_CRITICAL();
	configASSERT(usbhid->io_pending);
	usbhid->io_pending--;
	taskEXIT_CRITICAL();
}

static bool usbhid_io_idle(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool idle;

	taskENTER_CRITICAL();
	idle = usbhid->io_pending == 0;
	taskEXIT_CRITICAL();
	return idle;
}

static void usbhid_io_wait_idle(struct hid_device *hid)
{
	while (!usbhid_io_idle(hid))
		vTaskDelay(1);
}

static void usbhid_control_report_done(struct hid_device *hid, void *context,
				       int status)
{
	if (status < 0 && status != -ENODEV)
		async_msg("ERR: HID_CTRL_PARSE_FAIL");
	kfree(context);
	usbhid_io_put(hid);
}

static void usbhid_request_complete(const struct hid_async_request *req,
				    int status)
{
	struct usbhid_control_input *input = req->context;
	u16 len;
	int ret;

	if (status < 0) {
		if (status != -ENODEV)
			async_msg("ERR: HID_ASYNC_REQ_FAIL");
		kfree(input);
		usbhid_io_put(req->hid);
		return;
	}

	if (req->reqtype != HID_REQ_GET_REPORT) {
		configASSERT(!input);
		usbhid_io_put(req->hid);
		return;
	}

	configASSERT(input);
	len = min_t(u16, req->actual_len, input->bufsize);
	memcpy(input->data, req->data, len);
	ret = usbhid_control_report_submit(req->hid, req->report->type,
					   input->data, input->bufsize, len,
					   input->parser_owner,
					   usbhid_control_report_done,
					   input);
	if (ret) {
		if (ret != -ENODEV)
			async_msg("ERR: HID_CTRL_PARSE_Q_FAIL");
		kfree(input);
		usbhid_io_put(req->hid);
	}
}

static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype)
{
	struct usbhid_control_input *input = NULL;
	u32 bufsize;
	int ret;

	if (reqtype == HID_REQ_GET_REPORT && (hid->quirks & HID_QUIRK_NOGET))
		return;
	if (!usbhid_io_get(hid))
		return;
	if (reqtype == HID_REQ_GET_REPORT) {
		bufsize = hid_report_len(report);
		if (bufsize > HID_ASYNC_REPORT_MAX) {
			async_msg("ERR: HID_REPORT_TOO_LONG");
			usbhid_io_put(hid);
			return;
		}
		bufsize += 7 + (report->id == 0);
		input = kzalloc(sizeof(*input) + bufsize, GFP_KERNEL);
		if (!input) {
			async_msg("ERR: HID_REPORT_NOMEM");
			usbhid_io_put(hid);
			return;
		}
		if (sema_owned_by_current(&hid->driver_input_lock))
			input->parser_owner = xTaskGetCurrentTaskHandle();
		input->bufsize = (u16)bufsize;
	}

	// usbhid_submit_report(hid, report, reqtype);
	/*
	 * Match upstream usbhid_submit_report(): queue best-effort control work and
	 * return. GET_REPORT completion is parsed from the report queue before it
	 * releases usbhid->io_pending, so a following hid_hw_wait() cannot observe a
	 * false-idle handoff between transport completion and hid-core parsing.
	 */
	ret = hid_async_queue_report(hid, report, reqtype,
				     usbhid_request_complete, input);
	if (ret) {
		async_msg("ERR: HID_ASYNC_REQ_FAIL");
		kfree(input);
		usbhid_io_put(hid);
	}
}

static int usbhid_wait_io(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	bool owns_input_lock = sema_owned_by_current(&hid->driver_input_lock);

	if (owns_input_lock) {
		taskENTER_CRITICAL();
		configASSERT(!usbhid->control_waiter ||
			     usbhid->control_waiter == task);
		usbhid->control_waiter = task;
		taskEXIT_CRITICAL();
		/* The owner predicate changed after a probe-time GET was queued. */
		usbhid_control_report_owner_ready();
	}

	/*
	 * Probe owns driver_input_lock while resolution multipliers are fetched.
	 * Let that same task consume only its completed control GET; opening the
	 * lock here would also admit interrupt-IN into a half-built input device.
	 */
	while (!usbhid_io_idle(hid)) {
		if (owns_input_lock &&
		    usbhid_control_report_process_owned(hid))
			continue;
		vTaskDelay(1);
	}

	if (owns_input_lock) {
		taskENTER_CRITICAL();
		if (usbhid->control_waiter == task)
			usbhid->control_waiter = NULL;
		taskEXIT_CRITICAL();
	}
	return usbhid_report_is_stopping(hid) ? -ENODEV : 0;
}

static int usbhid_raw_request(struct hid_device *hid, unsigned char reportnum,
			      __u8 *buf, size_t len, unsigned char rtype,
			      int reqtype)
{
	struct usbhid_sync_request sync = {
		.task = xTaskGetCurrentTaskHandle(),
	};
	int ret;

	if (!usbhid_io_get(hid))
		return -ENODEV;

	if (reqtype == HID_REQ_SET_REPORT) {
		// return usbhid_set_raw_report(hid, reportnum, buf, len, rtype);
		// Queue raw SET_REPORT through the HID async task and wait in the
		// calling task; TinyUSB callbacks remain nonblocking.
		ret = hid_async_queue_raw_set_report(hid, reportnum, rtype, buf, len,
						     usbhid_sync_complete, &sync);
		ret = usbhid_sync_wait(&sync, ret);
		if (!ret)
			ret = (int)sync.actual_len;
		goto out;
	}

	if (reqtype == HID_REQ_GET_REPORT) {
		sync.buf = buf;
		sync.bufsize = len;
		ret = hid_async_queue_raw_get_report_id(hid, reportnum, rtype, len,
							 usbhid_sync_complete, &sync);
		ret = usbhid_sync_wait(&sync, ret);
		if (!ret)
			ret = (int)sync.actual_len;
		goto out;
	}

	ret = -ENOSYS;
out:
	usbhid_io_put(hid);
	return ret;
}

static int usbhid_output_report(struct hid_device *hid, __u8 *buf, size_t len)
{
	struct usbhid_sync_request sync = {
		.task = xTaskGetCurrentTaskHandle(),
	};

	if (!usbhid_io_get(hid))
		return -ENODEV;

	// ret = usb_interrupt_msg(dev, usbhid->urbout->pipe, buf, count,
	//			   &actual_length, USB_CTRL_SET_TIMEOUT);
	// TinyUSB interrupt OUT is queued through the HID async task; its exact
	// endpoint completion only wakes that task and never blocks the host owner.
	int ret = hid_async_queue_output_report(hid, buf, len,
						usbhid_sync_complete, &sync);
	ret = usbhid_sync_wait(&sync, ret);
	if (!ret)
		ret = (int)sync.actual_len + (buf[0] == 0);
	usbhid_io_put(hid);
	return ret;
}

static int usbhid_idle(struct hid_device *hid, int report, int idle, int reqtype)
{
	struct usbhid_sync_request sync = {
		.task = xTaskGetCurrentTaskHandle(),
	};
	int ret;

	if (reqtype != HID_REQ_SET_IDLE)
		return -EINVAL;
	if (!usbhid_io_get(hid))
		return -ENODEV;

	/*
	 * return hid_set_idle(dev, ifnum, report, idle);
	 * Keep the upstream synchronous ll_driver contract over the serialized
	 * async EP0 owner; no TinyUSB callback waits for this completion.
	 */
	ret = hid_async_queue_idle(hid, (u8)report, (u8)idle,
				   usbhid_sync_complete, &sync);
	ret = usbhid_sync_wait(&sync, ret);
	usbhid_io_put(hid);
	return ret;
}

int tuh_usb_control_msg(struct usb_device *dev, unsigned int pipe,
			u8 request, u8 requesttype, u16 value, u16 index,
			void *data, u16 size, int timeout)
{
	/*
	 * Generic synchronous EP0 is not routed by this transport yet. Callers must
	 * be converted to the serialized async owner before they can be linked.
	 */
	(void)dev;
	(void)pipe;
	(void)request;
	(void)requesttype;
	(void)value;
	(void)index;
	(void)data;
	(void)size;
	(void)timeout;
	return -ENOSYS;
}

#if 0
/*
 * Deferred: no current HID driver in the CMake allowlist uses Linux URB
 * transport. Keep the upstream-shaped bridge disabled until such a driver is
 * re-enabled with matching async request coverage.
 */
struct urb *usb_alloc_urb(int iso_packets, gfp_t mem_flags)
{
	// Upstream Linux USB core allocates URBs; firmware keeps only the
	// Linux-shaped non-ISO URB storage needed by imported HID drivers.
	(void)iso_packets;
	return kzalloc(sizeof(struct urb), mem_flags);
}

void usb_free_urb(struct urb *urb)
{
	kfree(urb);
}

static void usbhid_urb_complete(const struct hid_async_request *req, int status)
{
	struct urb *urb = req->context;

	if (status >= 0 && usb_pipein(urb->pipe) && urb->transfer_buffer)
		memcpy(urb->transfer_buffer, req->data, req->actual_len);

	urb->status = status;
	urb->actual_length = req->actual_len;
	if (urb->complete)
		urb->complete(urb);
}

int usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
	struct usb_ctrlrequest *setup = (struct usb_ctrlrequest *)urb->setup_packet;
	/*
	 * A shared physical usb_device cannot identify one HID interface.
	 * Upstream usbhid URBs carry their hid_device as completion context;
	 * use that ownership when this disabled bridge is eventually enabled.
	 */
	struct hid_device *hid = urb->context;

	(void)mem_flags;

	return hid_async_queue_usb_control_msg(hid, urb->dev, urb->pipe,
					       setup->bRequest,
					       setup->bRequestType,
					       le16_to_cpu(setup->wValue),
					       le16_to_cpu(setup->wIndex),
					       urb->transfer_buffer,
					       (u16)urb->transfer_buffer_length,
					       USB_CTRL_SET_TIMEOUT,
					       usbhid_urb_complete, urb);
}

void usb_kill_urb(struct urb *urb)
{
	/*
	 * usb_kill_urb(urb);
	 * TinyUSB transfers are canceled per HID device through
	 * hid_async_cancel_device() before driver remove runs. The URB wrapper
	 * itself has no separate host-controller queue to drain.
	 */
	(void)urb;
}
#endif

static const struct hid_ll_driver usb_hid_driver = {
	.parse = usbhid_parse,
	.start = usbhid_start,
	.stop = usbhid_stop,
	.open = usbhid_open,
	.close = usbhid_close,
	.request = usbhid_request,
	.wait = usbhid_wait_io,
	.raw_request = usbhid_raw_request,
	.output_report = usbhid_output_report,
	.idle = usbhid_idle,
	.max_buffer_size = HID_MAX_BUFFER_SIZE,
};
