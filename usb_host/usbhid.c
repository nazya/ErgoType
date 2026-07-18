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
#include "usbhid_backend.h"
#include "usbhid_report.h"
#include "stdio_tusb_cdc.h"
#include "linux/include/linux/hid.h"
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
#define USBHID_LIFECYCLE_QUEUE_LEN (HID_HOST_MAX_DEVICES * 2)
#define USBHID_STRING_LANGID 0x0409u
#define USBHID_USB_DEVICE_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
#define USBHID_USB_MAXCHILD 31
#define USBHID_PENDING_HID_MAX HID_HOST_MAX_DEVICES

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
	USBHID_LIFECYCLE_PROBE,
	USBHID_LIFECYCLE_DISCONNECT,
};

struct usbhid_lifecycle_event {
	enum usbhid_lifecycle_kind kind;
	u8 dev_addr;
	u8 instance;
	u32 generation;
	u16 desc_len;
	u8 *desc_report;
	tusb_desc_device_t device_desc;
};

struct usbhid_sync_request {
	TaskHandle_t task;
	u8 *buf;
	size_t bufsize;
	size_t actual_len;
	int status;
	volatile bool done;
};

struct usbhid_pending_hid_probe {
	bool valid;
	uint8_t instance;
	uint16_t desc_len;
	uint8_t *desc_report;
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
	bool have_device_desc;
	bool device_desc_requested;
	uint8_t pending_desc_dev_addr;
	u32 generation;
	enum usbhid_preprobe_stage preprobe_stage;
	uint16_t string_langid;
	struct usb_device dev;
	tusb_desc_device_t device_desc;
	struct usbhid_pending_hid_probe pending_hid[USBHID_PENDING_HID_MAX];
};

struct usbhid_raw_interface {
	bool valid;
	uint8_t dev_addr;
	uint8_t ifnum;
	uint8_t subclass;
	uint8_t protocol;
	uint8_t endpoint_count;
	struct usb_host_endpoint endpoint[USB_HOST_ENDPOINT_MAX];
};

static struct usb_device usbhid_root_hub;
static bool usbhid_root_hub_valid;
static struct usbhid_usb_device *usbhid_usb_devices[USBHID_USB_DEVICE_MAX];
static struct usbhid_raw_interface usbhid_raw_interfaces[HID_HOST_RAW_INTERFACE_MAX];
static u32 usbhid_generation;
static void usbhid_io_wait_idle(struct hid_device *hid);

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
	for (size_t i = 0; i < USBHID_USB_DEVICE_MAX; i++) {
		struct usbhid_usb_device *entry = usbhid_usb_devices[i];

		if (entry && entry->valid && entry->dev.dev_addr == dev_addr)
			return entry;
	}

	return NULL;
}

static struct usbhid_usb_device *usbhid_usb_device_slot(uint8_t dev_addr)
{
	struct usbhid_usb_device *free_slot = NULL;

	for (size_t i = 0; i < USBHID_USB_DEVICE_MAX; i++) {
		struct usbhid_usb_device *entry = usbhid_usb_devices[i];

		if (entry && entry->valid && entry->dev.dev_addr == dev_addr)
			return entry;
		if (entry && !entry->valid && !free_slot)
			free_slot = entry;
		if (!entry && !free_slot) {
			entry = kzalloc(sizeof(*entry), GFP_KERNEL);
			if (!entry)
				return NULL;
			usbhid_usb_devices[i] = entry;
			free_slot = entry;
		}
	}

	return free_slot;
}

static struct usb_device *usbhid_usb_device_parent(struct usb_device *dev)
{
	struct usbhid_usb_device *hub;

	if (dev->hub_addr) {
		hub = usbhid_usb_device_find(dev->hub_addr);
		if (hub)
			return &hub->dev;
	}

	return usbhid_usb_root_hub(dev->rhport);
}

static void usbhid_usb_device_copy(struct usbhid_usb_device *entry,
				   const struct usb_device *src,
				   bool have_device_desc)
{
	struct usb_device *dst = &entry->dev;

	if (have_device_desc || !entry->have_device_desc)
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
	dst->parent = usbhid_usb_device_parent(dst);
	if (src->product && src->product[0]) {
		strscpy(dst->product_buf, src->product, sizeof(dst->product_buf));
		dst->product = dst->product_buf;
	}
	if (have_device_desc)
		entry->have_device_desc = true;
}

static struct usbhid_usb_device *
usbhid_usb_device_upsert(const struct usb_device *src, bool have_device_desc)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_slot(src->dev_addr);

	if (!entry)
		return NULL;

	if (!entry->valid) {
		memset(entry, 0, sizeof(*entry));
		device_initialize(&entry->dev.dev);
		entry->dev.dev.type = &usb_device_type;
		usbhid_usb_device_init_config(&entry->dev);
		entry->generation = usbhid_next_generation();
	}
	entry->valid = true;
	usbhid_usb_device_copy(entry, src, have_device_desc);
	return entry;
}

static bool usbhid_usb_device_copy_if_generation(const struct usb_device *src,
						   bool have_device_desc,
						   u32 generation)
{
	struct usbhid_usb_device *entry;
	bool copied = false;

	taskENTER_CRITICAL();
	entry = usbhid_usb_device_find(src->dev_addr);
	if (entry && entry->generation == generation) {
		usbhid_usb_device_copy(entry, src, have_device_desc);
		copied = true;
	}
	taskEXIT_CRITICAL();
	return copied;
}

static struct usbhid_usb_device *usbhid_usb_device_prepare(uint8_t dev_addr)
{
	struct usb_device dev = { 0 };
	uint16_t vid;
	uint16_t pid;

	dev.dev_addr = dev_addr;
	usbhid_usb_device_apply_topology(&dev);
	dev.maxchild = USBHID_USB_MAXCHILD;
	dev.parent = usbhid_usb_device_parent(&dev);
	if (tuh_vid_pid_get(dev_addr, &vid, &pid)) {
		dev.descriptor.idVendor = vid;
		dev.descriptor.idProduct = pid;
	}

	return usbhid_usb_device_upsert(&dev, false);
}

static void usbhid_pending_hid_probe_clear(struct usbhid_pending_hid_probe *probe)
{
	kfree(probe->desc_report);
	memset(probe, 0, sizeof(*probe));
}

static void usbhid_usb_device_drop_pending_hid(struct usbhid_usb_device *entry)
{
	for (size_t i = 0; i < USBHID_PENDING_HID_MAX; i++)
		usbhid_pending_hid_probe_clear(&entry->pending_hid[i]);
}

static void usbhid_usb_device_drop_pending_hid_instance(struct usbhid_usb_device *entry,
							uint8_t instance)
{
	for (size_t i = 0; i < USBHID_PENDING_HID_MAX; i++) {
		struct usbhid_pending_hid_probe *probe = &entry->pending_hid[i];

		if (probe->valid && probe->instance == instance) {
			usbhid_pending_hid_probe_clear(probe);
			return;
		}
	}
}

static void usbhid_usb_device_remove(uint8_t dev_addr)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_find(dev_addr);

	if (entry) {
		usbhid_usb_device_drop_pending_hid(entry);
		entry->valid = false;
	}
}

struct usb_device *usb_hub_find_child(struct usb_device *hdev, int port1)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_MAX; i++) {
		struct usbhid_usb_device *entry = usbhid_usb_devices[i];

		if (entry && entry->valid && entry->dev.parent == hdev &&
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

	dev.dev_addr = dev_addr;
	usbhid_usb_device_apply_topology(&dev);
	dev.rhport = rhport;
	entry = usbhid_usb_device_upsert(&dev, false);
	if (!entry)
		return;

	while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
		if (p[1] == TUSB_DESC_INTERFACE)
			entry->dev.config_storage.desc.bNumInterfaces++;

		p += p[0];
	}
}

static void usbhid_raw_interface_store(uint8_t dev_addr,
				       tusb_desc_interface_t const *desc,
				       uint16_t max_len)
{
	struct usbhid_raw_interface *free_slot = NULL;
	const uint8_t *p;
	const uint8_t *end;

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

	if (!free_slot)
		return;

	free_slot->valid = true;
	free_slot->dev_addr = dev_addr;
	free_slot->ifnum = desc->bInterfaceNumber;
	free_slot->subclass = desc->bInterfaceSubClass;
	free_slot->protocol = desc->bInterfaceProtocol;
	free_slot->endpoint_count = 0;
	memset(free_slot->endpoint, 0, sizeof(free_slot->endpoint));

	p = (const uint8_t *)desc + desc->bLength;
	end = (const uint8_t *)desc + max_len;
	while (p + 2 <= end && p[0] >= 2 && p + p[0] <= end) {
		if (p[1] == TUSB_DESC_INTERFACE)
			break;

		if (p[1] == USB_DT_ENDPOINT &&
		    free_slot->endpoint_count < USB_HOST_ENDPOINT_MAX) {
			const tusb_desc_endpoint_t *ep =
				(const tusb_desc_endpoint_t *)p;
			struct usb_host_endpoint *host_ep =
				&free_slot->endpoint[free_slot->endpoint_count++];

			host_ep->desc.bLength = ep->bLength;
			host_ep->desc.bDescriptorType = ep->bDescriptorType;
			host_ep->desc.bEndpointAddress = ep->bEndpointAddress;
			host_ep->desc.bmAttributes = ep->bmAttributes.xfer;
			host_ep->desc.wMaxPacketSize = ep->wMaxPacketSize;
			host_ep->desc.bInterval = ep->bInterval;
		}

		p += p[0];
	}
}

static const struct usbhid_raw_interface *
usbhid_raw_interface_lookup(uint8_t dev_addr, uint8_t ifnum)
{
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		const struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr && raw->ifnum == ifnum)
			return raw;
	}

	return NULL;
}

static void usbhid_raw_interface_close(uint8_t dev_addr)
{
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr)
			raw->valid = false;
	}
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

		if (hid && hid->dev_addr == dev_addr && hid->instance == instance)
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
					 u32 device_generation)
{
	struct usbhid_usb_device *entry;
	int ret = -ENODEV;

	taskENTER_CRITICAL();
	entry = usbhid_usb_device_find(hid->dev_addr);
	if (entry && entry->generation == device_generation &&
	    tuh_hid_mounted(hid->dev_addr, hid->instance))
		ret = usbhid_insert(hid);
	taskEXIT_CRITICAL();
	return ret;
}

static int usbhid_probe(uint8_t dev_addr, uint8_t instance,
			uint8_t const *desc_report, uint16_t desc_len,
			const tusb_desc_device_t *device_desc,
			const char *product_name, u32 device_generation);
static void usbhid_usb_device_descriptor_complete(const struct hid_async_request *req,
						  int status);

static int usbhid_usb_device_store_pending_probe(struct usbhid_usb_device *entry,
						 uint8_t instance,
						 uint8_t const *desc_report,
						 uint16_t desc_len)
{
	struct usbhid_pending_hid_probe *slot = NULL;
	uint8_t *rdesc;

	if (!desc_report || !desc_len)
		return -ENODEV;

	rdesc = kmemdup(desc_report, desc_len, GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	for (size_t i = 0; i < USBHID_PENDING_HID_MAX; i++) {
		struct usbhid_pending_hid_probe *probe = &entry->pending_hid[i];

		if (probe->valid && probe->instance == instance) {
			slot = probe;
			usbhid_pending_hid_probe_clear(slot);
			break;
		}
		if (!probe->valid && !slot)
			slot = probe;
	}

	if (!slot) {
		kfree(rdesc);
		return -ENOMEM;
	}

	slot->valid = true;
	slot->instance = instance;
	slot->desc_len = desc_len;
	slot->desc_report = rdesc;
	return 0;
}

static void usbhid_usb_device_run_pending_probes(struct usbhid_usb_device *entry)
{
	for (size_t i = 0; i < USBHID_PENDING_HID_MAX; i++) {
		struct usbhid_pending_hid_probe *probe = &entry->pending_hid[i];
		struct usbhid_lifecycle_event event;

		if (!probe->valid)
			continue;

		memset(&event, 0, sizeof(event));
		event.kind = USBHID_LIFECYCLE_PROBE;
		event.dev_addr = entry->dev.dev_addr;
		event.instance = probe->instance;
		event.generation = entry->generation;
		event.desc_len = probe->desc_len;
		event.desc_report = probe->desc_report;
		event.device_desc = entry->device_desc;
		if (!usbhid_lifecycle_queue ||
		    xQueueSendToBack(usbhid_lifecycle_queue, &event, 0) != pdPASS) {
			async_msg("ERR: HID_PROBE_Q_FAIL");
			continue;
		}

		probe->desc_report = NULL;
		usbhid_pending_hid_probe_clear(probe);
	}
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
					  uint8_t index)
{
	return hid_async_queue_string_descriptor(entry->dev.dev_addr, index,
						 entry->string_langid,
						 usbhid_usb_device_descriptor_complete,
						 entry);
}

static void usbhid_usb_device_finish_preprobe(struct usbhid_usb_device *entry)
{
	entry->preprobe_stage = USBHID_PREPROBE_DONE;
	usbhid_usb_device_run_pending_probes(entry);
}

static void usbhid_usb_device_queue_next_string(struct usbhid_usb_device *entry)
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
							       0, 0,
							       usbhid_usb_device_descriptor_complete,
							       entry);
			break;
		case USBHID_PREPROBE_PRODUCT:
			if (!entry->dev.descriptor.iProduct) {
				entry->preprobe_stage = USBHID_PREPROBE_MANUFACTURER;
				continue;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iProduct);
			break;
		case USBHID_PREPROBE_MANUFACTURER:
			if (!entry->dev.descriptor.iManufacturer) {
				entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
				continue;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iManufacturer);
			break;
		case USBHID_PREPROBE_SERIAL:
			if (!entry->dev.descriptor.iSerialNumber) {
				usbhid_usb_device_finish_preprobe(entry);
				return;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iSerialNumber);
			break;
		default:
			usbhid_usb_device_finish_preprobe(entry);
			return;
		}

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

	if (!entry || !entry->valid || req->dev_addr != entry->dev.dev_addr)
		return;

	if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR) {
		usbhid_usb_device_store_string(entry, req, status);
		if (entry->preprobe_stage == USBHID_PREPROBE_DONE) {
			usbhid_usb_device_finish_preprobe(entry);
			return;
		}
		usbhid_usb_device_queue_next_string(entry);
		return;
	}

	entry->device_desc_requested = false;
	if (status < 0) {
		if (req->xfer_result == XFER_RESULT_INVALID)
			usbhid_log_device_desc_error("SUB", req);
		else
			usbhid_log_device_desc_error("XFER", req);
		usbhid_usb_device_drop_pending_hid(entry);
		return;
	}

	if (req->actual_len < sizeof(entry->device_desc)) {
		usbhid_log_device_desc_error("SHORT", req);
		usbhid_usb_device_drop_pending_hid(entry);
		return;
	}

	memcpy(&entry->device_desc, req->data, sizeof(entry->device_desc));
	entry->dev.descriptor.idVendor = entry->device_desc.idVendor;
	entry->dev.descriptor.idProduct = entry->device_desc.idProduct;
	entry->dev.descriptor.bcdDevice = entry->device_desc.bcdDevice;
	entry->dev.descriptor.bMaxPacketSize0 = entry->device_desc.bMaxPacketSize0;
	entry->dev.descriptor.iManufacturer = entry->device_desc.iManufacturer;
	entry->dev.descriptor.iProduct = entry->device_desc.iProduct;
	entry->dev.descriptor.iSerialNumber = entry->device_desc.iSerialNumber;
	entry->have_device_desc = true;
	entry->string_langid = USBHID_STRING_LANGID;
	entry->preprobe_stage = USBHID_PREPROBE_LANGID;
	usbhid_usb_device_queue_next_string(entry);
}

static void usbhid_usb_device_queue_descriptor(struct usbhid_usb_device *entry)
{
	int ret;

	if (entry->have_device_desc || entry->device_desc_requested)
		return;

	entry->pending_desc_dev_addr = entry->dev.dev_addr;
	entry->preprobe_stage = USBHID_PREPROBE_DEVICE_DESC;
	ret = hid_async_queue_device_descriptor(entry->dev.dev_addr,
						usbhid_usb_device_descriptor_complete,
						entry);
	if (ret) {
		entry->device_desc_requested = false;
		async_msg("ERR: HID_DEV_DESC_Q_FAIL");
		return;
	}

	async_msg("DBG: HID_DEV_DESC_Q");
	entry->device_desc_requested = true;
}

struct usb_interface *usb_ifnum_to_if(const struct usb_device *dev, unsigned int ifnum)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];

		if (hid && hid->usb_dev.dev_addr == dev->dev_addr &&
		    hid->usb_intf.cur_altsetting &&
		    hid->usb_intf.cur_altsetting->desc.bInterfaceNumber == ifnum)
			return &hid->usb_intf;
	}

	return NULL;
}

int usbhid_disconnect_init(void)
{
	usbhid_lifecycle_queue = xQueueCreate(USBHID_LIFECYCLE_QUEUE_LEN,
					      sizeof(struct usbhid_lifecycle_event));
	if (!usbhid_lifecycle_queue)
		return -ENOMEM;

	return 0;
}

void usbhid_disconnect_task(void *pvParameters)
{
	(void)pvParameters;

	for (;;) {
		struct usbhid_lifecycle_event event;
		struct hid_device *hid;
		struct usbhid_usb_device *usb_entry;

		xQueueReceive(usbhid_lifecycle_queue, &event, portMAX_DELAY);
		if (event.kind == USBHID_LIFECYCLE_PROBE) {
			usb_entry = usbhid_usb_device_find(event.dev_addr);
			if (usb_entry && usb_entry->generation == event.generation &&
			    tuh_hid_mounted(event.dev_addr, event.instance))
					usbhid_probe(event.dev_addr, event.instance,
						     event.desc_report, event.desc_len,
						     &event.device_desc, NULL,
						     event.generation);
			kfree(event.desc_report);
			continue;
		}

		hid = usbhid_lookup(event.dev_addr, event.instance);
		if (!hid || (event.generation &&
			    hid->ll_generation != event.generation))
			continue;

		usbhid_report_unplug(hid);
		if (hid_async_cancel_device_sync(hid->dev_addr, hid->instance))
			async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
		usbhid_report_wait_idle(hid);
		usbhid_io_wait_idle(hid);
		hid = usbhid_lookup(event.dev_addr, event.instance);
		if (!hid || (event.generation &&
			    hid->ll_generation != event.generation))
			continue;
		usbhid_remove_slot(hid);
		hid_destroy_device(hid);
	}
}

// static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
// TinyUSB mount callback provides dev_addr/instance/report descriptor instead
// of Linux usb_interface; build the local usb_interface shim before hid_add_device().
static int usbhid_probe(uint8_t dev_addr, uint8_t instance,
			uint8_t const *desc_report, uint16_t desc_len,
			const tusb_desc_device_t *device_desc,
			const char *product_name, u32 device_generation)
{
	struct hid_device *hid;
	uint8_t *rdesc;
	const struct usbhid_raw_interface *raw;
	struct usbhid_usb_device *usb_entry;
	uint16_t vid = 0;
	uint16_t pid = 0;
	tuh_itf_info_t itf_info;
	const char *name;
	u32 malloc_failures_before;
	int ret;

	if (!desc_report || !desc_len) {
		async_msg("ERR: HID_DESC_MISSING");
		return -ENODEV;
	}

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		async_msg("ERR: HID_ALLOC_FAIL");
		return PTR_ERR(hid);
	}

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// TinyUSB passes the report descriptor into the mount callback.
	rdesc = kmemdup(desc_report, desc_len, GFP_KERNEL);
	if (!rdesc) {
		hid_destroy_device(hid);
		async_msg("ERR: HID_DESC_ALLOC_FAIL");
		return -ENOMEM;
	}

	memset(&itf_info, 0, sizeof(itf_info));
	if (!tuh_vid_pid_get(dev_addr, &vid, &pid) ||
	    !tuh_hid_itf_get_info(dev_addr, instance, &itf_info)) {
		kfree(rdesc);
		hid_destroy_device(hid);
		async_msg("ERR: HID_ENUM_GONE");
		return -ENODEV;
	}

	device_initialize(&hid->usb_dev.dev);
	device_initialize(&hid->usb_intf.dev);
	// Upstream USB core sets device types before usbhid sees usb_interface.
	// TinyUSB port owns the local usb_device/usb_interface shims.
	hid->usb_dev.dev.type = &usb_device_type;
	hid->usb_intf.dev.type = &usb_if_device_type;
	usbhid_usb_device_init_config(&hid->usb_dev);

	// hid->dev.parent = &intf->dev;
	// TinyUSB mount callback builds a local usb_interface shim for this HID instance.
	hid->dev_addr = dev_addr;
	hid->instance = instance;
	hid->ll_generation = usbhid_next_generation();
	hid->usb_dev.dev_addr = dev_addr;
	usbhid_usb_device_apply_topology(&hid->usb_dev);
	usb_entry = usbhid_usb_device_find(dev_addr);
	if (usb_entry && usb_entry->dev.actconfig)
		hid->usb_dev.config_storage.desc = usb_entry->dev.actconfig->desc;
	if (device_desc) {
		// dev = interface_to_usbdev(intf);
		// TinyUSB mount callback has no Linux usb_device yet; use the
		// async device-descriptor snapshot captured before this probe.
		hid->usb_dev.descriptor.idVendor = device_desc->idVendor;
		hid->usb_dev.descriptor.idProduct = device_desc->idProduct;
		hid->usb_dev.descriptor.bcdDevice = device_desc->bcdDevice;
		hid->usb_dev.descriptor.bMaxPacketSize0 = device_desc->bMaxPacketSize0;
		hid->usb_dev.descriptor.iManufacturer = device_desc->iManufacturer;
		hid->usb_dev.descriptor.iProduct = device_desc->iProduct;
		hid->usb_dev.descriptor.iSerialNumber = device_desc->iSerialNumber;
	} else {
		hid->usb_dev.descriptor.idVendor = vid;
		hid->usb_dev.descriptor.idProduct = pid;
	}
	name = product_name;
	if (!name && usb_entry && usb_entry->dev.product)
		name = usb_entry->dev.product;
	if (name && name[0]) {
		strscpy(hid->usb_dev.product_buf, name,
			sizeof(hid->usb_dev.product_buf));
		hid->usb_dev.product = hid->usb_dev.product_buf;
	}
	if (usb_entry && usb_entry->dev.manufacturer) {
		strscpy(hid->usb_dev.manufacturer_buf,
			usb_entry->dev.manufacturer,
			sizeof(hid->usb_dev.manufacturer_buf));
		hid->usb_dev.manufacturer = hid->usb_dev.manufacturer_buf;
	}
	if (usb_entry && usb_entry->dev.serial) {
		strscpy(hid->usb_dev.serial_buf, usb_entry->dev.serial,
			sizeof(hid->usb_dev.serial_buf));
		hid->usb_dev.serial = hid->usb_dev.serial_buf;
		strscpy(hid->uniq, usb_entry->dev.serial, sizeof(hid->uniq));
	}
	hid->usb_dev.maxchild = USBHID_USB_MAXCHILD;
	hid->usb_dev.parent = usbhid_usb_device_parent(&hid->usb_dev);
	if (!usbhid_usb_device_copy_if_generation(&hid->usb_dev,
						 device_desc != NULL,
						 device_generation)) {
		ret = -ENODEV;
		goto fail;
	}

	hid->usb_altsetting.desc.bInterfaceNumber = itf_info.desc.bInterfaceNumber;
	raw = usbhid_raw_interface_lookup(dev_addr, itf_info.desc.bInterfaceNumber);
	if (raw) {
		hid->usb_altsetting.desc.bInterfaceSubClass = raw->subclass;
		hid->usb_altsetting.desc.bInterfaceProtocol = raw->protocol;
		hid->usb_altsetting.desc.bNumEndpoints = raw->endpoint_count;
		memcpy(hid->usb_altsetting.endpoint, raw->endpoint,
		       sizeof(hid->usb_altsetting.endpoint));
		for (u8 i = 0; i < raw->endpoint_count; i++) {
			const struct usb_endpoint_descriptor *ep =
				&hid->usb_altsetting.endpoint[i].desc;

			if ((ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT &&
			    usb_endpoint_xfer_int(ep)) {
				hid->usb_altsetting.has_interrupt_out = true;
				hid->usb_altsetting.interrupt_out_endpoint =
					ep->bEndpointAddress;
			}
		}
	} else {
		hid->usb_altsetting.desc.bInterfaceSubClass = itf_info.desc.bInterfaceSubClass;
		hid->usb_altsetting.desc.bInterfaceProtocol = itf_info.desc.bInterfaceProtocol;
		hid->usb_altsetting.desc.bNumEndpoints = itf_info.desc.bNumEndpoints;
	}
	hid->usb_intf.altsetting = &hid->usb_altsetting;
	hid->usb_intf.cur_altsetting = &hid->usb_altsetting;
	hid->usb_intf.dev.parent = &hid->usb_dev.dev;
	hid->dev.parent = &hid->usb_intf.dev;
	usb_set_intfdata(&hid->usb_intf, hid);

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
	hid->ll_rdesc = rdesc;
	hid->ll_rsize = desc_len;
	init_waitqueue_head(&hid->ll_wait);

	hid->bus = BUS_USB;
	hid->vendor = vid;
	hid->product = pid;
	hid->version = le16_to_cpu(hid->usb_dev.descriptor.bcdDevice);
	// if (intf->cur_altsetting->desc.bInterfaceProtocol ==
	//		USB_INTERFACE_PROTOCOL_MOUSE)
	//	hid->type = HID_TYPE_USBMOUSE;
	// else if (intf->cur_altsetting->desc.bInterfaceProtocol == 0)
	//	hid->type = HID_TYPE_USBNONE;
	// Port has no Linux usb_interface from USB core; use the raw interface
	// descriptor snapshot captured before TinyUSB normalizes HID protocol.
	if (hid->usb_altsetting.desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE)
		hid->type = HID_TYPE_USBMOUSE;
	else if (hid->usb_altsetting.desc.bInterfaceProtocol == 0)
		hid->type = HID_TYPE_USBNONE;

	// hid->name[0] = 0;
	// if (dev->manufacturer)
	// 	strscpy(hid->name, dev->manufacturer, sizeof(hid->name));
	//
	// if (dev->product) {
	// 	if (dev->manufacturer)
	// 		strlcat(hid->name, " ", sizeof(hid->name));
	// 	strlcat(hid->name, dev->product, sizeof(hid->name));
	// }
	//
	// if (!strlen(hid->name))
	// 	snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x",
	// 		 le16_to_cpu(dev->descriptor.idVendor),
	// 		 le16_to_cpu(dev->descriptor.idProduct));
	// Port mirrors upstream usbhid name construction using the TinyUSB-backed
	// usb_device snapshot prepared above.
	hid->name[0] = 0;
	if (hid->usb_dev.manufacturer)
		strscpy(hid->name, hid->usb_dev.manufacturer, sizeof(hid->name));

	if (hid->usb_dev.product) {
		if (hid->usb_dev.manufacturer)
			strlcat(hid->name, " ", sizeof(hid->name));
		strlcat(hid->name, hid->usb_dev.product, sizeof(hid->name));
	}

	if (!strlen(hid->name))
		snprintf(hid->name, sizeof(hid->name), "HID %04x:%04x",
			 le16_to_cpu(hid->usb_dev.descriptor.idVendor),
			 le16_to_cpu(hid->usb_dev.descriptor.idProduct));
	usb_make_path(&hid->usb_dev, hid->phys, sizeof(hid->phys));
	strlcat(hid->phys, "/input", sizeof(hid->phys));

	ret = usbhid_insert_if_generation(hid, device_generation);
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
		if (hid_async_cancel_device_sync(hid->dev_addr, hid->instance))
			async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
		usbhid_report_wait_idle(hid);
		usbhid_io_wait_idle(hid);
		usbhid_remove_slot(hid);
		async_msg(ret == -ENODEV ? "WARN: HID_IGNORED" : "ERR: HID_ADD_FAIL");
		goto fail;
	}

	// tuh_hid_receive_report(dev_addr, instance);
	// Start interrupt IN from usbhid_open()/usbhid_start() after the Linux HID
	// device binds, matching upstream hid_start_in() lifecycle instead of
	// probe-time report delivery.
	hid->ll_rdesc = NULL;
	hid->ll_rsize = 0;
	kfree(rdesc);
	return 0;

fail:
	kfree(rdesc);
	hid_destroy_device(hid);
	return ret;
}

// static void usbhid_disconnect(struct usb_interface *intf)
// TinyUSB unmount callback identifies the HID interface by dev_addr + instance.
static void usbhid_disconnect(uint8_t dev_addr, uint8_t instance)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);
	int ret;
	struct usbhid_lifecycle_event event = {
		.kind = USBHID_LIFECYCLE_DISCONNECT,
		.dev_addr = dev_addr,
		.instance = instance,
	};

	if (hid) {
		taskENTER_CRITICAL();
		if (hid->ll_disconnect_queued) {
			taskEXIT_CRITICAL();
			return;
		}
		hid->ll_disconnect_queued = true;
		event.generation = hid->ll_generation;
		taskEXIT_CRITICAL();

		usbhid_report_unplug(hid);
	}

	ret = hid_async_cancel_device(dev_addr, instance);

	if (ret)
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	/*
	 * Upstream Linux runs usbhid_disconnect() from USB core process
	 * context and can call hid_destroy_device() directly. TinyUSB calls
	 * this hook from unmount callback context, so driver remove is handed
	 * to a firmware task before any Linux-style flush/cancel waits run.
	 */
	if (!usbhid_lifecycle_queue ||
	    xQueueSendToBack(usbhid_lifecycle_queue, &event, 0) != pdPASS)
		async_msg("ERR: HID_DISCONNECT_Q_FAIL");
}

void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      uint8_t const *desc_report, uint16_t desc_len)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_prepare(dev_addr);
	int ret;

	async_msg("DBG: HID_MOUNT_CB");
	if (!entry) {
		async_msg("ERR: HID_USB_DEV_ALLOC_FAIL");
		return;
	}

	ret = usbhid_usb_device_store_pending_probe(entry, instance, desc_report,
						    desc_len);
	if (ret) {
		async_msg("ERR: HID_PROBE_DEFER_FAIL");
		return;
	}

	if (entry->have_device_desc &&
	    entry->preprobe_stage == USBHID_PREPROBE_DONE)
		usbhid_usb_device_run_pending_probes(entry);
}

void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_find(dev_addr);

	if (entry)
		usbhid_usb_device_drop_pending_hid_instance(entry, instance);
	usbhid_disconnect(dev_addr, instance);
}

void usbhid_backend_device_mount(uint8_t dev_addr)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_prepare(dev_addr);

	async_msg("DBG: USB_MOUNT_CB");
	if (!entry) {
		async_msg("ERR: HID_USB_DEV_ALLOC_FAIL");
		return;
	}

	usbhid_usb_device_queue_descriptor(entry);
}

void usbhid_backend_device_umount(uint8_t dev_addr)
{
	int ret;

	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];

		if (hid && hid->dev_addr == dev_addr)
			usbhid_report_unplug(hid);
	}

	ret = hid_async_cancel_dev_addr(dev_addr);

	if (ret)
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_usb_device_remove(dev_addr);
}

void usbhid_backend_report_received(uint8_t dev_addr, uint8_t instance,
				    uint8_t const *report, uint16_t len)
{
	struct hid_device *hid = usbhid_lookup(dev_addr, instance);
	uint8_t protocol_mode = tuh_hid_get_protocol(dev_addr, instance);
	int ret;

	if (protocol_mode == HID_PROTOCOL_BOOT)
		async_msg("WARN: HID_PROTOCOL_BOOT");

	if (hid) {
		ret = usbhid_report_submit(hid, report, len,
					   protocol_mode != HID_PROTOCOL_BOOT);
		if (ret < 0)
			async_msg("ERR: HID_REPORT_SKIP");
	} else {
		async_msg("ERR: HID_REPORT_SKIP");
	}
}

static int usbhid_start(struct hid_device *hid)
{
	if (usbhid_report_is_stopping(hid))
		return -ENODEV;

	/*
	 * Upstream usbhid start may arm polling, reset LEDs, and enable wakeup.
	 * Current callback slice has no URB allocation here; TinyUSB interrupt IN
	 * starts here only for ALWAYS_POLL, matching upstream hid_start_in().
	 */
	if (hid->quirks & HID_QUIRK_ALWAYS_POLL)
		return usbhid_report_start(hid);

	return 0;
}

static void usbhid_stop(struct hid_device *hid)
{
	/*
	 * Upstream usbhid stop cancels URBs and queued delayed work. The firmware
	 * transport drains its matching async request before driver state is freed.
	 */
	usbhid_report_stop(hid);
	if (hid_async_cancel_device_sync(hid->dev_addr, hid->instance))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_report_wait_idle(hid);
	usbhid_io_wait_idle(hid);
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
	/*
	 * Upstream close kills interrupt IN unless ALWAYS_POLL is active. The port
	 * stops rearming here; one already armed transfer may remain parked and is
	 * either reused by reopen or consumed once without entering the parser.
	 */
	if (!(hid->quirks & HID_QUIRK_ALWAYS_POLL))
		usbhid_report_close(hid);
}

static int usbhid_parse(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	unsigned long quirks = hid->quirks;
	unsigned long transport_quirks = 0;
	int ret;

	if (!hid->ll_rdesc || !hid->ll_rsize)
		return -ENODEV;

	if (interface->desc.bInterfaceSubClass == USB_INTERFACE_SUBCLASS_BOOT) {
		if (interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_KEYBOARD ||
		    interface->desc.bInterfaceProtocol == USB_INTERFACE_PROTOCOL_MOUSE) {
			/*
			 * Keep upstream usbhid_parse() behavior for boot keyboards/mice:
			 * do not GET_REPORT during init. In this slice all hardware
			 * request paths are disabled anyway, but keeping the quirk matters
			 * for later async request work.
			 */
			quirks |= HID_QUIRK_NOGET;
			transport_quirks |= HID_QUIRK_NOGET;
		}
	}

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// TinyUSB supplies the report descriptor to tuh_hid_mount_cb(); this
	// ll_driver consumes it here so hid_add_device() keeps upstream parse flow.
	ret = hid_parse_report(hid, hid->ll_rdesc, hid->ll_rsize);
	if (ret)
		return ret;

	// hid->quirks |= quirks;
	// hid_device_probe() recomputes quirks from initial_quirks after parse();
	// keep transport-added usbhid_parse() quirks alive.
	hid->initial_quirks |= transport_quirks;
	hid->quirks |= quirks;
	return 0;
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
	bool acquired = false;

	taskENTER_CRITICAL();
	if (!hid->ll_transport_stopping) {
		hid->ll_io_pending++;
		acquired = true;
	}
	taskEXIT_CRITICAL();
	return acquired;
}

static void usbhid_io_put(struct hid_device *hid)
{
	taskENTER_CRITICAL();
	configASSERT(hid->ll_io_pending);
	hid->ll_io_pending--;
	taskEXIT_CRITICAL();
}

static void usbhid_io_wait_idle(struct hid_device *hid)
{
	bool idle;

	do {
		taskENTER_CRITICAL();
		idle = hid->ll_io_pending == 0;
		taskEXIT_CRITICAL();
		if (!idle)
			vTaskDelay(1);
	} while (!idle);
}

static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype)
{
	struct usbhid_sync_request sync = {
		.task = xTaskGetCurrentTaskHandle(),
	};
	u8 *report_data = NULL;
	int ret;

	if (!usbhid_io_get(hid))
		return;
	if (reqtype == HID_REQ_GET_REPORT) {
		report_data = hid_alloc_report_buf(report, GFP_KERNEL);
		if (!report_data) {
			async_msg("ERR: HID_REPORT_NOMEM");
			goto out;
		}
		sync.buf = report_data;
		sync.bufsize = hid_report_len(report) + 7 + (report->id == 0);
	}

	// usbhid_submit_report(hid, report, reqtype);
	// Queue transport work and wait only in the Linux-facing caller task. The
	// transfer owner copies the result and wakes us; it never enters hid-core.
	ret = hid_async_queue_report(hid, report, reqtype,
				     usbhid_sync_complete, &sync);
	ret = usbhid_sync_wait(&sync, ret);
	if (ret) {
		async_msg("ERR: HID_ASYNC_REQ_FAIL");
	} else if (reqtype == HID_REQ_GET_REPORT &&
		   !usbhid_report_is_stopping(hid)) {
		(void)hid_safe_input_report(hid, report->type, report_data,
					    sync.bufsize, sync.actual_len, 0);
	}

out:
	kfree(report_data);
	usbhid_io_put(hid);
}

static int usbhid_wait_io(struct hid_device *hid)
{
	/*
	 * usbhid_wait_io(hid);
	 * The firmware request/raw_request/output callbacks already wait for their
	 * async TinyUSB completion, so no separate transport wait remains here.
	 */
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
			ret = (int)len;
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
	// TinyUSB interrupt OUT is queued through the HID async task; the report
	// sent callback only wakes that task and never blocks TinyUSB callbacks.
	int ret = hid_async_queue_output_report(hid, buf, len,
						usbhid_sync_complete, &sync);
	ret = usbhid_sync_wait(&sync, ret);
	if (!ret)
		ret = (int)len;
	usbhid_io_put(hid);
	return ret;
}

static int usbhid_idle(struct hid_device *hid, int report, int idle, int reqtype)
{
	/*
	 * return hid_set_idle(dev, ifnum, report, idle);
	 * SET_IDLE is a synchronous control request; keep it deferred with the
	 * rest of the hardware request path.
	 */
	(void)hid;
	(void)report;
	(void)idle;
	(void)reqtype;
	return 0;
}

int tuh_usb_control_msg(struct usb_device *dev, unsigned int pipe,
			u8 request, u8 requesttype, u16 value, u16 index,
			void *data, u16 size, int timeout)
{
	/*
	 * Synchronous EP0 control is intentionally disabled in the callback-driven
	 * parser slice. Any caller reaching this must be converted to async later.
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
	struct hid_device *hid = container_of(urb->dev, struct hid_device, usb_dev);

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
