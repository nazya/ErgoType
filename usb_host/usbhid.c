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
/* A canceled old epoch may overlap all interface metadata from a fast replug. */
#define USBHID_REPORT_DESCRIPTOR_SLOTS (HID_HOST_MAX_DEVICES + 1u)
#define USBHID_REPORT_DESCRIPTOR_RETRIES 4u
#define USBHID_DEVICE_DESCRIPTOR_ATTEMPTS 4u
#define USBHID_DEVICE_DESCRIPTOR_RETRY_MS 100u
#define USBHID_RESET_HUB_ATTEMPTS 3u
#define USBHID_RESET_PHASE_TIMEOUT_MS 6000u
#define USBHID_RESET_HUB_TIMEOUT_MS \
	(USB_CTRL_SET_TIMEOUT * USBHID_RESET_HUB_ATTEMPTS + 1000u)
#define USBHID_RESET_POLL_MS 10u

_Static_assert(HID_MAX_BUFFER_SIZE + 8u <= UINT16_MAX,
	       "HID control buffer size must fit TinyUSB's 16-bit length");
_Static_assert(HID_MAX_DESCRIPTOR_SIZE <= UINT16_MAX,
	       "HID report descriptor size must fit TinyUSB's 16-bit length");

/*
 * SHA-pinned build-local TinyUSB host-owner extensions. They enter the same
 * enum_new_device() path without recursively sending to the host queue.
 */
bool usbh_port_attach_on_host(uint8_t rhport, uint8_t hub_addr,
			      uint8_t hub_port);
bool usbh_port_reenumerate_on_host(uint8_t rhport, uint8_t hub_addr,
				   uint8_t hub_port);

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
	size_t actual_len;
	int status;
	volatile bool done;
};

/* EP0-rounded GET buffer owned from .request enqueue through parser completion. */
struct usbhid_control_input {
	TaskHandle_t parser_owner;
	/* Stable async slot identity held until parser completion. */
	u32 async_serial;
	u16 bufsize;
	u8 data[];
};

enum usbhid_report_descriptor_state {
	USBHID_REPORT_DESCRIPTOR_FREE,
	USBHID_REPORT_DESCRIPTOR_WAIT_PREPROBE,
	USBHID_REPORT_DESCRIPTOR_FETCH_PENDING,
	USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE,
	USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED,
	USBHID_REPORT_DESCRIPTOR_RELEASE_PENDING,
	USBHID_REPORT_DESCRIPTOR_READY,
	USBHID_REPORT_DESCRIPTOR_PROBING,
};

struct usbhid_report_descriptor_slot {
	enum usbhid_report_descriptor_state state;
	u8 dev_addr;
	u8 instance;
	u8 ifnum;
	u8 fetch_retries;
	u16 len;
	u32 generation;
	u32 serial;
	u8 *data;
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

/*
 * Upstream Linux queues usb_reset_device() from error recovery and lets USB
 * core serialize it. Firmware uses the stronger full teardown/reprobe fallback
 * in this lifecycle state machine because TinyUSB has one global enumeration
 * EP0 and no usbcore in-place reset owner.
 */
enum usbhid_reset_state {
	USBHID_RESET_IDLE,
	USBHID_RESET_WAIT_RETIRE,
	USBHID_RESET_WAIT_CONTROL_IDLE,
	USBHID_RESET_ROOT_ATTACH_ACTIVE,
	USBHID_RESET_HUB_IO_ACTIVE,
	USBHID_RESET_HUB_RETRY_WAIT,
	USBHID_RESET_WAIT_REENUM,
	USBHID_RESET_COMPLETE,
	USBHID_RESET_FAILED,
	USBHID_RESET_CANCELLED,
};

struct usbhid_reset_coordinator {
	enum usbhid_reset_state state;
	u32 generation;
	u32 parent_generation;
	u32 replacement_generation;
	TickType_t deadline;
	TickType_t gate_started;
	u8 dev_addr;
	u8 rhport;
	u8 hub_addr;
	u8 hub_port;
	u8 attempts;
	bool gate_held;
	bool hub_io_pending;
	bool root_io_pending;
	bool enum_active;
};

struct usbhid_usb_device {
	bool valid;
	bool retiring;
	bool have_device_desc;
	bool device_desc_requested;
	u32 generation;
	u32 io_pending;
	u32 interface_mask;
	TickType_t device_desc_retry_at;
	enum usbhid_preprobe_stage preprobe_stage;
	u8 device_desc_attempts_left;
	/* These flags share the enum-alignment byte in the physical cache. */
	bool reset_requested : 1;
	bool mount_complete : 1;
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
	struct usbhid_reset_coordinator reset;
};

static struct usb_device usbhid_root_hub;
static bool usbhid_root_hub_valid;
static struct usbhid_transport_pool *usbhid_transport_pool;
#define usbhid_reset (&usbhid_transport_pool->reset)
static struct usbhid_usb_device *usbhid_usb_devices;
static struct usbhid_usb_device *usbhid_retired_devices;
static struct usbhid_report_descriptor_slot *usbhid_report_descriptors;
static struct usbhid_raw_interface usbhid_raw_interfaces[HID_HOST_RAW_INTERFACE_MAX];
static u32 usbhid_generation;
static u32 usbhid_report_descriptor_serial;
static u32 usbhid_transport_faults;
static bool usbhid_lifecycle_wake_pending;
static bool usbhid_io_get(struct hid_device *hid);
static void usbhid_io_put(struct hid_device *hid);
static void usbhid_io_wait_idle(struct hid_device *hid);
static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype);
static void usbhid_lifecycle_kick(void);
static void usbhid_transport_fault(enum usbhid_transport_fault fault);
static void usbhid_backend_device_detach(uint8_t dev_addr);
static bool usbhid_usb_device_has_waiting_hid(
		const struct usbhid_usb_device *entry);
static int usbhid_reset_process(void);
static TickType_t usbhid_reset_wait_ticks(void);
static void hid_free_buffers(struct usb_device *dev, struct hid_device *hid);

static u32 usbhid_next_generation_locked(void)
{
	u32 generation = ++usbhid_generation;

	if (!generation)
		generation = ++usbhid_generation;
	return generation;
}

static bool usbhid_tick_reached(TickType_t now, TickType_t deadline)
{
	/* RP2040 FreeRTOS ticks are 32-bit; signed subtraction is wrap-safe here. */
	return (int32_t)(now - deadline) >= 0;
}

static u32 usbhid_next_generation(void)
{
	u32 generation;

	taskENTER_CRITICAL();
	generation = usbhid_next_generation_locked();
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

/*
 * Upstream USB core pins struct usb_device while a synchronous message sleeps.
 * This transport lease pins the matching cache epoch and separately captures
 * hid_async's address generation before detach can retarget work at a reused
 * TinyUSB address.
 */
static int usbhid_usb_device_io_get(struct usb_device *dev,
				    struct usbhid_usb_device **owner,
				    u8 *dev_addr, u32 *async_generation)
{
	struct usbhid_usb_device *entry = NULL;
	u8 addr;
	int ret;

	if (!dev || !owner || !dev_addr || !async_generation)
		return -EINVAL;
	addr = dev->dev_addr;
	ret = hid_async_device_epoch_snapshot(addr, async_generation);
	if (ret)
		return ret;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *candidate = &usbhid_usb_devices[i];

		if (&candidate->dev == dev && candidate->valid &&
		    candidate->dev.dev_addr == addr) {
			candidate->io_pending++;
			entry = candidate;
			break;
		}
	}
	taskEXIT_CRITICAL();
	if (!entry)
		return -ENODEV;

	*owner = entry;
	*dev_addr = addr;
	return 0;
}

static void usbhid_usb_device_io_put(struct usbhid_usb_device *entry)
{
	bool wake_lifecycle;

	taskENTER_CRITICAL();
	configASSERT(entry && entry->io_pending);
	entry->io_pending--;
	wake_lifecycle = entry->retiring && !entry->io_pending;
	taskEXIT_CRITICAL();
	if (wake_lifecycle)
		usbhid_lifecycle_kick();
}

/*
 * Linux usb_pipe_endpoint() resolves an endpoint from struct usb_device. This
 * compact TinyUSB cache keeps endpoint descriptors in its live HID interface
 * shims instead, so resolve the same physical-device/endpoint relationship at
 * the transport boundary without growing every device-cache entry. Pin that
 * interface at the same time so its generic request participates in firmware
 * interface stop/cancel. This is a local stronger lifetime adaptation; Linux
 * `usb_interrupt_msg()` creates a synchronous device/endpoint URB rather than
 * attaching that hidden URB to `struct usbhid_device`.
 */
static int usbhid_usb_device_interrupt_owner_get(
		const struct usb_device *dev, u8 ep_addr,
		struct hid_device **owner)
{
	const u8 ep_addrs[] = { ep_addr, 0 };
	int ret = -EINVAL;

	*owner = NULL;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && interface_to_usbdev(usbhid->intf) == dev &&
		    usb_check_int_endpoints(usbhid->intf, ep_addrs)) {
			if (usbhid->transport_stopping) {
				ret = -ENODEV;
			} else {
				usbhid->io_pending++;
				*owner = hid;
				ret = 0;
			}
			break;
		}
	}
	taskEXIT_CRITICAL();
	return ret;
}

/*
 * Linux `usb_control_msg()` creates a synchronous device request; it does not
 * infer an interface owner from wIndex. The firmware bridge deliberately tags
 * a matching HID interface so stop can cancel/wake that task-side wait before
 * teardown. Non-interface requests remain physical-device-owned.
 */
static int usbhid_usb_device_control_owner_get(
		const struct usb_device *dev, u8 requesttype, u16 index,
		struct hid_device **owner)
{
	int ret = 0;

	*owner = NULL;
	if ((requesttype & USB_RECIP_MASK) != USB_RECIP_INTERFACE)
		return 0;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (!usbhid || interface_to_usbdev(usbhid->intf) != dev ||
		    usbhid->ifnum != index)
			continue;
		if (usbhid->transport_stopping) {
			ret = -ENODEV;
		} else {
			usbhid->io_pending++;
			*owner = hid;
		}
		break;
	}
	taskEXIT_CRITICAL();
	return ret;
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
	struct usbhid_usb_device *entry = NULL;
	struct usb_device *parent;
	enum usbhid_transport_fault fault = 0;

	/*
	 * Upstream Linux USB core publishes a fully initialized usb_device under
	 * its device-model locks. Keep the firmware cache copy and valid bit behind
	 * one SMP publication fence; callbacks only report a fault after unlocking.
	 */
	taskENTER_CRITICAL();
	parent = usbhid_usb_device_parent(src);
	/* A child without its live hub epoch must never masquerade as root-owned. */
	if (!parent) {
		fault = USBHID_FAULT_TOPOLOGY;
		goto out;
	}

	entry = usbhid_usb_device_slot(src->dev_addr);
	if (!entry) {
		fault = USBHID_FAULT_DEVICE_CACHE_FULL;
		goto out;
	}

	if (!entry->valid) {
		memset(entry, 0, sizeof(*entry));
		device_initialize(&entry->dev.dev);
		entry->dev.dev.type = &usb_device_type;
		usbhid_usb_device_init_config(&entry->dev);
		entry->generation = usbhid_next_generation_locked();
		entry->device_desc_attempts_left =
			USBHID_DEVICE_DESCRIPTOR_ATTEMPTS;
		entry->device_desc_retry_at = xTaskGetTickCount();
	}
	usbhid_usb_device_copy(entry, src, parent);
	/* valid is the release publication for all fields copied above. */
	entry->valid = true;
out:
	taskEXIT_CRITICAL();
	if (fault)
		usbhid_transport_fault(fault);
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
	configASSERT(!slot->data);
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
	bool release_pending = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		struct usbhid_report_descriptor_slot *slot =
			&usbhid_report_descriptors[i];

		if (slot->state == USBHID_REPORT_DESCRIPTOR_FREE ||
		    slot->dev_addr != dev_addr ||
		    (match_generation && slot->generation != generation) ||
		    (instance >= 0 && slot->instance != (u8)instance))
			continue;

		/*
		 * TinyUSB unmount callbacks cannot free memory. An active EP0 fetch
		 * retains its borrowed buffer through hid_async cancellation; a READY
		 * buffer is handed to the lifecycle task for release. PROBING already
		 * moved ownership into that task, so rotating its token is sufficient.
		 */
		slot->serial = usbhid_next_report_descriptor_serial_locked();
		if (slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE) {
			slot->state = USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED;
		} else if (slot->state == USBHID_REPORT_DESCRIPTOR_READY ||
			   (slot->state ==
				    USBHID_REPORT_DESCRIPTOR_FETCH_PENDING &&
			    slot->data)) {
			slot->state = USBHID_REPORT_DESCRIPTOR_RELEASE_PENDING;
			release_pending = true;
		} else if (slot->state !=
			   USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED &&
			   slot->state !=
			   USBHID_REPORT_DESCRIPTOR_RELEASE_PENDING) {
			usbhid_report_descriptor_free(slot);
		}
	}
	taskEXIT_CRITICAL();

	if (release_pending)
		usbhid_lifecycle_kick();
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

static void usbhid_report_descriptor_release_pending(void)
{
	for (;;) {
		struct usbhid_report_descriptor_slot *slot = NULL;
		u8 *data = NULL;

		taskENTER_CRITICAL();
		for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
			if (usbhid_report_descriptors[i].state !=
			    USBHID_REPORT_DESCRIPTOR_RELEASE_PENDING)
				continue;
			slot = &usbhid_report_descriptors[i];
			data = slot->data;
			slot->data = NULL;
			usbhid_report_descriptor_free(slot);
			break;
		}
		taskEXIT_CRITICAL();

		if (!slot)
			return;
		/* heap_4 is task-owned; TinyUSB callbacks only publish RELEASE_PENDING. */
		kfree(data);
	}
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

static bool usbhid_usb_device_has_live_io(
		const struct usbhid_usb_device *entry)
{
	bool found;

	taskENTER_CRITICAL();
	found = entry->io_pending != 0;
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
		    usbhid_usb_device_has_live_hids(entry) ||
		    usbhid_usb_device_has_live_io(entry) ||
		    usbhid_usb_device_has_waiting_hid(entry)) {
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
		    snapshot.endpoint_count < desc->bNumEndpoints &&
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

/* Caller holds the firmware SMP critical section across lookup and pinning. */
static struct hid_device *usbhid_lookup_locked(uint8_t dev_addr,
					       uint8_t instance,
					       u32 generation)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && usbhid->dev_addr == dev_addr &&
		    usbhid->instance == instance &&
		    (!generation || usbhid->generation == generation))
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
	/* Pair slot removal with SMP readers before hid/usbhid storage is freed. */
	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (usbhid_devices[i] == hid) {
			usbhid_devices[i] = NULL;
			break;
		}
	}
	taskEXIT_CRITICAL();
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
						 uint8_t instance)
{
	struct usbhid_report_descriptor_slot *slot = NULL;
	struct usbhid_raw_interface raw;
	struct hid_descriptor hdesc;
	tuh_itf_info_t itf_info;
	unsigned int rsize;
	bool already_pending = false;
	bool stored = false;

	memset(&itf_info, 0, sizeof(itf_info));
	if (!tuh_hid_itf_get_info(entry->dev.dev_addr, instance, &itf_info) ||
	    !usbhid_raw_interface_copy(entry->dev.dev_addr,
				       itf_info.desc.bInterfaceNumber, &raw))
		return -ENODEV;
	memcpy(&hdesc, raw.hid_descriptor, sizeof(hdesc));
	if (hdesc.bDescriptorType != HID_DT_HID ||
	    hdesc.rpt_desc.bDescriptorType != HID_DT_REPORT)
		return -EINVAL;
	rsize = le16_to_cpu(hdesc.rpt_desc.wDescriptorLength);
	if (!rsize || rsize > HID_MAX_DESCRIPTOR_SIZE)
		return -EINVAL;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		struct usbhid_report_descriptor_slot *candidate =
			&usbhid_report_descriptors[i];

		if (candidate->state != USBHID_REPORT_DESCRIPTOR_FREE &&
		    candidate->dev_addr == entry->dev.dev_addr &&
		    candidate->instance == instance &&
		    candidate->generation == entry->generation) {
			already_pending = true;
			break;
		}
		if (candidate->state == USBHID_REPORT_DESCRIPTOR_FREE && !slot)
			slot = candidate;
	}

	if (!already_pending && slot && entry->valid) {
		slot->state = entry->have_device_desc &&
			      entry->preprobe_stage == USBHID_PREPROBE_DONE ?
			      USBHID_REPORT_DESCRIPTOR_FETCH_PENDING :
			      USBHID_REPORT_DESCRIPTOR_WAIT_PREPROBE;
		slot->dev_addr = entry->dev.dev_addr;
		slot->instance = instance;
		slot->ifnum = itf_info.desc.bInterfaceNumber;
		slot->fetch_retries = USBHID_REPORT_DESCRIPTOR_RETRIES;
		slot->len = (u16)rsize;
		slot->generation = entry->generation;
		slot->serial = usbhid_next_report_descriptor_serial_locked();
		slot->data = NULL;
		stored = true;
	}
	taskEXIT_CRITICAL();

	/* TinyUSB does not normally repeat mount for a live interface. */
	if (already_pending)
		return 0;
	if (!stored)
		return -ENOMEM;
	/* Descriptor-fetch work and deferred pre-probe submission are authoritative. */
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

static void usbhid_report_descriptor_fetch_complete(
		const struct hid_async_request *req, int status)
{
	struct usbhid_report_descriptor_slot *slot = req->context;
	bool fault;
	bool ready;
	bool release;
	bool retry;

	taskENTER_CRITICAL();
	configASSERT(slot && slot->data == req->data &&
		     (slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE ||
		      slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED));
	ready = slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE &&
		!status && (req->actual_len >= slot->len ||
			    slot->fetch_retries == 1);
	/* A canceled physical epoch cannot service Linux's remaining retries. */
	retry = slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE &&
		status != -ENODEV && !ready && slot->fetch_retries > 1;
	fault = slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE &&
		status != -ENODEV && !ready && !retry;
	release = !ready && !retry;
	if (ready) {
		slot->state = USBHID_REPORT_DESCRIPTOR_READY;
	} else if (retry) {
		/* hid_get_class_descriptor() retries up to four short/error reads. */
		slot->fetch_retries--;
		slot->state = USBHID_REPORT_DESCRIPTOR_FETCH_PENDING;
	} else {
		slot->data = NULL;
		usbhid_report_descriptor_free(slot);
	}
	taskEXIT_CRITICAL();

	/* hid_async has completed or fenced cancellation before releasing this borrow. */
	if (release)
		kfree(req->data);
	if (fault)
		usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
	usbhid_lifecycle_kick();
}

static int usbhid_report_descriptor_queue_fetch(void)
{
	struct usbhid_report_descriptor_slot *slot = NULL;
	struct usbhid_report_descriptor_slot *empty_pending = NULL;
	u32 descriptor_serial = 0;
	u32 device_generation = 0;
	u32 async_generation;
	u16 len = 0;
	u8 dev_addr = 0;
	u8 ifnum = 0;
	u8 *data = NULL;
	u8 *release_data = NULL;
	bool allocated = false;
	bool buffer_busy = false;
	bool slot_current;
	bool cancelled;
	int ret;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_REPORT_DESCRIPTOR_SLOTS; i++) {
		struct usbhid_report_descriptor_slot *candidate =
			&usbhid_report_descriptors[i];

		if (candidate->state == USBHID_REPORT_DESCRIPTOR_FETCH_PENDING) {
			if (candidate->data) {
				if (!slot)
					slot = candidate;
				else
					buffer_busy = true;
			} else if (!empty_pending) {
				empty_pending = candidate;
			}
			continue;
		}
		if (candidate->data)
			buffer_busy = true;
	}
	if (!slot)
		slot = empty_pending;
	if (slot) {
		dev_addr = slot->dev_addr;
		ifnum = slot->ifnum;
		len = slot->len;
		device_generation = slot->generation;
		descriptor_serial = slot->serial;
		data = slot->data;
	}
	taskEXIT_CRITICAL();

	/* One exact-size buffer bounds peak heap while the physical EP0 is serial. */
	if (buffer_busy || !slot)
		return 0;
	ret = hid_async_device_epoch_snapshot(dev_addr, &async_generation);
	if (ret)
		goto invalidate;

	if (!data) {
		// int result, retries = 4;
		// memset(buf, 0, size);
		// Upstream hid_get_class_descriptor() zeroes once before four tries;
		// the slot retains that same buffer across asynchronous attempts.
		data = kzalloc(len, GFP_KERNEL);
		if (!data)
			return -ENOMEM;
		allocated = true;
	}

	taskENTER_CRITICAL();
	slot_current = slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_PENDING &&
		  slot->dev_addr == dev_addr && slot->ifnum == ifnum &&
		  slot->len == len && slot->generation == device_generation &&
		  slot->serial == descriptor_serial &&
		  (!slot->data || slot->data == data);
	if (slot_current) {
		if (!slot->data)
			slot->data = data;
		slot->state = USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE;
	}
	taskEXIT_CRITICAL();
	if (!slot_current) {
		if (allocated)
			kfree(data);
		return 1;
	}

	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// do {
	// 	result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
	// 		USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN,
	// 		(type << 8), ifnum, buf, size,
	// 		USB_CTRL_GET_TIMEOUT);
	// 	retries--;
	// } while (result < size && retries);
	// Linux issues this standard interface GET_DESCRIPTOR from task context.
	// The firmware queues the same request through its physical EP0 owner; the
	// exact heap buffer stays borrowed until completion or generation cancel.
	ret = hid_async_queue_usb_control_msg(NULL, dev_addr, async_generation,
		USB_REQ_GET_DESCRIPTOR,
		USB_RECIP_INTERFACE | USB_DIR_IN,
		(u16)HID_DT_REPORT << 8, ifnum, data, len,
		USB_CTRL_GET_TIMEOUT, usbhid_report_descriptor_fetch_complete,
		slot);
	if (!ret)
		return 1;

	taskENTER_CRITICAL();
	slot_current = slot->data == data &&
		  (slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_ACTIVE ||
		   slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED);
	cancelled = slot_current &&
		    slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_CANCELLED;
	if (slot_current) {
		if (!cancelled && ret == -EBUSY) {
			slot->state = USBHID_REPORT_DESCRIPTOR_FETCH_PENDING;
		} else {
			slot->data = NULL;
			usbhid_report_descriptor_free(slot);
			release_data = data;
		}
	}
	taskEXIT_CRITICAL();
	kfree(release_data);
	if (!cancelled && ret != -EBUSY && ret != -ENODEV)
		usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
	return ret == -EBUSY && !cancelled ? -EAGAIN : 1;

invalidate:
	taskENTER_CRITICAL();
	if (slot->state == USBHID_REPORT_DESCRIPTOR_FETCH_PENDING &&
	    slot->dev_addr == dev_addr && slot->generation == device_generation &&
	    slot->serial == descriptor_serial) {
		release_data = slot->data;
		slot->data = NULL;
		usbhid_report_descriptor_free(slot);
	}
	taskEXIT_CRITICAL();
	kfree(release_data);
	return 1;
}

static int usbhid_report_descriptor_take_ready(
		struct usbhid_probe_token *token, u8 **desc_report)
{
	struct usbhid_report_descriptor_slot *slot;

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
		/* The lifecycle task takes the exact EP0 buffer without another copy. */
		*desc_report = slot->data;
		slot->data = NULL;
		slot->state = USBHID_REPORT_DESCRIPTOR_PROBING;
		slot->len = 0;
		break;
	}
	taskEXIT_CRITICAL();

	if (!*desc_report)
		return 0;
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
// The allocation-heavy usb_string()/full descriptor helper stays disabled;
// pre-probe only needs this decode after its queued async descriptor fetch.
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
					  uint8_t index, u32 generation,
					  u32 device_generation)
{
	return hid_async_queue_string_descriptor(entry->dev.dev_addr, index,
						 entry->string_langid, generation,
						 device_generation,
						 usbhid_usb_device_descriptor_complete,
						 entry);
}

static void usbhid_usb_device_finish_preprobe(struct usbhid_usb_device *entry)
{
	bool fetch_pending = false;

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
			slot->state = USBHID_REPORT_DESCRIPTOR_FETCH_PENDING;
			fetch_pending = true;
		}
	}
	taskEXIT_CRITICAL();

	if (fetch_pending)
		usbhid_lifecycle_kick();
}

static void usbhid_usb_device_queue_next_string(struct usbhid_usb_device *entry,
						u32 generation,
						u32 device_generation)
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
							       device_generation,
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
							     generation,
							     device_generation);
			break;
		case USBHID_PREPROBE_MANUFACTURER:
			if (!entry->dev.descriptor.iManufacturer) {
				entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
				continue;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iManufacturer,
							     generation,
							     device_generation);
			break;
		case USBHID_PREPROBE_SERIAL:
			if (!entry->dev.descriptor.iSerialNumber) {
				usbhid_usb_device_finish_preprobe(entry);
				return;
			}
			ret = usbhid_usb_device_queue_string(entry,
							     entry->dev.descriptor.iSerialNumber,
							     generation,
							     device_generation);
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

static void usbhid_usb_device_descriptor_failed(
		struct usbhid_usb_device *entry,
		const struct hid_async_request *req, const char *reason)
{
	TickType_t retry_delay =
		pdMS_TO_TICKS(USBHID_DEVICE_DESCRIPTOR_RETRY_MS);
	bool is_current = false;
	bool retry = false;

	if (!retry_delay)
		retry_delay = 1;
	taskENTER_CRITICAL();
	if (entry->valid && entry->generation == req->client_generation &&
	    entry->dev.dev_addr == req->dev_addr &&
	    entry->device_desc_requested) {
		entry->device_desc_requested = false;
		is_current = true;
		if (entry->device_desc_attempts_left) {
			entry->device_desc_retry_at =
				xTaskGetTickCount() + retry_delay;
			retry = true;
		}
	}
	taskEXIT_CRITICAL();
	if (!is_current)
		return;

	usbhid_log_device_desc_error(reason, req);
	if (retry) {
		/*
		 * Linux USB core already owns a stable device descriptor here. This
		 * firmware-only refetch fills the compact usb_device shim, so retain
		 * every mounted HID across a bounded transient EP0 failure.
		 */
		usbhid_lifecycle_kick();
		return;
	}

	usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
	usbhid_usb_device_drop_pending_hid(entry);
}

static void usbhid_usb_device_descriptor_complete(const struct hid_async_request *req,
						  int status)
{
	struct usbhid_usb_device *entry = req->context;
	tusb_desc_device_t descriptor;
	bool is_current;

	if (!entry)
		return;
	taskENTER_CRITICAL();
	is_current = entry->valid &&
		     entry->generation == req->client_generation &&
		     req->dev_addr == entry->dev.dev_addr;
	taskEXIT_CRITICAL();
	if (!is_current)
		return;

	if (req->kind == HID_ASYNC_REQUEST_STRING_DESCRIPTOR) {
		usbhid_usb_device_store_string(entry, req, status);
		if (entry->preprobe_stage == USBHID_PREPROBE_DONE) {
			usbhid_usb_device_finish_preprobe(entry);
			return;
		}
		usbhid_usb_device_queue_next_string(entry, req->generation,
						    req->client_generation);
		return;
	}

	if (status < 0) {
		usbhid_usb_device_descriptor_failed(
			entry, req, req->xfer_result == XFER_RESULT_INVALID ?
				    "SUB" : "XFER");
		return;
	}

	if (req->actual_len < sizeof(descriptor)) {
		usbhid_usb_device_descriptor_failed(entry, req, "SHORT");
		return;
	}

	memcpy(&descriptor, req->data, sizeof(descriptor));
	taskENTER_CRITICAL();
	if (!entry->valid || entry->generation != req->client_generation ||
	    req->dev_addr != entry->dev.dev_addr) {
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
	entry->device_desc_attempts_left = 0;
	entry->string_langid = USBHID_STRING_LANGID;
	entry->preprobe_stage = USBHID_PREPROBE_LANGID;
	taskEXIT_CRITICAL();
	usbhid_usb_device_queue_next_string(entry, req->generation,
						    req->client_generation);
}

static void usbhid_usb_device_queue_descriptor(struct usbhid_usb_device *entry)
{
	TickType_t retry_delay =
		pdMS_TO_TICKS(USBHID_DEVICE_DESCRIPTOR_RETRY_MS);
	TickType_t now = xTaskGetTickCount();
	u32 async_generation;
	u32 generation;
	u8 dev_addr;
	int ret;

	if (!retry_delay)
		retry_delay = 1;
	/* Global mount is the exact fence after every TinyUSB config driver. */
	taskENTER_CRITICAL();
	if (!entry->valid || !entry->mount_complete || entry->have_device_desc ||
	    entry->device_desc_requested || !entry->device_desc_attempts_left ||
	    !usbhid_tick_reached(now, entry->device_desc_retry_at)) {
		taskEXIT_CRITICAL();
		return;
	}

	/*
	 * The cache publication and TinyUSB address-epoch snapshot share the same
	 * firmware critical section. The broker then verifies this expected epoch
	 * again while enqueuing, so detach cannot restamp old cache work as new.
	 */
	dev_addr = entry->dev.dev_addr;
	ret = hid_async_device_epoch_snapshot(dev_addr, &async_generation);
	if (ret) {
		taskEXIT_CRITICAL();
		usbhid_transport_fault(USBHID_FAULT_REPORT_DESCRIPTOR);
		return;
	}
	generation = entry->generation;
	entry->preprobe_stage = USBHID_PREPROBE_DEVICE_DESC;
	entry->device_desc_requested = true;
	entry->device_desc_attempts_left--;
	taskEXIT_CRITICAL();
	ret = hid_async_queue_device_descriptor(dev_addr, async_generation,
						generation,
						usbhid_usb_device_descriptor_complete,
						entry);
	if (ret) {
		taskENTER_CRITICAL();
		if (entry->valid && entry->generation == generation &&
		    entry->device_desc_requested) {
			entry->device_desc_requested = false;
			if (entry->device_desc_attempts_left <
			    USBHID_DEVICE_DESCRIPTOR_ATTEMPTS)
				entry->device_desc_attempts_left++;
			entry->device_desc_retry_at = now + retry_delay;
		}
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

/* Caller holds the firmware SMP critical section while inspecting both pools. */
static bool usbhid_usb_device_preprobe_pending_locked(
		const struct usbhid_usb_device *entry)
{
	bool waiting = false;

	if (!entry->valid || !entry->mount_complete || entry->have_device_desc ||
	    entry->device_desc_requested || !entry->device_desc_attempts_left)
		return false;

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
	if (!waiting)
		return false;

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *old = &usbhid_usb_devices[i];

		if (old != entry && old->retiring &&
		    old->dev.dev_addr == entry->dev.dev_addr)
			return false;
	}

	return true;
}

static void usbhid_usb_device_retry_preprobes(void)
{
	TickType_t now = xTaskGetTickCount();

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];
		bool due;

		taskENTER_CRITICAL();
		due = usbhid_usb_device_preprobe_pending_locked(entry) &&
		      usbhid_tick_reached(now, entry->device_desc_retry_at);
		taskEXIT_CRITICAL();
		if (due)
			usbhid_usb_device_queue_descriptor(entry);
	}
}

/* Upstream Linux: USB core owns usb_queue_reset_device() serialization. */
static bool usbhid_reset_deadline_expired(TickType_t now,
					   TickType_t deadline)
{
	return usbhid_tick_reached(now, deadline);
}

static bool usbhid_reset_parent_live_locked(void)
{
	if (!usbhid_reset->hub_addr)
		return true;

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *parent = &usbhid_usb_devices[i];

		if (parent->valid &&
		    parent->dev.dev_addr == usbhid_reset->hub_addr &&
		    parent->generation == usbhid_reset->parent_generation)
			return true;
	}

	return false;
}

static bool usbhid_reset_target_valid_locked(void)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->valid && entry->generation == usbhid_reset->generation &&
		    entry->dev.dev_addr == usbhid_reset->dev_addr)
			return true;
	}

	return false;
}

static bool usbhid_reset_target_valid(void)
{
	bool valid;

	taskENTER_CRITICAL();
	valid = usbhid_reset_target_valid_locked();
	taskEXIT_CRITICAL();
	return valid;
}

static bool usbhid_reset_target_epoch_present(void)
{
	bool present = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if ((entry->valid || entry->retiring) &&
		    entry->generation == usbhid_reset->generation &&
		    entry->dev.dev_addr == usbhid_reset->dev_addr) {
			present = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return present;
}

static bool usbhid_reset_topology_matches_locked(
		const struct usbhid_usb_device *entry)
{
	return entry->valid && entry->dev.rhport == usbhid_reset->rhport &&
	       entry->dev.hub_addr == usbhid_reset->hub_addr &&
	       entry->dev.hub_port == usbhid_reset->hub_port &&
	       usbhid_reset_parent_live_locked();
}

static bool usbhid_reset_replacement_valid_locked(void)
{
	if (!usbhid_reset->replacement_generation)
		return false;

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->mount_complete &&
		    entry->generation == usbhid_reset->replacement_generation &&
		    usbhid_reset_topology_matches_locked(entry))
			return true;
	}

	return false;
}

static struct usbhid_usb_device *usbhid_reset_fresh_epoch_locked(void)
{
	struct usbhid_usb_device *provisional = NULL;

	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->generation == usbhid_reset->generation ||
		    !usbhid_reset_topology_matches_locked(entry))
			continue;
		/* Prefer the exact post-enumeration epoch if a transient pair exists. */
		if (entry->mount_complete)
			return entry;
		if (!provisional)
			provisional = entry;
	}

	return provisional;
}

static bool usbhid_reset_topology_retiring(void)
{
	bool retiring = false;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->retiring && entry->dev.rhport == usbhid_reset->rhport &&
		    entry->dev.hub_addr == usbhid_reset->hub_addr &&
		    entry->dev.hub_port == usbhid_reset->hub_port) {
			retiring = true;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return retiring;
}

static void usbhid_reset_queue_remove(void)
{
	const hcd_event_t event = {
		.rhport = usbhid_reset->rhport,
		.event_id = HCD_EVENT_DEVICE_REMOVE,
		.connection = {
			.hub_addr = usbhid_reset->hub_addr,
			.hub_port = usbhid_reset->hub_port,
		},
	};

	/*
	 * TinyUSB application callbacks cannot wait through Linux-style reset.
	 * Lifecycle first asks TinyUSB to run its normal close path; the retiring
	 * cache epoch below is the post-hcd_device_close() fence.
	 */
	hcd_event_handler(&event, false);
}

static bool usbhid_reset_start_pending(void)
{
	bool cancelled;
	bool found = false;
	int ret;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (!entry->valid || !entry->reset_requested)
			continue;

		memset(usbhid_reset, 0, sizeof(*usbhid_reset));
		usbhid_reset->state = USBHID_RESET_WAIT_RETIRE;
		usbhid_reset->generation = entry->generation;
		usbhid_reset->dev_addr = entry->dev.dev_addr;
		usbhid_reset->rhport = entry->dev.rhport;
		usbhid_reset->hub_addr = entry->dev.hub_addr;
		usbhid_reset->hub_port = entry->dev.hub_port;
		usbhid_reset->deadline = xTaskGetTickCount() +
			pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
		usbhid_reset->gate_started = xTaskGetTickCount();
		/* Publish the intent before closing the broker on the other core. */
		usbhid_reset->gate_held = true;
		if (entry->dev.hub_addr) {
			for (size_t j = 0; j < USBHID_USB_DEVICE_SLOTS; j++) {
				const struct usbhid_usb_device *parent =
					&usbhid_usb_devices[j];

				if (parent->valid &&
				    &parent->dev == entry->dev.parent &&
				    parent->dev.dev_addr == entry->dev.hub_addr) {
					usbhid_reset->parent_generation =
						parent->generation;
					break;
				}
			}
		}
		entry->reset_requested = false;
		found = true;
		break;
	}
	taskEXIT_CRITICAL();

	if (!found)
		return false;

	ret = hid_async_control_gate_acquire();
	taskENTER_CRITICAL();
	if (ret) {
		usbhid_reset->gate_held = false;
		usbhid_reset->state = USBHID_RESET_FAILED;
	}
	taskEXIT_CRITICAL();
	if (!ret && ((usbhid_reset->hub_addr &&
		      !usbhid_reset->parent_generation) ||
		     !usbhid_reset_target_valid())) {
		taskENTER_CRITICAL();
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		taskEXIT_CRITICAL();
	}

	taskENTER_CRITICAL();
	cancelled = usbhid_reset->state == USBHID_RESET_CANCELLED;
	taskEXIT_CRITICAL();
	if (ret || cancelled)
		return true;

	async_msg("DBG: HID_RESET_Q");
	usbhid_reset_queue_remove();
	return true;
}

static void usbhid_reset_hub_complete(const struct hid_async_request *req,
				       int status)
{
	TickType_t now = xTaskGetTickCount();
	bool matched = false;

	/*
	 * A failed/timeout completion is wire-ambiguous. Never enumerate from that
	 * port state: retry SET_FEATURE(PORT_RESET) until one completion is known
	 * successful, then the host callback below enters ATTACH directly.
	 */
	(void)status;
	taskENTER_CRITICAL();
	if (req->context == usbhid_reset && usbhid_reset->hub_io_pending &&
	    req->dev_addr == usbhid_reset->hub_addr &&
	    req->hub_port == usbhid_reset->hub_port) {
		usbhid_reset->hub_io_pending = false;
		matched = true;
		if (usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE) {
			if (!usbhid_reset_parent_live_locked()) {
				usbhid_reset->state = USBHID_RESET_CANCELLED;
			} else if (usbhid_reset->attempts >=
					USBHID_RESET_HUB_ATTEMPTS ||
				   usbhid_reset_deadline_expired(
					   now, usbhid_reset->deadline)) {
				usbhid_reset->state = USBHID_RESET_FAILED;
			} else {
				usbhid_reset->state = USBHID_RESET_HUB_RETRY_WAIT;
			}
		}
	}
	taskEXIT_CRITICAL();

	if (matched)
		usbhid_lifecycle_kick();
}

void usbhid_backend_hub_reset_host_complete(uint8_t hub_addr,
					    uint8_t hub_port,
					    void *context, int status)
{
	bool matched = false;
	u8 rhport = 0;

	/*
	 * On known success TinyUSB has already made EP0 idle, and the host owner is
	 * still ahead of hub-status events. Enter enum_new_device() directly: a
	 * recursive host-queue send could block its only consumer.
	 */
	if (status)
		return;
	taskENTER_CRITICAL();
	if (context == usbhid_reset && usbhid_reset->hub_io_pending &&
	    usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE &&
	    usbhid_reset->hub_addr == hub_addr &&
	    usbhid_reset->hub_port == hub_port) {
		rhport = usbhid_reset->rhport;
		matched = true;
	}
	taskEXIT_CRITICAL();

	if (matched)
		(void)usbh_port_reenumerate_on_host(rhport, hub_addr,
						     hub_port);
}

bool usbhid_backend_hub_reenumerate_begin(uint8_t rhport,
					  uint8_t hub_addr,
					  uint8_t hub_port)
{
	bool allow = false;
	bool progress = false;
	TickType_t now = xTaskGetTickCount();

	/*
	 * Authorize the generated direct continuation before it touches topology.
	 * Host ownership makes WAIT_REENUM -> remove -> ATTACH indivisible with
	 * respect to mount callbacks, while exact parent generation rejects reuse.
	 */
	taskENTER_CRITICAL();
	if (usbhid_transport_pool && usbhid_reset->gate_held &&
	    usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE &&
	    usbhid_reset->hub_io_pending &&
	    usbhid_reset->rhport == rhport &&
	    usbhid_reset->hub_addr == hub_addr &&
	    usbhid_reset->hub_port == hub_port) {
		if (!usbhid_reset_parent_live_locked())
			usbhid_reset->state = USBHID_RESET_CANCELLED;
		else {
			struct usbhid_usb_device *fresh =
				usbhid_reset_fresh_epoch_locked();

			/* The successful wire reset invalidates every raced cache epoch. */
			usbhid_reset->replacement_generation =
				fresh ? fresh->generation : 0;
			usbhid_reset->state = USBHID_RESET_WAIT_REENUM;
			allow = true;
		}
		usbhid_reset->deadline = now +
			pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
		progress = true;
	}
	taskEXIT_CRITICAL();

	if (progress)
		usbhid_lifecycle_kick();
	return allow;
}

/*
 * Upstream Linux USB core owns enumeration and reset under the same device
 * lock. TinyUSB exposes neither owner, so this bounded callback keeps the
 * firmware's global EP0 gate closed from exact enum start through terminal.
 */
void usbhid_backend_enum_state(uint8_t rhport, uint8_t hub_addr,
			       uint8_t hub_port, bool active, bool success)
{
	TickType_t now = xTaskGetTickCount();
	bool progress = false;

	taskENTER_CRITICAL();
	if (usbhid_transport_pool && usbhid_reset->gate_held &&
	    usbhid_reset->rhport == rhport &&
	    usbhid_reset->hub_addr == hub_addr &&
	    usbhid_reset->hub_port == hub_port) {
		if (active) {
			usbhid_reset->enum_active = true;
			usbhid_reset->deadline = now +
				pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
			progress = true;
		} else if (usbhid_reset->enum_active) {
			usbhid_reset->enum_active = false;
			if (!success &&
			    usbhid_reset->state == USBHID_RESET_WAIT_REENUM)
				usbhid_reset->state = USBHID_RESET_FAILED;
			else
				usbhid_reset->deadline = now +
					pdMS_TO_TICKS(
						USBHID_RESET_PHASE_TIMEOUT_MS);
			progress = true;
		}
	}
	taskEXIT_CRITICAL();

	if (progress)
		usbhid_lifecycle_kick();
}

static void usbhid_reset_root_attach_on_host(void *context)
{
	struct usbhid_usb_device *fresh;
	TickType_t now = xTaskGetTickCount();
	u32 generation = (u32)(uintptr_t)context;
	bool attach = false;
	bool started;
	u8 rhport = 0;

	/*
	 * Run the final native-replacement check in TinyUSB's host owner. If a
	 * mount callback is already in progress, its event necessarily precedes
	 * this deferred continuation and prevents a duplicate root attach.
	 */
	taskENTER_CRITICAL();
	if (usbhid_reset->root_io_pending &&
	    usbhid_reset->generation == generation) {
		usbhid_reset->root_io_pending = false;
		if (usbhid_reset->state == USBHID_RESET_ROOT_ATTACH_ACTIVE) {
			fresh = usbhid_reset_fresh_epoch_locked();
			if (usbhid_reset_replacement_valid_locked()) {
				usbhid_reset->state = USBHID_RESET_COMPLETE;
			} else if (fresh && fresh->mount_complete) {
				usbhid_reset->replacement_generation =
					fresh->generation;
				usbhid_reset->state = USBHID_RESET_COMPLETE;
			} else if (fresh) {
				/* Raw config parsing is ahead of the exact mount fence. */
				usbhid_reset->state =
					USBHID_RESET_WAIT_CONTROL_IDLE;
			} else {
				usbhid_reset->replacement_generation = 0;
				usbhid_reset->state = USBHID_RESET_WAIT_REENUM;
				rhport = usbhid_reset->rhport;
				attach = true;
			}
			usbhid_reset->deadline = now +
				pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
		}
	}
	taskEXIT_CRITICAL();

	if (attach) {
		started = usbh_port_attach_on_host(rhport, 0, 0);
		if (!started) {
			/* Another enumeration won the final host-owner idle check. */
			taskENTER_CRITICAL();
			if (usbhid_reset->generation == generation &&
			    usbhid_reset->state == USBHID_RESET_WAIT_REENUM &&
			    !usbhid_reset->replacement_generation) {
				usbhid_reset->state =
					USBHID_RESET_WAIT_CONTROL_IDLE;
				usbhid_reset->deadline = xTaskGetTickCount() +
					pdMS_TO_TICKS(
						USBHID_RESET_PHASE_TIMEOUT_MS);
			}
			taskEXIT_CRITICAL();
		}
	}
	usbhid_lifecycle_kick();
}

static bool usbhid_reset_queue_root(void)
{
	struct usbhid_usb_device *fresh;
	TickType_t now = xTaskGetTickCount();
	u32 generation;
	bool mounted;

	taskENTER_CRITICAL();
	if (usbhid_reset->state != USBHID_RESET_WAIT_CONTROL_IDLE) {
		taskEXIT_CRITICAL();
		return true;
	}
	if (usbhid_reset_replacement_valid_locked()) {
		usbhid_reset->state = USBHID_RESET_COMPLETE;
		taskEXIT_CRITICAL();
		return true;
	}
	fresh = usbhid_reset_fresh_epoch_locked();
	if (fresh) {
		mounted = fresh->mount_complete;
		if (mounted) {
			usbhid_reset->replacement_generation = fresh->generation;
			usbhid_reset->state = USBHID_RESET_COMPLETE;
		}
		taskEXIT_CRITICAL();
		return mounted;
	}
	usbhid_reset->state = USBHID_RESET_ROOT_ATTACH_ACTIVE;
	usbhid_reset->root_io_pending = true;
	usbhid_reset->deadline = now +
		pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
	generation = usbhid_reset->generation;
	taskEXIT_CRITICAL();

	/*
	 * TinyUSB's bounded host queue carries only this tokenized function call.
	 * The callback enters enumeration directly, so the host consumer never
	 * recursively sends ATTACH to its own potentially full queue.
	 */
	usbh_defer_func(usbhid_reset_root_attach_on_host,
			(void *)(uintptr_t)generation, false);
	return true;
}

static bool usbhid_reset_queue_hub(void)
{
	struct usbhid_usb_device *fresh;
	enum usbhid_reset_state waiting_state;
	u32 async_generation;
	bool mounted;
	bool retry;
	int ret;

	taskENTER_CRITICAL();
	waiting_state = usbhid_reset->state;
	retry = waiting_state == USBHID_RESET_HUB_RETRY_WAIT;
	if (!retry && waiting_state != USBHID_RESET_WAIT_CONTROL_IDLE) {
		taskEXIT_CRITICAL();
		return true;
	}
	if (!usbhid_reset_parent_live_locked()) {
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		taskEXIT_CRITICAL();
		return true;
	}
	if (!retry) {
		fresh = usbhid_reset_fresh_epoch_locked();
		if (fresh) {
			mounted = fresh->mount_complete;
			if (mounted) {
				usbhid_reset->replacement_generation =
					fresh->generation;
				usbhid_reset->state = USBHID_RESET_COMPLETE;
			}
			taskEXIT_CRITICAL();
			return mounted;
		}
	}
	taskEXIT_CRITICAL();

	ret = hid_async_device_epoch_snapshot(usbhid_reset->hub_addr,
					      &async_generation);
	if (ret) {
		bool done;

		taskENTER_CRITICAL();
		if (usbhid_reset->state != waiting_state) {
			taskEXIT_CRITICAL();
			return true;
		}
		if (retry) {
			usbhid_reset->state = USBHID_RESET_CANCELLED;
			taskEXIT_CRITICAL();
			return true;
		}
		fresh = usbhid_reset_fresh_epoch_locked();
		mounted = fresh && fresh->mount_complete;
		if (mounted) {
			usbhid_reset->replacement_generation = fresh->generation;
			usbhid_reset->state = USBHID_RESET_COMPLETE;
		} else if (!fresh) {
			usbhid_reset->state = USBHID_RESET_CANCELLED;
		}
		done = mounted || !fresh;
		taskEXIT_CRITICAL();
		return done;
	}

	taskENTER_CRITICAL();
	/* Revalidate the exact parent epoch after the async address snapshot. */
	if (usbhid_reset->state != waiting_state) {
		taskEXIT_CRITICAL();
		return true;
	}
	if (!retry) {
		if (usbhid_reset_replacement_valid_locked()) {
			usbhid_reset->state = USBHID_RESET_COMPLETE;
			taskEXIT_CRITICAL();
			return true;
		}
		fresh = usbhid_reset_fresh_epoch_locked();
		if (fresh) {
			mounted = fresh->mount_complete;
			if (mounted) {
				usbhid_reset->replacement_generation =
					fresh->generation;
				usbhid_reset->state = USBHID_RESET_COMPLETE;
			}
			taskEXIT_CRITICAL();
			return mounted;
		}
	}
	if (!usbhid_reset_parent_live_locked()) {
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		taskEXIT_CRITICAL();
		return true;
	}
	usbhid_reset->state = USBHID_RESET_HUB_IO_ACTIVE;
	usbhid_reset->hub_io_pending = true;
	usbhid_reset->attempts++;
	taskEXIT_CRITICAL();

	ret = hid_async_queue_hub_port_reset(usbhid_reset->hub_addr,
					     async_generation,
					     usbhid_reset->hub_port,
					     usbhid_reset_hub_complete,
					     usbhid_reset);
	if (!ret)
		return true;

	/* Queue rejection cannot race a completion because no slot was published. */
	taskENTER_CRITICAL();
	configASSERT(usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE &&
		     usbhid_reset->hub_io_pending);
	usbhid_reset->hub_io_pending = false;
	usbhid_reset->attempts--;
	if (ret == -ENODEV)
		usbhid_reset->state = USBHID_RESET_CANCELLED;
	else
		usbhid_reset->state = waiting_state;
	taskEXIT_CRITICAL();
	return false;
}

static bool usbhid_reset_record_mounted_locked(
		const struct usbhid_usb_device *entry, TickType_t now)
{
	if (!usbhid_reset_topology_matches_locked(entry) ||
	    entry->generation == usbhid_reset->generation)
		return false;

	switch (usbhid_reset->state) {
	case USBHID_RESET_WAIT_RETIRE:
		/* Native re-enumeration may finish before the old epoch retires. */
		if (usbhid_reset->replacement_generation == entry->generation)
			return false;
		usbhid_reset->replacement_generation = entry->generation;
		return true;
	case USBHID_RESET_WAIT_CONTROL_IDLE:
		/* A fully mounted native replacement already performed the recovery. */
		usbhid_reset->replacement_generation = entry->generation;
		usbhid_reset->state = USBHID_RESET_COMPLETE;
		usbhid_reset->deadline = now +
			pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
		return true;
	case USBHID_RESET_ROOT_ATTACH_ACTIVE:
	case USBHID_RESET_HUB_IO_ACTIVE:
	case USBHID_RESET_HUB_RETRY_WAIT:
		/* The host-owner continuation decides whether its I/O invalidated it. */
		if (usbhid_reset->replacement_generation == entry->generation)
			return false;
		usbhid_reset->replacement_generation = entry->generation;
		return true;
	case USBHID_RESET_WAIT_REENUM:
		/* Direct host-owner attach accepts only its new post-reset epoch. */
		if (usbhid_reset->replacement_generation == entry->generation)
			return false;
		usbhid_reset->replacement_generation = entry->generation;
		usbhid_reset->state = USBHID_RESET_COMPLETE;
		usbhid_reset->deadline = now +
			pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
		return true;
	default:
		return false;
	}
}

static bool usbhid_reset_device_mounted(
		const struct usbhid_usb_device *entry)
{
	bool defer_preprobe;
	bool progress = false;
	TickType_t now = xTaskGetTickCount();

	/*
	 * tuh_mount_cb is the exact post-enum_full_complete fence for every address;
	 * the build's pinned TinyUSB compatibility patch now emits it for hubs too.
	 */
	taskENTER_CRITICAL();
	defer_preprobe = usbhid_transport_pool && usbhid_reset->gate_held;
	if (defer_preprobe)
		progress = usbhid_reset_record_mounted_locked(entry, now);
	taskEXIT_CRITICAL();

	if (progress)
		usbhid_lifecycle_kick();
	return defer_preprobe;
}

static int usbhid_reset_finish(void)
{
	enum usbhid_reset_state state;
	u32 paused_ticks;
	bool gate_held;

	taskENTER_CRITICAL();
	state = usbhid_reset->state;
	if ((state != USBHID_RESET_COMPLETE &&
	     state != USBHID_RESET_FAILED &&
	     state != USBHID_RESET_CANCELLED) ||
	    usbhid_reset->hub_io_pending || usbhid_reset->enum_active) {
		taskEXIT_CRITICAL();
		return -EAGAIN;
	}
	gate_held = usbhid_reset->gate_held;
	paused_ticks = (u32)(xTaskGetTickCount() -
			     usbhid_reset->gate_started);
	taskEXIT_CRITICAL();
	/* REMOVE and its retiring cache epoch must finish before the gate opens. */
	if (state == USBHID_RESET_COMPLETE &&
	    usbhid_reset_topology_retiring())
		return -EAGAIN;

	/* Keep firmware's gate flag visible until the broker is physically open. */
	if (gate_held)
		hid_async_control_gate_release(paused_ticks);

	if (state == USBHID_RESET_COMPLETE)
		async_msg("DBG: HID_RESET_OK");
	else if (state == USBHID_RESET_FAILED)
		async_msg("ERR: HID_RESET_FAIL");

	taskENTER_CRITICAL();
	memset(usbhid_reset, 0, sizeof(*usbhid_reset));
	taskEXIT_CRITICAL();
	return 1;
}

static int usbhid_reset_process(void)
{
	enum usbhid_reset_state state;
	TickType_t now = xTaskGetTickCount();

	taskENTER_CRITICAL();
	state = usbhid_reset->state;
	if (state != USBHID_RESET_IDLE &&
	    state != USBHID_RESET_COMPLETE &&
	    state != USBHID_RESET_FAILED &&
	    state != USBHID_RESET_CANCELLED &&
	    /* Published REMOVE is an uncancellable lifetime fence. */
	    state != USBHID_RESET_WAIT_RETIRE &&
	    !usbhid_reset->hub_io_pending &&
	    !usbhid_reset->enum_active &&
	    usbhid_reset_deadline_expired(now, usbhid_reset->deadline)) {
		usbhid_reset->state = USBHID_RESET_FAILED;
		state = USBHID_RESET_FAILED;
	}
	taskEXIT_CRITICAL();

	if (state == USBHID_RESET_IDLE)
		return usbhid_reset_start_pending() ? 1 : 0;

	if (state == USBHID_RESET_COMPLETE || state == USBHID_RESET_FAILED ||
	    state == USBHID_RESET_CANCELLED)
		return usbhid_reset_finish();

	if (state == USBHID_RESET_WAIT_RETIRE) {
		if (usbhid_reset_target_epoch_present())
			return -EAGAIN;
		taskENTER_CRITICAL();
		if (usbhid_reset_replacement_valid_locked())
			usbhid_reset->state = USBHID_RESET_COMPLETE;
		else {
			usbhid_reset->replacement_generation = 0;
			usbhid_reset->state =
				USBHID_RESET_WAIT_CONTROL_IDLE;
		}
		usbhid_reset->deadline = now + pdMS_TO_TICKS(
			usbhid_reset->hub_addr ? USBHID_RESET_HUB_TIMEOUT_MS :
			USBHID_RESET_PHASE_TIMEOUT_MS);
		taskEXIT_CRITICAL();
		return 1;
	}

	if (state == USBHID_RESET_WAIT_CONTROL_IDLE ||
	    state == USBHID_RESET_HUB_RETRY_WAIT) {
		if (!hid_async_control_gate_idle())
			return -EAGAIN;

		if (usbhid_reset->hub_addr)
			return usbhid_reset_queue_hub() ? 1 : -EAGAIN;
		if (state != USBHID_RESET_WAIT_CONTROL_IDLE)
			return -EAGAIN;

		/* Root attach is serialized with native mount callbacks by host owner. */
		return usbhid_reset_queue_root() ? 1 : -EAGAIN;
	}

	return -EAGAIN;
}

static TickType_t usbhid_reset_wait_ticks(void)
{
	TickType_t poll = pdMS_TO_TICKS(USBHID_RESET_POLL_MS);
	enum usbhid_reset_state state;

	if (!poll)
		poll = 1;
	taskENTER_CRITICAL();
	state = usbhid_reset->state;
	taskEXIT_CRITICAL();
	return state == USBHID_RESET_IDLE ? portMAX_DELAY : poll;
}

static TickType_t usbhid_preprobe_wait_ticks(void)
{
	TickType_t now = xTaskGetTickCount();
	TickType_t wait = portMAX_DELAY;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];
		TickType_t candidate;

		if (!usbhid_usb_device_preprobe_pending_locked(entry))
			continue;
		if (usbhid_tick_reached(now, entry->device_desc_retry_at)) {
			wait = 0;
			break;
		}
		candidate = entry->device_desc_retry_at - now;
		if (wait == portMAX_DELAY || candidate < wait)
			wait = candidate;
	}
	taskEXIT_CRITICAL();
	return wait;
}

static TickType_t usbhid_lifecycle_wait_ticks(void)
{
	TickType_t reset_wait = usbhid_reset_wait_ticks();

	/* Preprobe is deliberately parked while reset owns the global EP0 gate. */
	if (reset_wait != portMAX_DELAY)
		return reset_wait;
	return usbhid_preprobe_wait_ticks();
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

int usbhid_backend_queue_device_reset(struct hid_device *hid,
				      uint32_t report_revision)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	struct usb_device *dev = usbhid ? interface_to_usbdev(usbhid->intf) : NULL;
	int ret = -ENODEV;

	/* Publish only an exact cache epoch; no hid pointer survives this call. */
	taskENTER_CRITICAL();
	for (size_t i = 0; dev && usbhid_transport_pool &&
			 i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (&entry->dev != dev)
			continue;
		/* Match cancel_work_sync(&usbhid->reset_work) at close. */
		if (usbhid->transport_stopping || !usbhid->report_wanted ||
		    usbhid->report_revision != report_revision) {
			ret = -ECANCELED;
		} else if (usbhid_reset->state != USBHID_RESET_IDLE &&
		    usbhid_reset->generation == entry->generation) {
			ret = 0;
		} else if (entry->valid) {
			entry->reset_requested = true;
			ret = 0;
		}
		break;
	}
	taskEXIT_CRITICAL();

	if (!ret)
		usbhid_lifecycle_kick();
	return ret;
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

		taskENTER_CRITICAL();
		entry = usbhid_usb_device_find(token.dev_addr);
		if (entry && entry->generation != token.device_generation)
			entry = NULL;
		taskEXIT_CRITICAL();
		if (!entry ||
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
		usbhid_report_descriptor_release_pending();
		usbhid_lifecycle_drain_disconnects();
		if (usbhid_usb_device_release_retired()) {
			async_msg("ERR: HID_RETIRE_SYNC_FAIL");
			vTaskDelay(1);
			continue;
		}
		ret = usbhid_reset_process();
		if (ret > 0)
			continue;
		if (!ret) {
			usbhid_usb_device_retry_preprobes();
			ret = usbhid_report_descriptor_queue_fetch();
			if (ret < 0) {
				if (ret == -ENOMEM)
					async_msg("ERR: HID_DESC_ALLOC_FAIL");
				vTaskDelay(1);
				continue;
			}
			if (ret > 0)
				continue;
			ret = usbhid_lifecycle_process_ready_probes();
			if (ret == -ENOMEM) {
				async_msg("ERR: HID_DESC_ALLOC_FAIL");
				vTaskDelay(1);
				continue;
			}
		}
		usbhid_lifecycle_log_transport_faults();

		if (xQueueReceive(usbhid_lifecycle_queue, &event,
				  usbhid_lifecycle_wait_ticks()) == pdPASS) {
			taskENTER_CRITICAL();
			usbhid_lifecycle_wake_pending = false;
			taskEXIT_CRITICAL();
		}
	}
}

// static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
// TinyUSB mount identifies dev_addr/instance instead of Linux usb_interface;
// lifecycle fetches the report descriptor and builds the shim before add.
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
	// The lifecycle task fetched the class-declared size through async EP0 and
	// moved that exact owned buffer into Linux-shaped probe context. TinyUSB's
	// callback descriptor is ephemeral and may be omitted above
	// CFG_TUH_ENUMERATION_BUFSIZE.

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
		/* A failed device_add() need not reach the HID driver's .stop(). */
		usbhid_report_release(hid);
		hid_free_buffers(hid_to_usb_dev(hid), hid);
		usbhid_remove_slot(hid);
		async_msg(ret == -ENODEV ? "WARN: HID_IGNORED" : "ERR: HID_ADD_FAIL");
		goto fail;
	}

	/* Publish the fully built input/driver graph to sibling-interface users. */
	taskENTER_CRITICAL();
	usbhid->driver_ready = true;
	taskEXIT_CRITICAL();

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
	struct hid_device *hid = NULL;
	bool disconnect_published = false;
	bool pinned = false;
	int ret;

	/*
	 * Linux USB core retains intf through disconnect(). TinyUSB supplies only
	 * an address pair, so publish disconnect and acquire the firmware HID lease
	 * atomically before callback context may hand teardown to lifecycle.
	 */
	taskENTER_CRITICAL();
	hid = usbhid_lookup_locked(dev_addr, instance, 0);
	if (hid) {
		struct usbhid_device *usbhid = hid->driver_data;

		if (!usbhid->disconnect_queued) {
			usbhid->disconnect_queued = true;
			disconnect_published = true;
			if (!usbhid->transport_stopping) {
				usbhid->io_pending++;
				pinned = true;
			}
		}
	}
	taskEXIT_CRITICAL();

	if (pinned)
		usbhid_report_unplug(hid);

	ret = hid_async_cancel_device(dev_addr, instance);

	if (ret)
		usbhid_transport_fault(USBHID_FAULT_ASYNC_CANCEL);
	if (pinned)
		usbhid_io_put(hid);
	/*
	 * Upstream Linux runs usbhid_disconnect() from USB core process
	 * context and can call hid_destroy_device() directly. TinyUSB calls
	 * this hook from unmount callback context, so driver remove is handed
	 * to a firmware task before any Linux-style flush/cancel waits run.
	 */
	if (disconnect_published)
		usbhid_lifecycle_kick();
}

void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      uint8_t const *desc_report, uint16_t desc_len)
{
	struct usbhid_usb_device *entry = usbhid_usb_device_prepare(dev_addr);
	int ret;

	/*
	 * The SHA-pinned build-local TinyUSB HID class deliberately skips its
	 * duplicate enumeration-buffer report fetch and always mounts with NULL
	 * descriptor data. This callback records only interface metadata; lifecycle
	 * owns the exact-size, four-attempt Linux-shaped EP0 fetch before probe.
	 */
	(void)desc_report;
	(void)desc_len;
	if (!entry) {
		return;
	}

	ret = usbhid_usb_device_store_pending_probe(entry, instance);
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

	/*
	 * tuh_mount_cb is the exact enum_full_complete fence. Publish it under the
	 * same SMP critical section as reset_requested because both are bitfields
	 * in one cache byte.
	 */
	taskENTER_CRITICAL();
	entry->mount_complete = true;
	taskEXIT_CRITICAL();

	/*
	 * Previous port:
	 * if (usbhid_reset_device_mounted(entry) ||
	 *     usbhid_usb_device_old_epoch_retiring(entry))
	 * 	usbhid_lifecycle_kick();
	 * else if (usbhid_usb_device_has_waiting_hid(entry))
	 * 	usbhid_usb_device_queue_descriptor(entry);
	 *
	 * TinyUSB invokes this from its host callback owner. Publish the completed
	 * mount/reset state and wake lifecycle, but do not continue pre-probe by
	 * submitting the device-descriptor request from the callback. The existing
	 * lifecycle retry scan owns that same guarded submission in task context.
	 */
	(void)usbhid_reset_device_mounted(entry);
	usbhid_lifecycle_kick();
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
		struct hid_device *hid = NULL;
		bool pinned = false;

		taskENTER_CRITICAL();
		hid = usbhid_devices[i];
		if (hid) {
			struct usbhid_device *usbhid = hid->driver_data;

			if (interface_to_usbdev(usbhid->intf) != &retired->dev) {
				hid = NULL;
			} else {
				usbhid->disconnect_queued = true;
				if (!usbhid->transport_stopping) {
					usbhid->io_pending++;
					pinned = true;
				}
			}
		}
		taskEXIT_CRITICAL();

		if (pinned) {
			usbhid_report_unplug(hid);
			usbhid_io_put(hid);
		}
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
	struct hid_device *hid;
	uint8_t protocol_mode = HID_PROTOCOL_REPORT;
	bool parse = xfer_result == XFER_RESULT_SUCCESS;
	int ret;

	/*
	 * Pin the exact interface generation before leaving the callback lookup
	 * fence. A stale completion must not bind to a fast-reused address/instance.
	 */
	taskENTER_CRITICAL();
	hid = usbhid_lookup_locked(dev_addr, instance, generation);
	if (hid) {
		struct usbhid_device *usbhid = hid->driver_data;

		if (usbhid->transport_stopping || usbhid->disconnect_queued)
			hid = NULL;
		else
			usbhid->io_pending++;
	}
	taskEXIT_CRITICAL();
	if (!hid)
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
	usbhid_io_put(hid);
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

/*
 * Traverse the supplied list of reports and find the longest
 */
static void hid_find_max_report(struct hid_device *hid, unsigned int type,
		unsigned int *max)
{
	struct hid_report *report;
	unsigned int size;

	list_for_each_entry(report, &hid->report_enum[type].report_list, list) {
		size = ((report->size - 1) >> 3) + 1 + hid->report_enum[type].numbered;
		if (*max < size)
			*max = size;
	}
}

static int hid_alloc_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_host_interface *interface = usbhid->intf->cur_altsetting;
	unsigned int alloc_size = HID_MIN_BUFFER_SIZE;
	unsigned int maxpacket = 0;

	for (u8 i = 0; i < interface->desc.bNumEndpoints; i++) {
		const struct usb_endpoint_descriptor *ep =
			&interface->endpoint[i].desc;

		if (ep->bEndpointAddress ==
			usbhid->usb_altsetting.interrupt_in_endpoint) {
			maxpacket = le16_to_cpu(ep->wMaxPacketSize) & 0x7ffu;
			break;
		}
	}

	/*
	 * The pinned PIO HCD copies a complete received packet before checking the
	 * logical transfer remainder. Keep the Linux length unchanged, but provide
	 * backing space through the end of that final packet.
	 */
	if (maxpacket) {
		unsigned int rounded =
			((usbhid->report_bufsize + maxpacket - 1u) /
			 maxpacket) * maxpacket;

		if (alloc_size < rounded)
			alloc_size = rounded;
	} else if (alloc_size < usbhid->report_bufsize) {
		alloc_size = usbhid->report_bufsize;
	}

	// usbhid->inbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
	// 				    &usbhid->inbuf_dma);
	// usbhid->outbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
	// 				     &usbhid->outbuf_dma);
	// usbhid->cr = kmalloc_obj(*usbhid->cr);
	// usbhid->ctrlbuf = usb_alloc_coherent(dev, usbhid->bufsize, GFP_KERNEL,
	// 				      &usbhid->ctrlbuf_dma);
	// OUT and control payloads have exact per-request ownership in hid_async.
	// This port also sizes the reusable per-interface IN backing for INPUT only,
	// rounded above for PIO packet writes, instead of charging a large unrelated
	// FEATURE/OUTPUT report to every interrupt receive.
	// TinyUSB/PIO accepts ordinary SRAM rather than Linux DMA-coherent memory.
	// Allocate in task context and retain the upstream per-interface owner.
	(void)dev;
	usbhid->inbuf = kmalloc(alloc_size, GFP_KERNEL);
	// if (!usbhid->inbuf || !usbhid->outbuf || !usbhid->cr ||
	// 		!usbhid->ctrlbuf)
	// 	return -1;
	// The other three owners are deliberately absent for the reason above.
	if (!usbhid->inbuf)
		return -1;

	return 0;
}

static void hid_free_buffers(struct usb_device *dev, struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	// usb_free_coherent(dev, usbhid->bufsize, usbhid->inbuf,
	// 			 usbhid->inbuf_dma);
	// usb_free_coherent(dev, usbhid->bufsize, usbhid->outbuf,
	// 			 usbhid->outbuf_dma);
	// kfree(usbhid->cr);
	// usb_free_coherent(dev, usbhid->bufsize, usbhid->ctrlbuf,
	// 			 usbhid->ctrlbuf_dma);
	// The TinyUSB replacement above owns an ordinary heap buffer. Either start
	// failed before publication, or the report stop/fence path has revoked every
	// HCD and parser borrower here. hid_async independently releases its
	// per-request OUT/control payloads.
	(void)dev;
	kfree(usbhid->inbuf);
	usbhid->inbuf = NULL;
}

static int usbhid_start(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usbhid_device *usbhid = hid->driver_data;
	unsigned int insize = 0;
	int ret = 0;

	if (usbhid_report_is_stopping(hid))
		return -ENODEV;

	usbhid->bufsize = HID_MIN_BUFFER_SIZE;
	hid_find_max_report(hid, HID_INPUT_REPORT, &usbhid->bufsize);
	hid_find_max_report(hid, HID_OUTPUT_REPORT, &usbhid->bufsize);
	hid_find_max_report(hid, HID_FEATURE_REPORT, &usbhid->bufsize);

	if (usbhid->bufsize > HID_MAX_BUFFER_SIZE)
		usbhid->bufsize = HID_MAX_BUFFER_SIZE;

	hid_find_max_report(hid, HID_INPUT_REPORT, &insize);

	if (insize > HID_MAX_BUFFER_SIZE)
		insize = HID_MAX_BUFFER_SIZE;
	usbhid->report_bufsize = (u16)insize;

	if (hid_alloc_buffers(dev, hid)) {
		ret = -ENOMEM;
		goto fail;
	}

	/*
	 * Firmware allocates no Linux URBs here. TinyUSB interrupt IN starts here
	 * only for ALWAYS_POLL, matching upstream hid_start_in(); boot-keyboard LED
	 * reset uses the same queued output/control routing as upstream usbhid.
	 */
	if (hid->quirks & HID_QUIRK_ALWAYS_POLL)
		ret = usbhid_report_start(hid);
	if (ret)
		goto fail;

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

fail:
	// usb_free_urb(usbhid->urbin);
	// usb_free_urb(usbhid->urbout);
	// usb_free_urb(usbhid->urbctrl);
	// usbhid->urbin = NULL;
	// usbhid->urbout = NULL;
	// usbhid->urbctrl = NULL;
	// TinyUSB owns endpoint objects rather than allocating Linux URBs.
	hid_free_buffers(dev, hid);
	return ret;
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
	// usb_free_urb(usbhid->urbin);
	// usb_free_urb(usbhid->urbctrl);
	// usb_free_urb(usbhid->urbout);
	// usbhid->urbin = NULL;
	// usbhid->urbctrl = NULL;
	// usbhid->urbout = NULL;
	// TinyUSB endpoint ownership was synchronously revoked above.
	hid_free_buffers(hid_to_usb_dev(hid), hid);
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

static int usbhid_get_raw_report(struct hid_device *hid,
		unsigned char report_number, __u8 *buf, size_t count,
		unsigned char report_type)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(hid);
	struct usb_interface *intf = usbhid->intf;
	struct usb_host_interface *interface = intf->cur_altsetting;
	int skipped_report_id = 0;
	int ret;

	/* Byte 0 is the report number. Report data starts at byte 1.*/
	buf[0] = report_number;
	if (report_number == 0x0) {
		/* Offset the return buffer by 1, so that the report ID
		   will remain in byte 0. */
		buf++;
		count--;
		skipped_report_id = 1;
	}
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
		HID_REQ_GET_REPORT,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		((report_type + 1) << 8) | report_number,
		interface->desc.bInterfaceNumber, buf, count,
		USB_CTRL_SET_TIMEOUT);

	/* count also the report id */
	if (ret > 0 && skipped_report_id)
		ret++;

	return ret;
}

static int usbhid_set_raw_report(struct hid_device *hid, unsigned int reportnum,
				 __u8 *buf, size_t count, unsigned char rtype)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(hid);
	struct usb_interface *intf = usbhid->intf;
	struct usb_host_interface *interface = intf->cur_altsetting;
	int ret, skipped_report_id = 0;

	/* Byte 0 is the report number. Report data starts at byte 1.*/
	if ((rtype == HID_OUTPUT_REPORT) &&
	    (hid->quirks & HID_QUIRK_SKIP_OUTPUT_REPORT_ID))
		buf[0] = 0;
	else
		buf[0] = reportnum;

	if (buf[0] == 0x0) {
		/* Don't send the Report ID */
		buf++;
		count--;
		skipped_report_id = 1;
	}

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			HID_REQ_SET_REPORT,
			USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			((rtype + 1) << 8) | reportnum,
			interface->desc.bInterfaceNumber, buf, count,
			USB_CTRL_SET_TIMEOUT);
	/* count also the report id, if this was a numbered report. */
	if (ret > 0 && skipped_report_id)
		ret++;

	return ret;
}

static int usbhid_output_report(struct hid_device *hid, __u8 *buf, size_t count)
{
	// struct usbhid_device *usbhid = hid->driver_data;
	// struct usb_device *dev = hid_to_usb_dev(hid);
	// Firmware disconnect can run on the other core; acquire the HID lease
	// before dereferencing its embedded interface and endpoint snapshot.
	struct usbhid_device *usbhid;
	struct usb_device *dev;
	u8 ep_addr;
	int actual_length, skipped_report_id = 0, ret;

	if (!usbhid_io_get(hid))
		return -ENODEV;
	usbhid = hid->driver_data;
	dev = hid_to_usb_dev(hid);

	// if (!usbhid->urbout)
	// 	return -ENOSYS;
	// TinyUSB owns no persistent output URB; the parsed interface snapshot is
	// the equivalent proof that this HID has an interrupt-OUT transport.
	if (!usbhid->usb_altsetting.has_interrupt_out ||
	    !usbhid->usb_altsetting.interrupt_out_endpoint) {
		ret = -ENOSYS;
		goto out;
	}
	ep_addr = usbhid->usb_altsetting.interrupt_out_endpoint;

	if (buf[0] == 0x0) {
		/* Don't send the Report ID */
		buf++;
		count--;
		skipped_report_id = 1;
	}

	// ret = usb_interrupt_msg(dev, usbhid->urbout->pipe,
	// 				buf, count, &actual_length,
	// 				USB_CTRL_SET_TIMEOUT);
	// Build the same interrupt-OUT pipe from the TinyUSB-owned endpoint; the
	// generic bridge below preserves synchronous upstream return semantics.
	ret = usb_interrupt_msg(dev, usb_sndintpipe(dev, ep_addr), buf, count,
				&actual_length, USB_CTRL_SET_TIMEOUT);
	/* return the number of bytes transferred */
	if (ret == 0) {
		ret = actual_length;
		/* count also the report id */
		if (skipped_report_id)
			ret++;
	}

	// return ret;
	// Release the firmware HID lease after the synchronous generic request.
out:
	usbhid_io_put(hid);
	return ret;
}

static int usbhid_parse(struct hid_device *hid)
{
	struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	struct usb_host_interface *interface = intf->cur_altsetting;
	// struct usb_device *dev = interface_to_usbdev(intf);
	// The lifecycle task completed the upstream-shaped asynchronous descriptor
	// request before entering this parser callback.
	struct usbhid_device *usbhid = hid->driver_data;
	struct hid_descriptor hdesc_storage;
	struct hid_descriptor *hdesc;
	struct hid_class_descriptor *hcdesc;
	__u8 fixed_opt_descriptors_size;
	u32 quirks = 0;
	unsigned long transport_quirks = 0;
	unsigned int rsize = 0;
	// char *rdesc;
	// The lifecycle task owns the asynchronously fetched descriptor in usbhid.
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
	 * Linux USB core requests exactly the class-declared size. The lifecycle
	 * EP0 bridge owns that request here, so reject a buffer from a different
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
	// TinyUSB performs enumeration SET_IDLE before mount; the lifecycle task
	// already completed the async descriptor request before this parser call.
	// ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
	//				  HID_DT_REPORT, rdesc, rsize);
	// if (ret < 0) {
	// 	dbg_hid("reading report descriptor failed\n");
	// 	kfree(rdesc);
	// 	goto err;
	// }
	// The lifecycle EP0 bridge supplies the owned report descriptor here so
	// hid_add_device() keeps the upstream parse flow.
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

	/* The blocked caller keeps the direct TinyUSB buffer alive through retire. */
	sync->actual_len = req->actual_len;
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

/*
 * Upstream USB configuration storage keeps sibling usb_interface objects alive
 * for the physical device lifetime. Firmware embeds each shim in its HID
 * transport object, so a cross-interface caller must pin that object while it
 * follows intfdata and input-list pointers.
 */
struct hid_device *usbhid_ifnum_io_get(const struct usb_device *dev,
					       unsigned int ifnum)
{
	struct hid_device *found = NULL;

	taskENTER_CRITICAL();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && usbhid->driver_ready &&
		    !usbhid->transport_stopping &&
		    interface_to_usbdev(usbhid->intf) == dev &&
		    usbhid->intf->cur_altsetting->desc.bInterfaceNumber == ifnum) {
			usbhid->io_pending++;
			found = hid;
			break;
		}
	}
	taskEXIT_CRITICAL();
	return found;
}

void usbhid_ifnum_io_put(struct hid_device *hid)
{
	usbhid_io_put(hid);
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
	struct usbhid_control_input *input = context;
	u32 serial = input->async_serial;

	if (status < 0 && status != -ENODEV)
		async_msg("ERR: HID_CTRL_PARSE_FAIL");
	/*
	 * TinyUSB adapter: the async executor retains this request slot across
	 * Linux-shaped control completion until the parser consumer releases it.
	 * Release while io_pending still keeps hid and its transport state alive.
	 */
	hid_async_control_report_release(hid, serial);
	kfree(input);
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
	/*
	 * TinyUSB adapter: the completion-owned input buffer is also the direct EP0
	 * destination and remains owned through hid_ctrl(). Pin this executor slot
	 * before publishing its payload to the parser lane.
	 */
	ret = hid_async_control_report_hold(req);
	if (ret) {
		if (ret != -ENODEV)
			async_msg("ERR: HID_CTRL_HOLD_FAIL");
		kfree(input);
		usbhid_io_put(req->hid);
		return;
	}
	input->async_serial = req->serial;
	ret = usbhid_control_report_submit(req->hid, req->report->type,
					   input->data, input->bufsize, len,
					   input->parser_owner,
					   usbhid_control_report_done,
					   input);
	if (ret) {
		if (ret != -ENODEV)
			async_msg("ERR: HID_CTRL_PARSE_Q_FAIL");
		hid_async_control_report_release(req->hid,
						 input->async_serial);
		kfree(input);
		usbhid_io_put(req->hid);
	}
}

// static void usbhid_submit_report(struct hid_device *hid, struct hid_report *report, unsigned char dir)
// {
// 	struct usbhid_device *usbhid = hid->driver_data;
// 	unsigned long flags;
//
// 	spin_lock_irqsave(&usbhid->lock, flags);
// 	__usbhid_submit_report(hid, report, dir);
// 	spin_unlock_irqrestore(&usbhid->lock, flags);
// }
/*
 * TinyUSB has neither Linux's URB FIFO nor usbhid->lock. The async executor is
 * the serialized transport owner instead; return its enqueue status and carry
 * the completion/context required by the firmware parser handoff.
 */
static int usbhid_queue_report(struct hid_device *hid,
			       struct hid_report *report,
			       enum hid_class_request reqtype,
			       u8 *data, u16 data_size,
			       hid_async_complete_t complete, void *context)
{
	return hid_async_queue_report(hid, report, reqtype, data, data_size,
				      complete, context);
}

static void usbhid_request(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype)
{
	struct usbhid_control_input *input = NULL;
	struct usbhid_device *usbhid;
	struct usb_device *dev;
	u32 maxpacket;
	u32 transfer_size;
	u32 bufsize;
	int ret;

	if (reqtype == HID_REQ_GET_REPORT && (hid->quirks & HID_QUIRK_NOGET))
		return;
	if (!usbhid_io_get(hid))
		return;
	usbhid = hid->driver_data;
	if (reqtype == HID_REQ_GET_REPORT) {
		bufsize = hid_report_len(report);
		// len += (len == 0); /* Don't allow 0-length reports */
		// len = round_up(len, maxpacket);
		// if (len > usbhid->bufsize)
		// 	len = usbhid->bufsize;
		// The port allocates each GET buffer to its exact rounded transfer instead
		// of a persistent ctrlbuf; retain Linux's per-device usbhid->bufsize cap.
		dev = hid_to_usb_dev(hid);
		maxpacket = dev->descriptor.bMaxPacketSize0;
		if (!maxpacket)
			maxpacket = 8;
		transfer_size = bufsize + !bufsize;
		transfer_size = DIV_ROUND_UP(transfer_size, maxpacket) * maxpacket;
		transfer_size = min_t(u32, transfer_size, usbhid->bufsize);
		bufsize += 7 + (report->id == 0);
		bufsize = max(bufsize, transfer_size);
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

	// switch (reqtype) {
	// case HID_REQ_GET_REPORT:
	// 	usbhid_submit_report(hid, rep, USB_DIR_IN);
	// 	break;
	// case HID_REQ_SET_REPORT:
	// 	usbhid_submit_report(hid, rep, USB_DIR_OUT);
	// 	break;
	// }
	/*
	 * TinyUSB adapter: the queue boundary above replaces both upstream direction
	 * branches and carries their completion owner. GET_REPORT parsing releases
	 * io_pending only after its report-lane consumer completes, so a following
	 * hid_hw_wait() cannot observe a false-idle transport/parser handoff.
	 */
	ret = usbhid_queue_report(hid, report, reqtype,
				  input ? input->data : NULL,
				  input ? input->bufsize : 0,
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

static int hid_set_idle(struct usb_device *dev, int ifnum, int report, int idle)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_IDLE, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(idle << 8) | report, ifnum, NULL, 0, USB_CTRL_SET_TIMEOUT);
}

static int usbhid_raw_request(struct hid_device *hid, unsigned char reportnum,
			      __u8 *buf, size_t len, unsigned char rtype,
			      int reqtype)
{
	int ret;

	if (!usbhid_io_get(hid))
		return -ENODEV;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		// return usbhid_get_raw_report(hid, reportnum, buf, len, rtype);
		// Firmware disconnect waits for the HID lease acquired above, so keep
		// the upstream result until that lease can be released below.
		ret = usbhid_get_raw_report(hid, reportnum, buf, len, rtype);
		break;
	case HID_REQ_SET_REPORT:
		// return usbhid_set_raw_report(hid, reportnum, buf, len, rtype);
		// Firmware disconnect waits for the HID lease acquired above, so keep
		// the upstream result until that lease can be released below.
		ret = usbhid_set_raw_report(hid, reportnum, buf, len, rtype);
		break;
	default:
		// return -EIO;
		// Release the firmware HID lease on the common return path.
		ret = -EIO;
		break;
	}

	usbhid_io_put(hid);
	return ret;
}

static int usbhid_idle(struct hid_device *hid, int report, int idle, int reqtype)
{
	// struct usb_device *dev = hid_to_usb_dev(hid);
	// struct usb_interface *intf = to_usb_interface(hid->dev.parent);
	// struct usb_host_interface *interface = intf->cur_altsetting;
	// int ifnum = interface->desc.bInterfaceNumber;
	// Firmware disconnect runs in another task. Delay dereferencing the HID
	// interface until its local transport lease is held.
	struct usb_device *dev;
	struct usb_interface *intf;
	struct usb_host_interface *interface;
	int ifnum;
	int ret;

	if (reqtype != HID_REQ_SET_IDLE)
		return -EINVAL;
	if (!usbhid_io_get(hid))
		return -ENODEV;
	dev = hid_to_usb_dev(hid);
	intf = to_usb_interface(hid->dev.parent);
	interface = intf->cur_altsetting;
	ifnum = interface->desc.bInterfaceNumber;

	// return hid_set_idle(dev, ifnum, report, idle);
	// The HID lease above lets firmware disconnect wait for this synchronous
	// USB-core contract while the generic bridge owns the physical EP0 request.
	ret = hid_set_idle(dev, ifnum, report, idle);
	usbhid_io_put(hid);
	return ret;
}

int usb_control_msg(struct usb_device *dev, unsigned int pipe,
		    u8 request, u8 requesttype, u16 value, u16 index,
		    void *data, u16 size, int timeout)
{
	struct usbhid_sync_request sync = { 0 };
	struct usbhid_usb_device *physical_owner;
	struct hid_device *hid_owner;
	u32 generation;
	u8 dev_addr;
	int ret;

	/* Match usb_start_wait_urb()'s bound for non-killable synchronous calls. */
	if (timeout <= 0 || timeout > USB_MAX_SYNCHRONOUS_TIMEOUT)
		timeout = USB_MAX_SYNCHRONOUS_TIMEOUT;
	if (!dev || usb_pipedevice(pipe) != dev->dev_addr ||
	    !usb_pipecontrol(pipe) || usb_pipeendpoint(pipe) ||
	    !!usb_pipein(pipe) != !!(requesttype & USB_DIR_IN) ||
	    (size && !data))
		return -EINVAL;
	if (!hid_async_sync_call_allowed())
		return -EAGAIN;
	sync.task = xTaskGetCurrentTaskHandle();

	ret = usbhid_usb_device_io_get(dev, &physical_owner, &dev_addr,
				       &generation);
	if (ret)
		return ret;
	ret = usbhid_usb_device_control_owner_get(dev, requesttype, index,
						 &hid_owner);
	if (ret)
		goto out_physical;
	/*
	 * usb_control_msg() blocks in Linux USB core. The metadata-slot executor
	 * owns EP0 here while the sleeping caller keeps its direct buffer alive;
	 * completion only wakes this task and unplug returns -ENODEV.
	 */
	ret = hid_async_queue_usb_control_msg(hid_owner, dev_addr, generation,
					      request, requesttype, value,
					      index, data, size, timeout,
					      usbhid_sync_complete, &sync);
	ret = usbhid_sync_wait(&sync, ret);
	if (!ret)
		ret = (int)sync.actual_len;
	if (hid_owner)
		usbhid_io_put(hid_owner);
out_physical:
	usbhid_usb_device_io_put(physical_owner);
	return ret;
}

int usb_interrupt_msg(struct usb_device *dev, unsigned int pipe,
		      void *data, int len, int *actual_length, int timeout)
{
	struct usbhid_sync_request sync = { 0 };
	struct usbhid_usb_device *physical_owner;
	struct hid_device *hid_owner;
	u32 generation;
	u8 dev_addr;
	u8 ep_addr;
	int ret;

	// return usb_bulk_msg(usb_dev, pipe, data, len, actual_length, timeout);
	// Linux bulk/URB core is not ported. This metadata-slot bridge implements the
	// audited HID interrupt-OUT subset while preserving the synchronous result.
	if (actual_length)
		*actual_length = 0;
	/* Match usb_start_wait_urb()'s bound for non-killable synchronous calls. */
	if (timeout <= 0 || timeout > USB_MAX_SYNCHRONOUS_TIMEOUT)
		timeout = USB_MAX_SYNCHRONOUS_TIMEOUT;
	if (!dev || usb_pipedevice(pipe) != dev->dev_addr ||
	    !usb_pipeint(pipe) || len < 0 ||
	    (len && !data))
		return -EINVAL;
	if (usb_pipein(pipe))
		return -ENOSYS;
	if ((unsigned int)len > UINT16_MAX)
		return -EMSGSIZE;
	ep_addr = (u8)usb_pipeendpoint(pipe);
	if (!ep_addr)
		return -EINVAL;
	if (!hid_async_sync_call_allowed())
		return -EAGAIN;
	sync.task = xTaskGetCurrentTaskHandle();

	ret = usbhid_usb_device_io_get(dev, &physical_owner, &dev_addr,
				       &generation);
	if (ret)
		return ret;
	// ep = usb_pipe_endpoint(usb_dev, pipe);
	// if (!ep || len < 0)
	// 	return -EINVAL;
	// TinyUSB stores the endpoint descriptor in each live HID interface shim;
	// the device lease keeps this exact physical cache epoch from being reused.
	// The shared upstream len < 0 condition was already checked above.
	ret = usbhid_usb_device_interrupt_owner_get(dev, ep_addr, &hid_owner);
	if (ret)
		goto out;

	/*
	 * Linux usb_interrupt_msg() waits on an URB. TinyUSB interrupt-IN remains
	 * continuously owned by usbhid_report; this generic task bridge therefore
	 * exposes only endpoint-addressed interrupt-OUT without stealing that lane.
	 */
	ret = hid_async_queue_usb_interrupt_out(hid_owner, dev_addr, generation,
						ep_addr, data, (u16)len,
						timeout, usbhid_sync_complete,
						&sync);
	ret = usbhid_sync_wait(&sync, ret);
	if (actual_length)
		*actual_length = (int)sync.actual_len;
	usbhid_io_put(hid_owner);
out:
	usbhid_usb_device_io_put(physical_owner);
	return ret;
}

/*
 * usb_alloc_urb()/usb_submit_urb()/usb_kill_urb() remain deliberately absent.
 * A future bridge needs per-URB identity plus synchronous kill and callback
 * resubmit ordering; neither arbitrary urb->context nor device-wide cancel is
 * a valid substitute. The upstream-shaped declarations remain disabled in
 * linux/include/linux/usb.h beside that missing-subsystem reason.
 */

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
