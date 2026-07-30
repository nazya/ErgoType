/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "tusb.h"
#include "host/hcd.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "hid_transport_sync.h"
#include "evdev.h"
#include "usbhid.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"
#include "stdio_tusb_cdc.h"
#include "linux/include/linux/delay.h"
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
#define USBHID_LIFECYCLE_NOTIFY_INDEX 1u
#define USBHID_LIFECYCLE_NOTIFY_WAKE (1u << 0)
#define USBHID_STRING_LANGID 0x0409u
#define USBHID_USB_DEVICE_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
/* One bounded spare lets a fast replug coexist with one retiring cache entry. */
#define USBHID_USB_DEVICE_SLOTS (USBHID_USB_DEVICE_MAX + 1)
#define USBHID_USB_MAXCHILD 31
/* TinyUSB cannot mount more HID interfaces than its class-instance pool. */
#define USBHID_PROBE_SLOTS HID_HOST_MAX_DEVICES
#define USBHID_DEVICE_DESCRIPTOR_ATTEMPTS 4u
#define USBHID_DEVICE_DESCRIPTOR_RETRY_MS 100u
#define USBHID_PREPROBE_BUFFER_SIZE 256u
#define USBHID_STRING_DESCRIPTOR_SIZE 255u
#define USBHID_RESET_HUB_ATTEMPTS 3u
#define USBHID_RESET_PHASE_TIMEOUT_MS 6000u
#define USBHID_RESET_HUB_TIMEOUT_MS \
	(USB_CTRL_SET_TIMEOUT * USBHID_RESET_HUB_ATTEMPTS + 1000u)

_Static_assert(HID_MAX_BUFFER_SIZE + 8u <= UINT16_MAX,
	       "HID control buffer size must fit TinyUSB's 16-bit length");
_Static_assert(HID_MAX_DESCRIPTOR_SIZE <= UINT16_MAX,
	       "HID report descriptor size must fit TinyUSB's 16-bit length");
_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES >
	       USBHID_LIFECYCLE_NOTIFY_INDEX,
	       "HID lifecycle requires a dedicated task notification index");
_Static_assert(CFG_TUH_HID <= 8,
	       "queued reset ownership uses one bit per TinyUSB HID instance");

/*
 * SHA-pinned build-local TinyUSB host-owner extensions. They enter the same
 * enum_new_device() path without recursively sending to the host queue.
 */
bool usbh_port_attach_on_host(uint8_t rhport, uint8_t hub_addr,
			      uint8_t hub_port);
void usbh_port_replace_ready_on_host(uint8_t rhport, uint8_t hub_addr,
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
static _Atomic(TaskHandle_t) usbhid_lifecycle_task_handle;
static struct work_struct *usbhid_lifecycle_work_head;
static struct work_struct *usbhid_lifecycle_work_tail;

struct usbhid_sync_request {
	TaskHandle_t task;
	size_t actual_len;
	int status;
	volatile bool done;
};

/*
 * Linux embeds 256 control and 256 output FIFO records in every usbhid_device
 * (the OUT ring uses only 64). That fixed layout costs about 5 KiB per HID on
 * 32-bit targets. Preserve its logical per-interface queues dynamically, but
 * cap their combined live allocation so one noisy device cannot consume the
 * firmware heap. Charge a conservative allocator header plus alignment for
 * every node and deferred GET buffer; the upstream count limits still apply
 * inside this byte cap.
 */
#define USBHID_REPORT_QUEUE_MEMORY_MAX 4096u
#define USBHID_REPORT_ALLOC_OVERHEAD 16u
#define USBHID_REPORT_ALIGN_MASK (portBYTE_ALIGNMENT - 1u)
#define USBHID_REPORT_ALIGN_UP(size) \
	(((size) + USBHID_REPORT_ALIGN_MASK) & \
	 ~(size_t)USBHID_REPORT_ALIGN_MASK)
#define USBHID_REPORT_HEAP_CHARGE(size) \
	USBHID_REPORT_ALIGN_UP((size) + USBHID_REPORT_ALLOC_OVERHEAD)

_Static_assert((portBYTE_ALIGNMENT & USBHID_REPORT_ALIGN_MASK) == 0,
	       "FreeRTOS heap alignment must be a power of two");

enum usbhid_report_lane {
	USBHID_REPORT_LANE_CTRL,
	USBHID_REPORT_LANE_OUT,
};

enum usbhid_report_request_state {
	USBHID_REPORT_REQUEST_QUEUED,
	USBHID_REPORT_REQUEST_SUBMITTING,
	USBHID_REPORT_REQUEST_ACTIVE,
	USBHID_REPORT_REQUEST_PARSING,
	USBHID_REPORT_REQUEST_PARKED,
};

struct usbhid_report_request {
	struct usbhid_report_request *next;
	struct hid_async_admission_node admission;
	struct hid_device *hid;
	struct hid_report *report;
	struct usbhid_control_input *input;
	TaskHandle_t parser_owner;
	u32 device_generation;
	u16 memory_charge;
	u16 wire_len;
	u8 reqtype;
	u8 lane;
	u8 state : 3;
	u8 cancel_requested : 1;
	u8 data[];
};

struct usbhid_io_waiter {
	struct usbhid_io_waiter *next;
	TaskHandle_t task;
	bool teardown;
};

/* EP0-rounded GET buffer owned from .request enqueue through parser completion. */
struct usbhid_control_input {
	struct usbhid_report_request *request;
	/* Stable async slot identity held until parser completion. */
	u32 async_serial;
	u16 bufsize;
	u16 len;
	u8 data[];
};

struct usbhid_probe_slot {
	u8 dev_addr;
	u8 instance;
	u8 ifnum;
	u32 generation;
	/* Host callbacks own host_serial; lifecycle owns handled_serial. */
	u32 host_serial;
	u32 handled_serial;
};

struct usbhid_probe_token {
	u8 slot;
	u8 dev_addr;
	u8 instance;
	u8 ifnum;
	u32 device_generation;
	u32 serial;
};

enum usbhid_transport_fault {
	USBHID_FAULT_DEVICE_CACHE_FULL = 1u << 0,
	USBHID_FAULT_PREPROBE = 1u << 1,
	USBHID_FAULT_ASYNC_CANCEL = 1u << 2,
	USBHID_FAULT_REPORT_DROP = 1u << 3,
	USBHID_FAULT_RX_REARM = 1u << 4,
	USBHID_FAULT_RAW_INTERFACE = 1u << 5,
	USBHID_FAULT_TOPOLOGY = 1u << 6,
	USBHID_FAULT_RX_STALL = 1u << 7,
	USBHID_FAULT_RX_XFER = 1u << 8,
	USBHID_FAULT_IO_ACCOUNTING = 1u << 9,
	USBHID_FAULT_RX_INVARIANT = 1u << 10,
	USBHID_FAULT_ASYNC_XFER_TUPLE = 1u << 11,
	USBHID_FAULT_ASYNC_HUB_PIN = 1u << 12,
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
	u32 generation;
	u32 io_pending;
	u32 interface_mask;
	TickType_t preprobe_retry_at;
	enum usbhid_preprobe_stage preprobe_stage;
	u8 device_desc_attempts_left;
	/* These flags share the enum-alignment byte in the physical cache. */
	bool mount_complete : 1;
	bool native_replace_pending : 1;
	bool reset_uncancelable : 1;
	/* Linux queues one cancelable reset_work per HID interface. */
	u8 reset_request_mask;
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
	struct usbhid_probe_slot probes[USBHID_PROBE_SLOTS];
	struct usbhid_reset_coordinator reset;
	/* Lifecycle-only, word-aligned descriptor scratch; callbacks never own it. */
	u32 preprobe_buffer_words[
		USBHID_PREPROBE_BUFFER_SIZE / sizeof(u32)];
};

static struct usb_device usbhid_root_hub;
static bool usbhid_root_hub_valid;
static struct usbhid_transport_pool *usbhid_transport_pool;
#define usbhid_reset (&usbhid_transport_pool->reset)
#define usbhid_preprobe_buffer \
	((u8 *)usbhid_transport_pool->preprobe_buffer_words)
static struct usbhid_usb_device *usbhid_usb_devices;
static struct usbhid_usb_device *usbhid_retired_devices;
static struct usbhid_probe_slot *usbhid_probes;
static struct usbhid_raw_interface usbhid_raw_interfaces[HID_HOST_RAW_INTERFACE_MAX];
static u32 usbhid_generation;
static u32 usbhid_probe_serial;
static u32 usbhid_transport_faults;
/* TinyUSB has one global enum owner; retain it even while reset is idle. */
static bool usbhid_enum_owner_active;
static size_t usbhid_report_queue_bytes;
static bool usbhid_report_queue_kick_pending;
static SemaphoreHandle_t usbhid_request_mutex;
static bool usbhid_io_get(struct hid_device *hid);
static void usbhid_teardown_wait(struct hid_device *hid);
static int usbhid_wait_io(struct hid_device *hid);
static int usbhid_wait_transport(struct hid_device *hid, bool teardown);
static void usbhid_request(struct hid_device *hid, struct hid_report *report,
				   enum hid_class_request reqtype);
static void usbhid_request_complete(const struct hid_async_request *req,
				    int status);
static void usbhid_lifecycle_kick(void);
static void usbhid_transport_fault(enum usbhid_transport_fault fault);
static void usbhid_backend_device_detach(uint8_t dev_addr);
static bool usbhid_usb_device_has_published_hid(
		const struct usbhid_usb_device *entry);
static int usbhid_reset_process(void);
static TickType_t usbhid_reset_wait_ticks(bool *active);
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

	hid_transport_lock();
	generation = usbhid_next_generation_locked();
	hid_transport_unlock();
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

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *candidate = &usbhid_usb_devices[i];

		if (&candidate->dev == dev && candidate->valid &&
		    candidate->dev.dev_addr == addr) {
			candidate->io_pending++;
			entry = candidate;
			break;
		}
	}
	hid_transport_unlock();
	if (!entry)
		return -ENODEV;

	*owner = entry;
	*dev_addr = addr;
	return 0;
}

static void usbhid_usb_device_io_put(struct usbhid_usb_device *entry)
{
	bool io_owned;
	bool wake_lifecycle;

	hid_transport_lock();
	io_owned = entry && entry->io_pending;
	if (!io_owned) {
		hid_transport_unlock();
		async_msg("ERR: HID_USB_IO_PUT");
		configASSERT(io_owned);
		return;
	}
	entry->io_pending--;
	wake_lifecycle = entry->retiring && !entry->io_pending;
	hid_transport_unlock();
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

	hid_transport_lock();
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
	hid_transport_unlock();
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

	hid_transport_lock();
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
	hid_transport_unlock();
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
	hid_transport_lock();
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
		entry->preprobe_retry_at = xTaskGetTickCount();
	}
	usbhid_usb_device_copy(entry, src, parent);
	/* valid is the release publication for all fields copied above. */
	entry->valid = true;
out:
	hid_transport_unlock();
	if (fault)
		usbhid_transport_fault(fault);
	return entry;
}

static void usbhid_probe_ack_locked(struct usbhid_probe_slot *slot, u32 serial)
{
	if (slot->host_serial == serial)
		slot->handled_serial = serial;
}

static u32 usbhid_next_probe_serial_locked(u32 handled_serial)
{
	u32 serial;

	do {
		serial = ++usbhid_probe_serial;
	} while (!serial || serial == handled_serial);
	return serial;
}

static void usbhid_probe_invalidate(uint8_t dev_addr, u32 generation,
				    bool match_generation, int instance)
{
	hid_transport_lock();
	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		struct usbhid_probe_slot *slot = &usbhid_probes[i];

		if (!slot->host_serial ||
		    slot->dev_addr != dev_addr ||
		    (match_generation && slot->generation != generation) ||
		    (instance >= 0 && slot->instance != (u8)instance))
			continue;

		/*
		 * Host unmount revokes only its publication serial. Lifecycle never
		 * clears a later mount record, and an in-flight token fails its final
		 * serial check; task-owned EP0 storage remains local to usbhid_parse().
		 */
		slot->host_serial = 0;
	}
	hid_transport_unlock();
}

/*
 * TinyUSB publishes HID interfaces before this port's shared device pre-probe.
 * A terminal device-descriptor failure consumes every current publication,
 * while only the host callbacks retain authority to revoke those records.
 */
static void usbhid_usb_device_ack_hid_publications(
		struct usbhid_usb_device *entry)
{
	hid_transport_lock();
	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		struct usbhid_probe_slot *slot = &usbhid_probes[i];

		if (slot->host_serial &&
		    slot->dev_addr == entry->dev.dev_addr &&
		    slot->generation == entry->generation)
			usbhid_probe_ack_locked(slot, slot->host_serial);
	}
	hid_transport_unlock();
}

static void usbhid_usb_device_invalidate_hid_publications(
		struct usbhid_usb_device *entry)
{
	usbhid_probe_invalidate(entry->dev.dev_addr, entry->generation, true, -1);
}

static void usbhid_invalidate_hid_publication_instance(
		uint8_t dev_addr, uint8_t instance)
{
	usbhid_probe_invalidate(dev_addr, 0, false, instance);
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
	hid_transport_lock();
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
	hid_transport_unlock();

	return entry;
}

static void
usbhid_usb_device_publish_retired(struct usbhid_usb_device *entry)
{
	if (!entry)
		return;

	hid_transport_lock();
	entry->retired_next = usbhid_retired_devices;
	usbhid_retired_devices = entry;
	hid_transport_unlock();
}

static struct usbhid_usb_device *usbhid_usb_device_take_retired(void)
{
	struct usbhid_usb_device *entry;

	hid_transport_lock();
	entry = usbhid_retired_devices;
	if (entry) {
		usbhid_retired_devices = entry->retired_next;
		entry->retired_next = NULL;
	}
	hid_transport_unlock();
	return entry;
}

static bool usbhid_usb_device_has_live_children(
		const struct usbhid_usb_device *parent)
{
	bool found = false;

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *child = &usbhid_usb_devices[i];

		if (child != parent && (child->valid || child->retiring) &&
		    child->dev.parent == &parent->dev) {
			found = true;
			break;
		}
	}
	hid_transport_unlock();
	return found;
}

static bool usbhid_usb_device_has_live_hids(
		const struct usbhid_usb_device *entry)
{
	bool found = false;

	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		const struct hid_device *hid = usbhid_devices[i];
		const struct usbhid_device *usbhid =
			hid ? hid->driver_data : NULL;

		if (usbhid && interface_to_usbdev(usbhid->intf) == &entry->dev) {
			found = true;
			break;
		}
	}
	hid_transport_unlock();
	return found;
}

static bool usbhid_usb_device_has_live_io(
		const struct usbhid_usb_device *entry)
{
	bool found;

	hid_transport_lock();
	found = entry->io_pending != 0;
	hid_transport_unlock();
	return found;
}

static void usbhid_usb_device_requeue_retired(
		struct usbhid_usb_device *entries)
{
	hid_transport_lock();
	while (entries) {
		struct usbhid_usb_device *entry = entries;

		entries = entry->retired_next;
		entry->retired_next = usbhid_retired_devices;
		usbhid_retired_devices = entry;
	}
	hid_transport_unlock();
}

static uintptr_t usbhid_topology_context(uint8_t rhport, uint8_t hub_addr,
					 uint8_t hub_port)
{
	return ((uintptr_t)rhport << 16) | ((uintptr_t)hub_addr << 8) |
	       (uintptr_t)hub_port;
}

static void usbhid_native_replace_ready_on_host(void *context)
{
	uintptr_t topology = (uintptr_t)context;

	/* This continuation itself runs in TinyUSB's sole host owner. */
	usbh_port_replace_ready_on_host((uint8_t)(topology >> 16),
					 (uint8_t)(topology >> 8),
					 (uint8_t)topology);
}

static void usbhid_cache_capacity_changed_on_host(void *context)
{
	/*
	 * The deferred-function event is the wake edge. TinyUSB's host service
	 * rechecks the durable cache-capacity predicate after this callback.
	 */
	(void)context;
}

static void usbhid_usb_device_release_retired(void)
{
	struct usbhid_usb_device *entry;
	struct usbhid_usb_device *blocked = NULL;
	bool capacity_changed = false;
	bool released = false;

	while ((entry = usbhid_usb_device_take_retired())) {
		bool replace_ready;
		uintptr_t replace_context;

		/* TinyUSB closes a hub before its subtree; reuse cache leaves first. */
		if (usbhid_usb_device_has_live_children(entry) ||
		    usbhid_usb_device_has_live_hids(entry) ||
		    usbhid_usb_device_has_live_io(entry) ||
		    usbhid_usb_device_has_published_hid(entry)) {
			entry->retired_next = blocked;
			blocked = entry;
			continue;
		}

		hid_transport_lock();
		replace_ready = entry->native_replace_pending;
		replace_context = usbhid_topology_context(entry->dev.rhport,
							 entry->dev.hub_addr,
							 entry->dev.hub_port);
		entry->native_replace_pending = false;
		entry->retiring = false;
		hid_transport_unlock();
		if (replace_ready)
			usbh_defer_func(usbhid_native_replace_ready_on_host,
					(void *)replace_context, false);
		else
			capacity_changed = true;
		released = true;
	}
	usbhid_usb_device_requeue_retired(blocked);
	/* Replacement-ready already wakes the same host queue for its topology. */
	if (capacity_changed)
		usbh_defer_func(usbhid_cache_capacity_changed_on_host, NULL, false);
	if (blocked && released)
		usbhid_lifecycle_kick();
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
	hid_transport_lock();
	entry->interface_mask |= interface_mask;
	entry->dev.config_storage.desc.bNumInterfaces =
		(u8)__builtin_popcount(entry->interface_mask);
	hid_transport_unlock();
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
	u8 described_endpoints = 0;
	bool have_interrupt_in = false;
	bool have_interrupt_out = false;

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

		/*
		 * Linux usbhid_start() searches the complete altsetting and keeps the
		 * first interrupt endpoint in each direction. TinyUSB owns the complete
		 * descriptor stream and physical endpoint objects; retain exactly that
		 * transport view instead of truncating the first four arbitrary endpoint
		 * descriptors into the firmware usb_interface shim.
		 */
		if (p[1] == USB_DT_ENDPOINT &&
		    described_endpoints < desc->bNumEndpoints) {
			bool is_in;

			described_endpoints++;
			/* TinyUSB's class-open parser rejects a malformed short endpoint. */
			if (p[0] < sizeof(tusb_desc_endpoint_t)) {
				p += p[0];
				continue;
			}

			const tusb_desc_endpoint_t *ep =
				(const tusb_desc_endpoint_t *)p;

			if (ep->bmAttributes.xfer != TUSB_XFER_INTERRUPT) {
				p += p[0];
				continue;
			}
			is_in = (ep->bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
				USB_DIR_IN;
			if ((is_in && have_interrupt_in) ||
			    (!is_in && have_interrupt_out)) {
				p += p[0];
				continue;
			}

			struct usb_host_endpoint *host_ep =
				&snapshot.endpoint[snapshot.endpoint_count++];

			host_ep->desc.bLength = ep->bLength;
			host_ep->desc.bDescriptorType = ep->bDescriptorType;
			host_ep->desc.bEndpointAddress = ep->bEndpointAddress;
			host_ep->desc.bmAttributes = ep->bmAttributes.xfer;
			host_ep->desc.wMaxPacketSize = ep->wMaxPacketSize;
			host_ep->desc.bInterval = ep->bInterval;
			if (is_in)
				have_interrupt_in = true;
			else
				have_interrupt_out = true;
		}

		p += p[0];
	}

	hid_transport_lock();
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
		hid_transport_unlock();
		usbhid_transport_fault(USBHID_FAULT_RAW_INTERFACE);
		return;
	}

	*free_slot = snapshot;
	hid_transport_unlock();
}

static bool usbhid_raw_interface_copy(uint8_t dev_addr, uint8_t ifnum,
				      struct usbhid_raw_interface *snapshot)
{
	bool found = false;

	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		const struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr && raw->ifnum == ifnum) {
			*snapshot = *raw;
			found = true;
			break;
		}
	}
	hid_transport_unlock();
	return found;
}

static void usbhid_raw_interface_close(uint8_t dev_addr)
{
	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_RAW_INTERFACE_MAX; i++) {
		struct usbhid_raw_interface *raw = &usbhid_raw_interfaces[i];

		if (raw->valid && raw->dev_addr == dev_addr)
			raw->valid = false;
	}
	hid_transport_unlock();

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

/* Caller holds the firmware transport mutex across lookup and pinning. */
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

struct hid_device *usbhid_report_owner_lookup_locked(unsigned int slot_index)
{
	struct hid_device *owner = NULL;

	/*
	 * Linux's URB retains its hid context until giveback. This port instead
	 * resolves the fixed report slot through the lifecycle registry while the
	 * common transport lock keeps removal and report ownership atomic.
	 */
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (!usbhid || usbhid->report_slot != slot_index + 1u)
			continue;
		/* Duplicate slot ownership is an invariant failure, not a choice. */
		if (owner)
			return NULL;
		owner = hid;
	}

	return owner;
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
	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		if (usbhid_devices[i] == hid) {
			usbhid_devices[i] = NULL;
			break;
		}
	}
	hid_transport_unlock();
}

static int usbhid_insert_if_generation(struct hid_device *hid,
					 const struct usbhid_probe_token *token)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_usb_device *entry;
	struct usbhid_probe_slot *slot = NULL;
	int ret = -ENODEV;

	hid_transport_lock();
	entry = usbhid_usb_device_find(usbhid->dev_addr);
	if (token && token->slot < USBHID_PROBE_SLOTS)
		slot = &usbhid_probes[token->slot];
	/*
	 * Previous port appended:
	 *     && tuh_hid_mounted(usbhid->dev_addr, usbhid->instance)
	 *
	 * TinyUSB calls HID unmount before clearing its class slot, and that callback
	 * rotates this probe token under the same firmware transport mutex.
	 * The token/cache generation fence is therefore the authoritative lifetime
	 * check; rereading TinyUSB's host-owned class table here is both redundant
	 * and outside that table's owner context.
	 */
	if (entry && entry->generation == token->device_generation && slot &&
	    usbhid->intf->dev.parent == &entry->dev.dev &&
	    token->dev_addr == usbhid->dev_addr &&
	    token->instance == usbhid->instance &&
	    token->ifnum == usbhid->ifnum &&
	    slot->host_serial == token->serial &&
	    slot->handled_serial != token->serial &&
	    slot->dev_addr == token->dev_addr &&
	    slot->instance == token->instance &&
	    slot->ifnum == token->ifnum &&
	    slot->generation == token->device_generation) {
		ret = usbhid_insert(hid);
		/*
		 * Publish either the pending host record or the live HID, never an empty
		 * detach window between them. probe_finish() acknowledges failures; a
		 * successful insert acknowledges the same serial atomically here.
		 */
		if (!ret)
			usbhid_probe_ack_locked(slot, token->serial);
	}
	hid_transport_unlock();
	return ret;
}

static int usbhid_probe(struct usbhid_usb_device *usb_entry,
			const struct usbhid_probe_token *token);

static int usbhid_probe_publish_mount(uint8_t dev_addr, uint8_t instance,
				      uint8_t ifnum)
{
	struct usbhid_usb_device *entry;
	struct usbhid_probe_slot *slot = NULL;
	bool already_published = false;
	bool entry_found = false;
	bool stored = false;

	hid_transport_lock();
	entry = usbhid_usb_device_find(dev_addr);
	if (!entry)
		goto out;
	entry_found = true;
	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		struct usbhid_probe_slot *candidate = &usbhid_probes[i];

		if (candidate->host_serial &&
		    candidate->dev_addr == dev_addr &&
		    candidate->instance == instance &&
		    candidate->generation == entry->generation) {
			already_published = true;
			break;
		}
		if (!candidate->host_serial && !slot)
			slot = candidate;
	}

	if (!already_published && slot && entry->valid) {
		/*
		 * TinyUSB mount publishes only the ephemeral class-instance identity.
		 * Lifecycle first completes shared device pre-probe; usbhid_parse()
		 * later validates HID metadata and performs Linux's descriptor request.
		 */
		slot->dev_addr = dev_addr;
		slot->instance = instance;
		slot->ifnum = ifnum;
		slot->generation = entry->generation;
		/* host_serial is the release publication for the identity above. */
		slot->host_serial =
			usbhid_next_probe_serial_locked(slot->handled_serial);
		stored = true;
	}
out:
	hid_transport_unlock();

	/* TinyUSB does not normally repeat mount for a live interface. */
	if (!entry_found)
		return -ENODEV;
	if (already_published)
		return 0;
	if (!stored)
		return -ENOMEM;
	/* Deferred pre-probe/probe state is authoritative; this is only a wake edge. */
	usbhid_lifecycle_kick();
	return 0;
}

static bool usbhid_probe_token_current(
		const struct usbhid_probe_token *token)
{
	const struct usbhid_probe_slot *slot;
	bool valid_token = false;

	if (!token || token->slot >= USBHID_PROBE_SLOTS)
		return false;

	hid_transport_lock();
	slot = &usbhid_probes[token->slot];
	valid_token = slot->host_serial == token->serial &&
		  slot->handled_serial != token->serial &&
		  slot->dev_addr == token->dev_addr &&
		  slot->instance == token->instance &&
		  slot->ifnum == token->ifnum &&
		  slot->generation == token->device_generation;
	hid_transport_unlock();
	return valid_token;
}

static int usbhid_probe_take_ready(struct usbhid_probe_token *token)
{
	struct usbhid_probe_slot *slot;
	struct usbhid_usb_device *entry;

	memset(token, 0, sizeof(*token));
	hid_transport_lock();
	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		slot = &usbhid_probes[i];
		if (!slot->host_serial ||
		    slot->host_serial == slot->handled_serial)
			continue;
		entry = usbhid_usb_device_find(slot->dev_addr);
		if (!entry || entry->generation != slot->generation) {
			/* Consume stale lifecycle work without changing host publication. */
			usbhid_probe_ack_locked(slot, slot->host_serial);
			continue;
		}
		if (!entry->have_device_desc ||
		    entry->preprobe_stage != USBHID_PREPROBE_DONE)
			continue;
		token->slot = (u8)i;
		token->dev_addr = slot->dev_addr;
		token->instance = slot->instance;
		token->ifnum = slot->ifnum;
		token->device_generation = slot->generation;
		token->serial = slot->host_serial;
		hid_transport_unlock();
		return 1;
	}
	hid_transport_unlock();
	return 0;
}

static void usbhid_probe_finish(const struct usbhid_probe_token *token)
{
	struct usbhid_probe_slot *slot;

	if (!token || token->slot >= USBHID_PROBE_SLOTS)
		return;

	hid_transport_lock();
	slot = &usbhid_probes[token->slot];
	if (slot->dev_addr == token->dev_addr &&
	    slot->instance == token->instance &&
	    slot->ifnum == token->ifnum &&
	    slot->generation == token->device_generation)
		usbhid_probe_ack_locked(slot, token->serial);
	hid_transport_unlock();
}

static bool usbhid_usb_device_has_string_indexes(const struct usbhid_usb_device *entry)
{
	return entry->dev.descriptor.iProduct ||
	       entry->dev.descriptor.iManufacturer ||
	       entry->dev.descriptor.iSerialNumber;
}

// Upstream Linux keeps this decode inside drivers/usb/core/message.c::usb_string().
// The active reduced USB-core shim has a separate usb_core_string_decode() for
// driver-time requests. Pre-probe enumeration still uses this helper with the
// lifecycle cache's fixed scratch buffer.
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

static void usbhid_log_device_desc_error(const char *reason,
					 int status, uint16_t actual)
{
	char msg[ASYNC_MSG_BUFSIZE];

	snprintf(msg, sizeof(msg), "ERR: HID_DEV_DESC_%s e%d l%u",
		 reason, status, (unsigned int)actual);
	_async_msg(msg);
}

static void usbhid_usb_device_descriptor_failed(
		struct usbhid_usb_device *entry,
		u32 generation, u8 dev_addr, int status, uint16_t actual,
		const char *reason)
{
	TickType_t retry_delay =
		pdMS_TO_TICKS(USBHID_DEVICE_DESCRIPTOR_RETRY_MS);
	bool is_current = false;
	bool retry = false;

	if (!retry_delay)
		retry_delay = 1;
	hid_transport_lock();
	if (entry->valid && entry->generation == generation &&
	    entry->dev.dev_addr == dev_addr &&
	    entry->preprobe_stage == USBHID_PREPROBE_DEVICE_DESC) {
		is_current = true;
		if (entry->device_desc_attempts_left) {
			entry->preprobe_retry_at =
				xTaskGetTickCount() + retry_delay;
			retry = true;
		}
	}
	hid_transport_unlock();
	if (!is_current)
		return;

	usbhid_log_device_desc_error(reason, status, actual);
	if (retry) {
		/*
		 * Linux USB core already owns a stable device descriptor here. This
		 * firmware-only refetch fills the compact usb_device shim, so retain
		 * every mounted HID across a bounded transient EP0 failure.
		 */
		usbhid_lifecycle_kick();
		return;
	}

	usbhid_transport_fault(USBHID_FAULT_PREPROBE);
	/* Terminal pre-probe failure consumes, but does not revoke, host mounts. */
	usbhid_usb_device_ack_hid_publications(entry);
}

static int usbhid_usb_device_get_descriptor(struct usbhid_usb_device *entry,
					    u8 type, u8 index, u16 langid,
					    u16 size)
{
	/*
	 * ret = usb_get_descriptor(&entry->dev, type, index,
	 *                          usbhid_preprobe_buffer, size);
	 *
	 * Linux USB core owns that synchronous helper. Keeping lifecycle's outer
	 * retry/backoff, exact string language ID, and fixed scratch policy here
	 * avoids adding usb_get_descriptor()'s separate three-attempt inner loop.
	 * The generic synchronous-over-async bridge sleeps on local fixed-pool
	 * admission before starting the complete wire timeout, so lifecycle needs no
	 * firmware-only admission loop here.
	 */
	memset(usbhid_preprobe_buffer, 0, size);
	return usb_control_msg(&entry->dev,
			       usb_rcvctrlpipe(&entry->dev, 0),
			       USB_REQ_GET_DESCRIPTOR, USB_DIR_IN,
			       ((u16)type << 8) | index, langid,
			       usbhid_preprobe_buffer, size,
			       USB_CTRL_GET_TIMEOUT);
}

static void usbhid_usb_device_defer_preprobe(
		struct usbhid_usb_device *entry, u32 generation)
{
	TickType_t retry_delay =
		pdMS_TO_TICKS(USBHID_DEVICE_DESCRIPTOR_RETRY_MS);
	bool is_current = false;

	if (!retry_delay)
		retry_delay = 1;
	hid_transport_lock();
	if (entry->valid && entry->generation == generation) {
		if (entry->device_desc_attempts_left <
			    USBHID_DEVICE_DESCRIPTOR_ATTEMPTS)
			entry->device_desc_attempts_left++;
		entry->preprobe_retry_at =
			xTaskGetTickCount() + retry_delay;
		is_current = true;
	}
	hid_transport_unlock();
	if (is_current) {
		usbhid_transport_fault(USBHID_FAULT_PREPROBE);
		usbhid_lifecycle_kick();
	}
}

static void usbhid_usb_device_abandon_strings(
		struct usbhid_usb_device *entry, u32 generation,
		enum usbhid_preprobe_stage stage)
{
	bool is_current = false;

	/* Optional strings must not keep an otherwise usable HID from probing. */
	hid_transport_lock();
	if (entry->valid && entry->generation == generation &&
	    entry->preprobe_stage == stage) {
		entry->preprobe_stage = USBHID_PREPROBE_DONE;
		is_current = true;
	}
	hid_transport_unlock();
	if (is_current) {
		/* Preserve the previous bounded-queue best-effort failure policy. */
		async_msg("WARN: HID_STRING_Q_FAIL");
		usbhid_lifecycle_kick();
	}
}

static void usbhid_usb_device_fetch_device_descriptor(
		struct usbhid_usb_device *entry)
{
	tusb_desc_device_t descriptor;
	u32 generation;
	u8 dev_addr;
	int ret;

	/*
	 * Global mount is the exact fence after every TinyUSB config driver. Only
	 * lifecycle enters here, so no separate callback-completion/requested bit is
	 * needed; usb_control_msg() itself pins this exact physical cache epoch.
	 */
	hid_transport_lock();
	if (!entry->valid || !entry->mount_complete || entry->have_device_desc ||
	    entry->preprobe_stage != USBHID_PREPROBE_DEVICE_DESC ||
	    !entry->device_desc_attempts_left) {
		hid_transport_unlock();
		return;
	}
	generation = entry->generation;
	dev_addr = entry->dev.dev_addr;
	entry->device_desc_attempts_left--;
	hid_transport_unlock();

	/*
	 * ret = usb_get_descriptor(&entry->dev, USB_DT_DEVICE, 0,
	 *                          usbhid_preprobe_buffer,
	 *                          sizeof(descriptor));
	 * The full USB-core helper is not linked. Keep the same request in lifecycle
	 * through the fixed-scratch generic bridge; USB_DT_DEVICE is named
	 * TUSB_DESC_DEVICE by the compact TinyUSB ch9 shim.
	 */
	ret = usbhid_usb_device_get_descriptor(entry, TUSB_DESC_DEVICE, 0, 0,
					       sizeof(descriptor));
	if (ret == -ENODEV)
		return;
	if (ret == -EBUSY) {
		usbhid_usb_device_defer_preprobe(entry, generation);
		return;
	}
	if (ret < 0) {
		usbhid_usb_device_descriptor_failed(entry, generation, dev_addr,
						    ret, 0, "XFER");
		return;
	}
	if (ret < (int)sizeof(descriptor)) {
		usbhid_usb_device_descriptor_failed(entry, generation, dev_addr,
						    0, (u16)ret, "SHORT");
		return;
	}
	memcpy(&descriptor, usbhid_preprobe_buffer, sizeof(descriptor));
	if (descriptor.bDescriptorType != TUSB_DESC_DEVICE) {
		usbhid_usb_device_descriptor_failed(entry, generation, dev_addr,
						    -ENODATA, (u16)ret,
						    "TYPE");
		return;
	}

	hid_transport_lock();
	if (entry->valid && entry->generation == generation &&
	    entry->dev.dev_addr == dev_addr &&
	    entry->preprobe_stage == USBHID_PREPROBE_DEVICE_DESC) {
		entry->dev.descriptor.idVendor = descriptor.idVendor;
		entry->dev.descriptor.idProduct = descriptor.idProduct;
		entry->dev.descriptor.bcdDevice = descriptor.bcdDevice;
		entry->dev.descriptor.bMaxPacketSize0 = descriptor.bMaxPacketSize0;
		entry->dev.descriptor.iManufacturer = descriptor.iManufacturer;
		entry->dev.descriptor.iProduct = descriptor.iProduct;
		entry->dev.descriptor.iSerialNumber = descriptor.iSerialNumber;
		entry->have_device_desc = true;
		entry->device_desc_attempts_left = 0;
		entry->dev.string_langid = USBHID_STRING_LANGID;
		entry->preprobe_stage = USBHID_PREPROBE_LANGID;
		entry->preprobe_retry_at = xTaskGetTickCount();
	}
	hid_transport_unlock();
	usbhid_lifecycle_kick();
}

static void usbhid_usb_device_fetch_string(struct usbhid_usb_device *entry)
{
	enum usbhid_preprobe_stage stage;
	char decoded_value[sizeof(entry->dev.product_buf)] = { 0 };
	u32 generation;
	u16 langid;
	u8 dev_addr;
	u8 index;
	bool is_current = false;
	bool finish = false;
	bool decoded_ok = false;
	int ret;

	/* Skip absent optional strings without issuing a firmware-only request. */
	for (;;) {
		hid_transport_lock();
		if (!entry->valid || !entry->have_device_desc ||
		    entry->preprobe_stage == USBHID_PREPROBE_DEVICE_DESC ||
		    entry->preprobe_stage == USBHID_PREPROBE_DONE) {
			hid_transport_unlock();
			return;
		}
		stage = entry->preprobe_stage;
		generation = entry->generation;
		dev_addr = entry->dev.dev_addr;
		langid = (u16)entry->dev.string_langid;
		index = 0;
		switch (stage) {
		case USBHID_PREPROBE_LANGID:
			if (!usbhid_usb_device_has_string_indexes(entry)) {
				finish = true;
				entry->preprobe_stage = USBHID_PREPROBE_DONE;
			} else {
				langid = 0;
			}
			break;
		case USBHID_PREPROBE_PRODUCT:
			index = entry->dev.descriptor.iProduct;
			if (!index)
				entry->preprobe_stage =
					USBHID_PREPROBE_MANUFACTURER;
			break;
		case USBHID_PREPROBE_MANUFACTURER:
			index = entry->dev.descriptor.iManufacturer;
			if (!index)
				entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
			break;
		case USBHID_PREPROBE_SERIAL:
			index = entry->dev.descriptor.iSerialNumber;
			if (!index) {
				finish = true;
				entry->preprobe_stage = USBHID_PREPROBE_DONE;
			}
			break;
		default:
			finish = true;
			entry->preprobe_stage = USBHID_PREPROBE_DONE;
			break;
		}
		hid_transport_unlock();
		if (finish) {
			usbhid_lifecycle_kick();
			return;
		}
		if (stage == USBHID_PREPROBE_LANGID || index)
			break;
	}

	/*
	 * ret = usb_get_string(&entry->dev, langid, index,
	 *                      usbhid_preprobe_buffer, 255);
	 * The USB-core helper/quirk layer is not linked. Preserve the existing
	 * best-effort single string fetch on the same standard request tuple.
	 */
	ret = usbhid_usb_device_get_descriptor(
		entry, USB_DT_STRING, index, langid,
		USBHID_STRING_DESCRIPTOR_SIZE);
	if (ret == -ENODEV)
		return;
	if (ret == -EBUSY) {
		usbhid_usb_device_abandon_strings(entry, generation, stage);
		return;
	}
	if (stage != USBHID_PREPROBE_LANGID && ret >= 2 &&
	    usbhid_preprobe_buffer[1] == USB_DT_STRING)
		decoded_ok = usb_string_decode(usbhid_preprobe_buffer, (u16)ret,
					       decoded_value,
					       sizeof(decoded_value)) >= 0;

	hid_transport_lock();
	if (entry->valid && entry->generation == generation &&
	    entry->dev.dev_addr == dev_addr && entry->preprobe_stage == stage) {
		is_current = true;
		switch (stage) {
		case USBHID_PREPROBE_LANGID:
			entry->dev.string_langid = USBHID_STRING_LANGID;
			if (ret >= 4 && usbhid_preprobe_buffer[1] == USB_DT_STRING)
				entry->dev.string_langid =
					(int)((u16)usbhid_preprobe_buffer[2] |
					      ((u16)usbhid_preprobe_buffer[3] << 8));
			entry->dev.have_langid = 1;
			entry->preprobe_stage = USBHID_PREPROBE_PRODUCT;
			break;
		case USBHID_PREPROBE_PRODUCT:
			if (decoded_ok) {
				memcpy(entry->dev.product_buf, decoded_value,
				       sizeof(entry->dev.product_buf));
				entry->dev.product = entry->dev.product_buf;
			}
			entry->preprobe_stage = USBHID_PREPROBE_MANUFACTURER;
			break;
		case USBHID_PREPROBE_MANUFACTURER:
			if (decoded_ok) {
				memcpy(entry->dev.manufacturer_buf, decoded_value,
				       sizeof(entry->dev.manufacturer_buf));
				entry->dev.manufacturer =
					entry->dev.manufacturer_buf;
			}
			entry->preprobe_stage = USBHID_PREPROBE_SERIAL;
			break;
		case USBHID_PREPROBE_SERIAL:
			if (decoded_ok) {
				memcpy(entry->dev.serial_buf, decoded_value,
				       sizeof(entry->dev.serial_buf));
				entry->dev.serial = entry->dev.serial_buf;
			}
			entry->preprobe_stage = USBHID_PREPROBE_DONE;
			break;
		default:
			break;
		}
		entry->preprobe_retry_at = xTaskGetTickCount();
	}
	hid_transport_unlock();
	if (is_current)
		usbhid_lifecycle_kick();
}

static void usbhid_usb_device_process_preprobe(
		struct usbhid_usb_device *entry)
{
	enum usbhid_preprobe_stage stage;

	hid_transport_lock();
	stage = entry->preprobe_stage;
	hid_transport_unlock();
	if (stage == USBHID_PREPROBE_DEVICE_DESC)
		usbhid_usb_device_fetch_device_descriptor(entry);
	else if (stage != USBHID_PREPROBE_DONE)
		usbhid_usb_device_fetch_string(entry);
}

static bool usbhid_usb_device_has_published_hid(
		const struct usbhid_usb_device *entry)
{
	bool published = false;

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		const struct usbhid_probe_slot *slot = &usbhid_probes[i];

		if (slot->host_serial &&
		    slot->dev_addr == entry->dev.dev_addr &&
		    slot->generation == entry->generation) {
			published = true;
			break;
		}
	}
	hid_transport_unlock();
	return published;
}

/* Caller holds the firmware transport mutex while inspecting both pools. */
static bool usbhid_usb_device_preprobe_pending_locked(
		const struct usbhid_usb_device *entry)
{
	bool waiting = false;

	if (!entry->valid || !entry->mount_complete ||
	    entry->preprobe_stage == USBHID_PREPROBE_DONE)
		return false;
	if (entry->preprobe_stage == USBHID_PREPROBE_DEVICE_DESC &&
	    (entry->have_device_desc || !entry->device_desc_attempts_left))
		return false;
	if (entry->preprobe_stage != USBHID_PREPROBE_DEVICE_DESC &&
	    !entry->have_device_desc)
		return false;

	for (size_t i = 0; i < USBHID_PROBE_SLOTS; i++) {
		const struct usbhid_probe_slot *slot = &usbhid_probes[i];

		if (slot->host_serial &&
		    slot->host_serial != slot->handled_serial &&
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

static bool usbhid_usb_device_retry_preprobes(void)
{
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];
		TickType_t now = xTaskGetTickCount();
		bool due;

		hid_transport_lock();
		due = usbhid_usb_device_preprobe_pending_locked(entry) &&
		      usbhid_tick_reached(now, entry->preprobe_retry_at);
		hid_transport_unlock();
		if (due) {
			usbhid_usb_device_process_preprobe(entry);
			return true;
		}
	}
	return false;
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

	hid_transport_lock();
	valid = usbhid_reset_target_valid_locked();
	hid_transport_unlock();
	return valid;
}

static bool usbhid_reset_target_epoch_present(void)
{
	bool present = false;

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if ((entry->valid || entry->retiring) &&
		    entry->generation == usbhid_reset->generation &&
		    entry->dev.dev_addr == usbhid_reset->dev_addr) {
			present = true;
			break;
		}
	}
	hid_transport_unlock();
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

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (entry->retiring && entry->dev.rhport == usbhid_reset->rhport &&
		    entry->dev.hub_addr == usbhid_reset->hub_addr &&
		    entry->dev.hub_port == usbhid_reset->hub_port) {
			retiring = true;
			break;
		}
	}
	hid_transport_unlock();
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

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (!entry->valid ||
		    (!entry->reset_request_mask && !entry->reset_uncancelable))
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
		/* One physical reset satisfies every interface claimant. */
		entry->reset_request_mask = 0;
		entry->reset_uncancelable = false;
		found = true;
		break;
	}
	hid_transport_unlock();

	if (!found)
		return false;

	ret = hid_async_control_gate_acquire();
	hid_transport_lock();
	if (ret) {
		usbhid_reset->gate_held = false;
		usbhid_reset->state = USBHID_RESET_FAILED;
	}
	hid_transport_unlock();
	if (!ret && ((usbhid_reset->hub_addr &&
		      !usbhid_reset->parent_generation) ||
		     !usbhid_reset_target_valid())) {
		hid_transport_lock();
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		hid_transport_unlock();
	}

	hid_transport_lock();
	cancelled = usbhid_reset->state == USBHID_RESET_CANCELLED;
	hid_transport_unlock();
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
	hid_transport_lock();
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
				   usbhid_tick_reached(
					   now, usbhid_reset->deadline)) {
				usbhid_reset->state = USBHID_RESET_FAILED;
			} else {
				usbhid_reset->state = USBHID_RESET_HUB_RETRY_WAIT;
			}
		}
	}
	hid_transport_unlock();

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
	hid_transport_lock();
	if (context == usbhid_reset && usbhid_reset->hub_io_pending &&
	    usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE &&
	    usbhid_reset->hub_addr == hub_addr &&
	    usbhid_reset->hub_port == hub_port) {
		rhport = usbhid_reset->rhport;
		matched = true;
	}
	hid_transport_unlock();

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
	hid_transport_lock();
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
	hid_transport_unlock();

	if (progress)
		usbhid_lifecycle_kick();
	return allow;
}

/*
 * Upstream Linux USB core owns enumeration and reset under the same device
 * lock. TinyUSB exposes neither owner, so this bounded callback keeps the
 * firmware's global EP0 gate closed from exact enum start through terminal.
 */
static void usbhid_backend_enum_update(uint8_t rhport, uint8_t hub_addr,
				       uint8_t hub_port, bool active,
				       bool terminal_failed)
{
	TickType_t now = xTaskGetTickCount();
	bool enum_owner_changed = false;
	bool progress = false;
	bool terminal = false;

	hid_transport_lock();
	if (usbhid_transport_pool) {
		terminal = !active;
		enum_owner_changed = usbhid_enum_owner_active != active;
		usbhid_enum_owner_active = active;
		if (usbhid_reset->gate_held && enum_owner_changed)
			progress = true;
		if (usbhid_reset->gate_held &&
		    usbhid_reset->rhport == rhport &&
		    usbhid_reset->hub_addr == hub_addr &&
		    usbhid_reset->hub_port == hub_port) {
			if (active) {
				usbhid_reset->enum_active = true;
				usbhid_reset->deadline = now +
					pdMS_TO_TICKS(
						USBHID_RESET_PHASE_TIMEOUT_MS);
				progress = true;
			} else if (usbhid_reset->enum_active) {
				usbhid_reset->enum_active = false;
				if (terminal_failed &&
				    usbhid_reset->state == USBHID_RESET_WAIT_REENUM) {
					usbhid_reset->state = USBHID_RESET_FAILED;
				} else {
					usbhid_reset->deadline = now +
						pdMS_TO_TICKS(
							USBHID_RESET_PHASE_TIMEOUT_MS);
				}
				progress = true;
			}
		}
		/* Time spent behind usbcore's global enum owner is not a reset failure. */
		if (terminal && usbhid_reset->gate_held &&
		    (usbhid_reset->state == USBHID_RESET_WAIT_CONTROL_IDLE ||
		     usbhid_reset->state == USBHID_RESET_HUB_RETRY_WAIT ||
		     usbhid_reset->state == USBHID_RESET_WAIT_REENUM)) {
			usbhid_reset->deadline = now + pdMS_TO_TICKS(
				usbhid_reset->hub_addr ? USBHID_RESET_HUB_TIMEOUT_MS :
						 USBHID_RESET_PHASE_TIMEOUT_MS);
		}
	}
	hid_transport_unlock();

	/* TinyUSB has one enum owner, so even a foreign terminal frees admission. */
	if (progress || terminal)
		usbhid_lifecycle_kick();
}

void usbhid_backend_enum_state(uint8_t rhport, uint8_t hub_addr,
			       uint8_t hub_port, bool active, bool success)
{
	usbhid_backend_enum_update(rhport, hub_addr, hub_port, active,
				   !active && !success);
}

void usbhid_backend_enum_parked(uint8_t rhport, uint8_t hub_addr,
				uint8_t hub_port)
{
	/*
	 * The physical TinyUSB owner is idle, but its retained ATTACH remains a
	 * valid Linux usb_device continuation. Release global admission without
	 * classifying that bounded cache wait as an enumeration terminal result.
	 */
	usbhid_backend_enum_update(rhport, hub_addr, hub_port, false, false);
}

static void usbhid_reset_root_attach_on_host(void *context)
{
	struct usbhid_usb_device *fresh;
	TickType_t now = xTaskGetTickCount();
	u32 generation = (u32)(uintptr_t)context;
	bool attach = false;
	bool parked = false;
	bool progress = false;
	bool started = false;
	u8 rhport = 0;

	/*
	 * Run the final native-replacement check in TinyUSB's host owner. If a
	 * mount callback is already in progress, its event necessarily precedes
	 * this deferred continuation and prevents a duplicate root attach.
	 */
	hid_transport_lock();
	if (usbhid_reset->root_io_pending &&
	    usbhid_reset->generation == generation) {
		usbhid_reset->root_io_pending = false;
		if (usbhid_reset->state == USBHID_RESET_ROOT_ATTACH_ACTIVE) {
			progress = true;
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
	hid_transport_unlock();

	if (attach) {
		started = usbh_port_attach_on_host(rhport, 0, 0);
		if (!started) {
			/* A global enum/control owner won final host admission. */
			hid_transport_lock();
			if (usbhid_reset->generation == generation &&
			    usbhid_reset->state == USBHID_RESET_WAIT_REENUM &&
			    !usbhid_reset->replacement_generation) {
				usbhid_reset->state =
					USBHID_RESET_WAIT_CONTROL_IDLE;
				usbhid_reset->deadline = xTaskGetTickCount() +
					pdMS_TO_TICKS(
						USBHID_RESET_PHASE_TIMEOUT_MS);
				parked = true;
			}
			hid_transport_unlock();
		}
	}
	/* Busy rollback waits for enum-terminal or physical-EP0-idle publication. */
	if (progress && !parked)
		usbhid_lifecycle_kick();
}

static bool usbhid_reset_queue_root(void)
{
	struct usbhid_usb_device *fresh;
	TickType_t now = xTaskGetTickCount();
	u32 generation;
	bool mounted;

	hid_transport_lock();
	if (usbhid_reset->state != USBHID_RESET_WAIT_CONTROL_IDLE) {
		hid_transport_unlock();
		return true;
	}
	if (usbhid_reset_replacement_valid_locked()) {
		usbhid_reset->state = USBHID_RESET_COMPLETE;
		hid_transport_unlock();
		return true;
	}
	fresh = usbhid_reset_fresh_epoch_locked();
	if (fresh) {
		mounted = fresh->mount_complete;
		if (mounted) {
			usbhid_reset->replacement_generation = fresh->generation;
			usbhid_reset->state = USBHID_RESET_COMPLETE;
		}
		hid_transport_unlock();
		return mounted;
	}
	/*
	 * Upstream USB core serializes reset and enumeration under the same device
	 * lock. TinyUSB has one global enum owner; wait for that owner's
	 * terminal publication instead of repeatedly deferring a rejected attach.
	 */
	if (usbhid_enum_owner_active) {
		hid_transport_unlock();
		return false;
	}
	usbhid_reset->state = USBHID_RESET_ROOT_ATTACH_ACTIVE;
	usbhid_reset->root_io_pending = true;
	usbhid_reset->deadline = now +
		pdMS_TO_TICKS(USBHID_RESET_PHASE_TIMEOUT_MS);
	generation = usbhid_reset->generation;
	hid_transport_unlock();

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
	bool request_pending;
	bool mounted;
	bool retry;
	int ret;

	hid_transport_lock();
	waiting_state = usbhid_reset->state;
	retry = waiting_state == USBHID_RESET_HUB_RETRY_WAIT;
	if (!retry && waiting_state != USBHID_RESET_WAIT_CONTROL_IDLE) {
		hid_transport_unlock();
		return true;
	}
	if (!usbhid_reset_parent_live_locked()) {
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		hid_transport_unlock();
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
			hid_transport_unlock();
			return mounted;
		}
	}
	/* Native enumeration already owns TinyUSB's sole physical enum slot. */
	if (usbhid_enum_owner_active) {
		hid_transport_unlock();
		return false;
	}
	hid_transport_unlock();

	ret = hid_async_device_epoch_snapshot(usbhid_reset->hub_addr,
					      &async_generation);
	if (ret) {
		bool done;

		hid_transport_lock();
		if (usbhid_reset->state != waiting_state) {
			hid_transport_unlock();
			return true;
		}
		if (retry) {
			usbhid_reset->state = USBHID_RESET_CANCELLED;
			hid_transport_unlock();
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
		hid_transport_unlock();
		return done;
	}

	hid_transport_lock();
	/* Revalidate the exact parent epoch after the async address snapshot. */
	if (usbhid_reset->state != waiting_state) {
		hid_transport_unlock();
		return true;
	}
	if (!retry) {
		if (usbhid_reset_replacement_valid_locked()) {
			usbhid_reset->state = USBHID_RESET_COMPLETE;
			hid_transport_unlock();
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
			hid_transport_unlock();
			return mounted;
		}
	}
	if (!usbhid_reset_parent_live_locked()) {
		usbhid_reset->state = USBHID_RESET_CANCELLED;
		hid_transport_unlock();
		return true;
	}
	/* Recheck after the address snapshot; enum start is independently published. */
	if (usbhid_enum_owner_active) {
		hid_transport_unlock();
		return false;
	}
	usbhid_reset->state = USBHID_RESET_HUB_IO_ACTIVE;
	usbhid_reset->hub_io_pending = true;
	usbhid_reset->attempts++;
	hid_transport_unlock();

	ret = hid_async_queue_hub_port_reset(usbhid_reset->hub_addr,
					     async_generation,
					     usbhid_reset->hub_port,
					     usbhid_reset_hub_complete,
					     usbhid_reset);
	if (!ret)
		return true;

	/* Queue rejection cannot race a completion because no slot was published. */
	hid_transport_lock();
	request_pending =
		usbhid_reset->state == USBHID_RESET_HUB_IO_ACTIVE &&
		usbhid_reset->hub_io_pending;
	if (!request_pending) {
		hid_transport_unlock();
		async_msg("ERR: HID_RESET_OWNER");
		configASSERT(request_pending);
		return true;
	}
	usbhid_reset->hub_io_pending = false;
	usbhid_reset->attempts--;
	if (ret == -ENODEV) {
		usbhid_reset->state = USBHID_RESET_CANCELLED;
	} else {
		usbhid_reset->state = waiting_state;
	}
	hid_transport_unlock();
	/* A terminal transition is progress; do not enter the event wait. */
	return ret == -ENODEV;
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

static int usbhid_reset_finish(void)
{
	enum usbhid_reset_state state;
	u32 paused_ticks;
	bool gate_held;

	hid_transport_lock();
	state = usbhid_reset->state;
	if ((state != USBHID_RESET_COMPLETE &&
	     state != USBHID_RESET_FAILED &&
	     state != USBHID_RESET_CANCELLED) ||
	    usbhid_reset->hub_io_pending || usbhid_reset->enum_active ||
	    usbhid_enum_owner_active) {
		hid_transport_unlock();
		return -EAGAIN;
	}
	gate_held = usbhid_reset->gate_held;
	paused_ticks = (u32)(xTaskGetTickCount() -
			     usbhid_reset->gate_started);
	hid_transport_unlock();
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

	hid_transport_lock();
	memset(usbhid_reset, 0, sizeof(*usbhid_reset));
	hid_transport_unlock();
	return 1;
}

static int usbhid_reset_process(void)
{
	enum usbhid_reset_state state;
	TickType_t now = xTaskGetTickCount();

	hid_transport_lock();
	/*
	 * Previous port consumed this exact mount fence directly from
	 * usbhid_backend_device_mount() through usbhid_reset_device_mounted().
	 * mount_complete is the durable callback publication; let the lifecycle
	 * owner advance its own reset state before taking the state snapshot.
	 */
	if (usbhid_reset->gate_held) {
		struct usbhid_usb_device *fresh =
			usbhid_reset_fresh_epoch_locked();

		if (fresh && fresh->mount_complete)
			(void)usbhid_reset_record_mounted_locked(fresh, now);
	}
	state = usbhid_reset->state;
	if (state != USBHID_RESET_IDLE &&
	    state != USBHID_RESET_COMPLETE &&
	    state != USBHID_RESET_FAILED &&
	    state != USBHID_RESET_CANCELLED &&
	    /* Published REMOVE is an uncancellable lifetime fence. */
	    state != USBHID_RESET_WAIT_RETIRE &&
	    !usbhid_reset->hub_io_pending &&
	    !usbhid_reset->enum_active &&
	    !usbhid_enum_owner_active &&
	    usbhid_tick_reached(now, usbhid_reset->deadline)) {
		usbhid_reset->state = USBHID_RESET_FAILED;
		state = USBHID_RESET_FAILED;
	}
	hid_transport_unlock();

	if (state == USBHID_RESET_IDLE)
		return usbhid_reset_start_pending() ? 1 : 0;

	if (state == USBHID_RESET_COMPLETE || state == USBHID_RESET_FAILED ||
	    state == USBHID_RESET_CANCELLED)
		return usbhid_reset_finish();

	if (state == USBHID_RESET_WAIT_RETIRE) {
		if (usbhid_reset_target_epoch_present())
			return -EAGAIN;
		hid_transport_lock();
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
		hid_transport_unlock();
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

static TickType_t usbhid_reset_wait_ticks(bool *active)
{
	TickType_t deadline;
	TickType_t now = xTaskGetTickCount();
	enum usbhid_reset_state state;
	bool callback_owned;

	hid_transport_lock();
	state = usbhid_reset->state;
	deadline = usbhid_reset->deadline;
	callback_owned = usbhid_reset->hub_io_pending ||
			 usbhid_reset->enum_active || usbhid_enum_owner_active;
	*active = state != USBHID_RESET_IDLE;
	hid_transport_unlock();

	/*
	 * Upstream reset work sleeps on USB-core completion and exact timers. Every
	 * callback-owned firmware phase now publishes the same wake edge. WAIT_RETIRE
	 * and terminal retirement are uncancellable lifetime fences whose final
	 * owner also wakes lifecycle, so none of these states needs periodic polling.
	 */
	if (!*active || state == USBHID_RESET_WAIT_RETIRE || callback_owned ||
	    state == USBHID_RESET_COMPLETE || state == USBHID_RESET_FAILED ||
	    state == USBHID_RESET_CANCELLED)
		return portMAX_DELAY;
	if (usbhid_tick_reached(now, deadline))
		return 0;
	return deadline - now;
}

static TickType_t usbhid_preprobe_wait_ticks(void)
{
	TickType_t now = xTaskGetTickCount();
	TickType_t wait = portMAX_DELAY;

	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		const struct usbhid_usb_device *entry = &usbhid_usb_devices[i];
		TickType_t candidate;

		if (!usbhid_usb_device_preprobe_pending_locked(entry))
			continue;
		if (usbhid_tick_reached(now, entry->preprobe_retry_at)) {
			wait = 0;
			break;
		}
		candidate = entry->preprobe_retry_at - now;
		if (wait == portMAX_DELAY || candidate < wait)
			wait = candidate;
	}
	hid_transport_unlock();
	return wait;
}

static TickType_t usbhid_lifecycle_wait_ticks(void)
{
	bool reset_active;
	TickType_t reset_wait = usbhid_reset_wait_ticks(&reset_active);

	/* Preprobe is deliberately parked while reset owns the global EP0 gate. */
	if (reset_active)
		return reset_wait;
	return usbhid_preprobe_wait_ticks();
}

int usbhid_lifecycle_init(void)
{
	SemaphoreHandle_t request_mutex;

	usbhid_transport_pool = kzalloc(sizeof(*usbhid_transport_pool), GFP_KERNEL);
	if (!usbhid_transport_pool)
		return -ENOMEM;
	request_mutex = xSemaphoreCreateMutex();
	if (!request_mutex) {
		kfree(usbhid_transport_pool);
		usbhid_transport_pool = NULL;
		return -ENOMEM;
	}
	usbhid_usb_devices = usbhid_transport_pool->devices;
	usbhid_probes = usbhid_transport_pool->probes;
	/* Publish only after both the pool and task-side PI serializer exist. */
	usbhid_request_mutex = request_mutex;

	return 0;
}

static void usbhid_lifecycle_kick(void)
{
	TaskHandle_t task = atomic_load_explicit(&usbhid_lifecycle_task_handle,
						memory_order_acquire);

	/* Durable lifecycle state is the predicate; this bit is only a wake edge. */
	if (task)
		(void)xTaskNotifyIndexed(task, USBHID_LIFECYCLE_NOTIFY_INDEX,
					 USBHID_LIFECYCLE_NOTIFY_WAKE, eSetBits);
}

void usbhid_lifecycle_schedule_work(struct work_struct *work)
{
	hid_transport_lock();
	if (work->pending) {
		hid_transport_unlock();
		return;
	}
	work->pending = 1;
	if (usbhid_lifecycle_work_tail)
		usbhid_lifecycle_work_tail->next = work;
	else
		usbhid_lifecycle_work_head = work;
	usbhid_lifecycle_work_tail = work;
	hid_transport_unlock();

	usbhid_lifecycle_kick();
}

void usbhid_lifecycle_cancel_work(struct work_struct *work)
{
	struct work_struct *queued;
	struct work_struct *previous = NULL;

	hid_transport_lock();
	for (queued = usbhid_lifecycle_work_head; queued;
	     previous = queued, queued = queued->next) {
		if (queued != work)
			continue;
		if (previous)
			previous->next = work->next;
		else
			usbhid_lifecycle_work_head = work->next;
		if (usbhid_lifecycle_work_tail == work)
			usbhid_lifecycle_work_tail = previous;
		work->next = NULL;
		work->pending = 0;
		break;
	}
	hid_transport_unlock();
}

static bool usbhid_lifecycle_process_work(void)
{
	struct work_struct *work;
	bool disconnect_pending = false;

	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; ++i) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid;

		if (!hid)
			continue;
		usbhid = hid->driver_data;
		if (usbhid->disconnect_queued) {
			disconnect_pending = true;
			break;
		}
	}
	if (disconnect_pending) {
		hid_transport_unlock();
		return true;
	}
	work = usbhid_lifecycle_work_head;
	if (work) {
		usbhid_lifecycle_work_head = work->next;
		if (!usbhid_lifecycle_work_head)
			usbhid_lifecycle_work_tail = NULL;
		work->next = NULL;
		work->pending = 0;
	}
	hid_transport_unlock();
	if (!work)
		return false;

	work->func(work);
	return true;
}

static void usbhid_transport_fault(enum usbhid_transport_fault fault)
{
	hid_transport_lock();
	usbhid_transport_faults |= (u32)fault;
	hid_transport_unlock();
	usbhid_lifecycle_kick();
}

void usbhid_backend_rx_report_dropped(void)
{
	usbhid_transport_fault(USBHID_FAULT_REPORT_DROP);
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

void usbhid_backend_rx_invariant_failed(void)
{
	usbhid_transport_fault(USBHID_FAULT_RX_INVARIANT);
}

void usbhid_backend_async_invariant_failed(
	enum usbhid_async_invariant reason)
{
	enum usbhid_transport_fault fault;

	switch (reason) {
	case USBHID_ASYNC_INVARIANT_NONE:
		return;
	case USBHID_ASYNC_INVARIANT_XFER_TUPLE:
		fault = USBHID_FAULT_ASYNC_XFER_TUPLE;
		break;
	case USBHID_ASYNC_INVARIANT_HUB_PIN:
		fault = USBHID_FAULT_ASYNC_HUB_PIN;
		break;
	default:
		/* A new callback reason must get a fixed lifecycle diagnostic. */
		fault = USBHID_FAULT_ASYNC_XFER_TUPLE;
		break;
	}
	usbhid_transport_fault(fault);
}

void usbhid_backend_control_gate_idle(void)
{
	/* hid_async owns the predicate; lifecycle owns every reset transition. */
	usbhid_lifecycle_kick();
}

void usbhid_backend_host_control_idle(void)
{
	bool waiting;

	/* The async submitter owns this durable EP0-resource predicate. */
	hid_async_host_control_ready();

	/* Publish only for the root admission which consumes global EP0 idle. */
	hid_transport_lock();
	waiting = usbhid_transport_pool && usbhid_reset->gate_held &&
		  !usbhid_reset->hub_addr &&
		  usbhid_reset->state == USBHID_RESET_WAIT_CONTROL_IDLE;
	hid_transport_unlock();
	if (waiting)
		usbhid_lifecycle_kick();
}

void usbhid_backend_host_endpoint_idle(uint8_t dev_addr, uint8_t ep_addr)
{
	/* Callback-side glue only publishes the exact endpoint wake edge. */
	hid_async_host_endpoint_ready(dev_addr, ep_addr);
}

int usbhid_backend_queue_device_reset(struct hid_device *hid,
				      uint32_t report_revision,
				      bool reset_work_running)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	struct usb_device *dev = usbhid ? interface_to_usbdev(usbhid->intf) : NULL;
	u8 reset_bit = usbhid && usbhid->report_slot &&
		usbhid->report_slot <= CFG_TUH_HID ?
		(u8)(1u << (usbhid->report_slot - 1u)) : 0;
	int ret = -ENODEV;

	/* Publish only an exact cache epoch; no hid pointer survives this call. */
	hid_transport_lock();
	for (size_t i = 0; dev && usbhid_transport_pool &&
			 i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (&entry->dev != dev)
			continue;
		/*
		 * Close cancels pending reset_work. An already-running equivalent may
		 * publish across close, matching the upstream outcome; stop still fences it.
		 */
		if (usbhid->transport_stopping ||
		    (!reset_work_running &&
		     (!usbhid->report_wanted ||
		      usbhid->report_revision != report_revision))) {
			ret = -ECANCELED;
		} else if (usbhid_reset->state != USBHID_RESET_IDLE &&
		    usbhid_reset->generation == entry->generation) {
			ret = 0;
		} else if (entry->valid) {
			if (reset_work_running) {
				/* A running Linux reset_work is no longer close-cancelable. */
				entry->reset_uncancelable = true;
				ret = 0;
			} else if (reset_bit) {
				/* Preserve sibling interface claimants independently. */
				entry->reset_request_mask |= reset_bit;
				ret = 0;
			}
		}
		break;
	}
	hid_transport_unlock();

	if (!ret)
		usbhid_lifecycle_kick();
	return ret;
}

bool usbhid_backend_cancel_device_reset(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	struct usb_device *dev = usbhid ? interface_to_usbdev(usbhid->intf) : NULL;
	u8 reset_bit = usbhid && usbhid->report_slot &&
		usbhid->report_slot <= CFG_TUH_HID ?
		(u8)(1u << (usbhid->report_slot - 1u)) : 0;
	bool cancelled = false;

	/*
	 * Linux cancel_work_sync() removes only this interface's queued reset_work.
	 * Once lifecycle has claimed the mask it is running and must complete; a
	 * sibling's bit or an already-running claimant is never revoked here.
	 */
	hid_transport_lock();
	for (size_t i = 0; dev && reset_bit && usbhid_transport_pool &&
			 i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (&entry->dev != dev || !(entry->reset_request_mask & reset_bit))
			continue;
		entry->reset_request_mask &= (u8)~reset_bit;
		cancelled = true;
		break;
	}
	hid_transport_unlock();
	return cancelled;
}

static void usbhid_lifecycle_log_transport_faults(void)
{
	u32 faults;

	hid_transport_lock();
	faults = usbhid_transport_faults;
	usbhid_transport_faults = 0;
	hid_transport_unlock();

	if (faults & USBHID_FAULT_DEVICE_CACHE_FULL)
		async_msg("ERR: HID_USB_DEV_ALLOC_FAIL");
	if (faults & USBHID_FAULT_PREPROBE)
		async_msg("ERR: HID_PROBE_DEFER_FAIL");
	if (faults & USBHID_FAULT_ASYNC_CANCEL)
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	if (faults & USBHID_FAULT_REPORT_DROP)
		async_msg("ERR: HID_REPORT_SKIP");
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
	if (faults & USBHID_FAULT_IO_ACCOUNTING)
		async_msg("ERR: HID_IO_ACCOUNTING");
	if (faults & USBHID_FAULT_RX_INVARIANT)
		async_msg("ERR: HID_RX_INVARIANT");
	if (faults & USBHID_FAULT_ASYNC_XFER_TUPLE)
		async_msg("ERR: HID_ASYNC_XFER_TUPLE");
	if (faults & USBHID_FAULT_ASYNC_HUB_PIN)
		async_msg("ERR: HID_ASYNC_HUB_PIN");
}

static void usbhid_disconnect(struct usb_interface *intf);

static void usbhid_lifecycle_drain_disconnects(void)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct usb_interface *intf = NULL;

		hid_transport_lock();
		if (usbhid_devices[i]) {
			struct hid_device *hid = usbhid_devices[i];
			struct usbhid_device *usbhid = hid->driver_data;

			if (usbhid->disconnect_queued)
				intf = usbhid->intf;
		}
		hid_transport_unlock();

		/* Only this lifecycle task removes HID slots, so intf stays owned here. */
		if (intf)
			usbhid_disconnect(intf);
	}
}

static int usbhid_lifecycle_process_ready_probes(void)
{
	for (;;) {
		struct usbhid_probe_token token;
		struct usbhid_usb_device *entry;
		int ret;

		ret = usbhid_probe_take_ready(&token);
		if (ret <= 0)
			return ret;

		hid_transport_lock();
		entry = usbhid_usb_device_find(token.dev_addr);
		if (entry && entry->generation != token.device_generation)
			entry = NULL;
		hid_transport_unlock();
		/*
		 * Previous port also tested:
		 *     !tuh_hid_mounted(token.dev_addr, token.instance)
		 *
		 * Mount published this token only after TinyUSB marked the interface
		 * mounted. HID unmount invalidates it before TinyUSB clears that state,
		 * so the owned token is the stronger cross-task liveness predicate.
		 */
		if (!entry || !usbhid_probe_token_current(&token)) {
			usbhid_probe_finish(&token);
			continue;
		}

		(void)usbhid_probe(entry, &token);
		usbhid_probe_finish(&token);
	}
}

void usbhid_lifecycle_task(void *pvParameters)
{
	bool owner_available;

	(void)pvParameters;

	/* A pre-registration kick is safe: the first loop scans all durable state. */
	hid_transport_lock();
	owner_available = !atomic_load_explicit(
		&usbhid_lifecycle_task_handle, memory_order_relaxed);
	if (!owner_available) {
		hid_transport_unlock();
		async_msg("ERR: HID_LIFE_OWNER");
		configASSERT(owner_available);
		vTaskDelete(NULL);
		return;
	}
	atomic_store_explicit(&usbhid_lifecycle_task_handle,
			      xTaskGetCurrentTaskHandle(), memory_order_release);
	hid_transport_unlock();

	for (;;) {
		int ret;

		/* Flags/cache slots own work; notification is only a bounded wakeup. */
		usbhid_lifecycle_drain_disconnects();
		usbhid_usb_device_release_retired();
		ret = usbhid_reset_process();
		if (ret > 0)
			continue;
		if (!ret) {
			/* Revisit disconnect/reset before every next descriptor wire step. */
			if (usbhid_usb_device_retry_preprobes())
				continue;
			(void)usbhid_lifecycle_process_ready_probes();
		}
		usbhid_lifecycle_log_transport_faults();
		/* One virtual-device work item, then recheck physical teardown/reset. */
		if (usbhid_lifecycle_process_work())
			continue;

		(void)xTaskNotifyWaitIndexed(USBHID_LIFECYCLE_NOTIFY_INDEX,
					     0, UINT32_MAX, NULL,
					     usbhid_lifecycle_wait_ticks());
	}
}

// static int usbhid_probe(struct usb_interface *intf, const struct usb_device_id *id)
// The lifecycle token is the firmware's retained usb_interface identity;
// lifecycle builds the shim before Linux's task-side parse/add flow.
static int usbhid_probe(struct usbhid_usb_device *usb_entry,
			const struct usbhid_probe_token *token)
{
	struct usb_device *dev = &usb_entry->dev;
	// struct usb_endpoint_descriptor *ep;
	// TinyUSB owns endpoint descriptors; the interface shim records the same
	// mandatory interrupt-IN endpoint below.
	struct usbhid_device *usbhid;
	struct hid_device *hid;
	u8 dev_addr = token->dev_addr;
	struct usbhid_raw_interface raw_snapshot;
	const struct usbhid_raw_interface *raw;
	size_t len;
	int ret;

	hid = hid_allocate_device();
	if (IS_ERR(hid)) {
		async_msg("ERR: HID_ALLOC_FAIL");
		return PTR_ERR(hid);
	}

	usbhid = kzalloc_obj(*usbhid);
	if (!usbhid) {
		hid_destroy_device(hid);
		async_msg("ERR: HID_ALLOC_FAIL");
		return -ENOMEM;
	}
	hid->driver_data = usbhid;
	usbhid->hid = hid;

	/*
	 * Previous port:
	 * memset(&itf_info, 0, sizeof(itf_info));
	 * if (!tuh_vid_pid_get(dev_addr, &vid, &pid) ||
	 *     !tuh_hid_itf_get_info(dev_addr, instance, &itf_info))
	 * 	return -ENODEV;
	 *
	 * Upstream usbhid_probe() consumes a stable usb_device/usb_interface owned
	 * by USB core, not live host-controller tables. The callback-captured raw
	 * interface and probe token are that stable boundary here; the exact
	 * device descriptor already populated usb_entry in lifecycle context.
	 */
	if (!usbhid_raw_interface_copy(dev_addr, token->ifnum,
				       &raw_snapshot)) {
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
	// Previous port: TinyUSB mount callback builds a local usb_interface shim.
	// Lifecycle builds it from the retained interface token and raw snapshot.
	usbhid->dev_addr = dev_addr;
	usbhid->instance = token->instance;
	usbhid->generation = usbhid_next_generation();

	usbhid->usb_altsetting.desc.bInterfaceNumber = token->ifnum;
	raw = &raw_snapshot;
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
	// init_waitqueue_head(&usbhid->wait);
	// The firmware keeps a multi-waiter intrusive list in usbhid_device instead;
	// stack waiters remain linked until their durable predicate is resolved.
	usbhid->io_waiters = NULL;
	mutex_init(&usbhid->mutex);
	/* Linux embeds this mutex; FreeRTOS allocates it from the bounded heap. */
	if (!mutex_initialized(&usbhid->mutex)) {
		async_msg("ERR: HID_ALLOC_FAIL");
		ret = -ENOMEM;
		goto fail;
	}

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

	ret = usbhid_insert_if_generation(hid, token);
	if (ret < 0) {
		async_msg(ret == -ENOMEM ? "ERR: HID_TABLE_FULL" :
			  "ERR: HID_ENUM_GONE");
		goto fail;
	}

	ret = hid_add_device(hid);
	if (ret < 0) {
		usbhid_report_stop(hid);
		if (hid_async_cancel_device(hid))
			async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
		usbhid_teardown_wait(hid);
		/* A failed device_add() need not reach the HID driver's .stop(). */
		usbhid_report_release(hid);
		hid_free_buffers(hid_to_usb_dev(hid), hid);
		usbhid_remove_slot(hid);
		if (ret == -ENOMEM)
			async_msg("ERR: HID_PROBE_NOMEM");
		else
			async_msg(ret == -ENODEV ? "WARN: HID_IGNORED" :
				  "ERR: HID_ADD_FAIL");
		goto fail;
	}

	/*
	 * Upstream may leave driver probe GET_REPORT or SET_REPORT requests in the
	 * usbhid control/output FIFOs. Firmware runs direct interrupt IN
	 * independently from async EP0/OUT and exposes an always-on evdev client, so
	 * finish that finite probe I/O before client activation can start input or
	 * issue output of its own.
	 * usbhid_wait_io() excludes the continuously armed interrupt-IN transport.
	 */
	ret = usbhid_wait_io(hid);
	if (ret < 0)
		goto fail_bound;

	// Linux keeps a per-open evdev client private until open succeeds. Firmware
	// auto-opens its evdev clients after the complete HID probe; a failed client
	// remains local to that input handler and does not unbind the HID device.
	evdev_activate_hid(hid);

	/* Publish the fully built input/driver graph to sibling-interface users. */
	hid_transport_lock();
	if (usbhid->disconnect_queued || usbhid->transport_stopping)
		ret = -ENODEV;
	else
		usbhid->driver_ready = true;
	usbhid->probe_raw_event = false;
	hid_transport_unlock();
	if (ret < 0)
		goto fail_bound;

	/*
	 * Direct interrupt IN starts from usbhid_open()/usbhid_start() after the
	 * Linux HID device binds, matching upstream hid_start_in() lifecycle
	 * instead of probe-time report delivery.
	 */
	return 0;

fail_bound:
	/* Keep the registry pin until every report/async owner is fenced. */
	usbhid_report_stop(hid);
	if (hid_async_cancel_device(hid))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_teardown_wait(hid);
	usbhid_remove_slot(hid);
fail:
	hid_destroy_device(hid);
	/* hid_destroy_device() may run .close()/.stop(), so destroy this last. */
	mutex_destroy(&usbhid->mutex);
	kfree(usbhid);
	return ret;
}

/* Lifecycle task supplies the process context Linux USB core has here. */
static void usbhid_disconnect(struct usb_interface *intf)
{
	struct hid_device *hid = usb_get_intfdata(intf);
	struct usbhid_device *usbhid;

	if (WARN_ON(!hid))
		return;

	usbhid = hid->driver_data;
	// spin_lock_irq(&usbhid->lock); /* Sync with error and led handlers */
	// set_bit(HID_DISCONNECTED, &usbhid->iofl);
	// spin_unlock_irq(&usbhid->lock);
	// TinyUSB callbacks run in host-task context rather than URB IRQ context.
	// Their publisher closes new interface producers under the shared transport
	// task mutex; the lifecycle task repeats the idempotent stop before its
	// synchronous drains.
	usbhid_report_unplug(hid);
	if (hid_async_cancel_device(hid))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_teardown_wait(hid);
	usbhid_remove_slot(hid);
	hid_destroy_device(hid);
	/* Linux embeds this mutex; the FreeRTOS compatibility object owns a queue. */
	mutex_destroy(&usbhid->mutex);
	kfree(usbhid);
}

/* Caller holds the firmware transport mutex across publication + pin. */
static bool usbhid_publish_disconnect_locked(struct hid_device *hid,
					      bool *pinned)
{
	struct usbhid_device *usbhid = hid->driver_data;

	*pinned = false;
	if (usbhid->disconnect_queued)
		return false;

	usbhid->disconnect_queued = true;
	if (!usbhid->transport_stopping) {
		usbhid->io_pending++;
		*pinned = true;
	}
	return true;
}

/*
 * Upstream Linux: no callback-side equivalent; usbcore invokes
 * usbhid_disconnect() in process context. TinyUSB supplies only an interface
 * address pair, so publish the stop and let the lifecycle task invoke the
 * upstream-shaped teardown.
 */
static void usbhid_publish_disconnect(uint8_t dev_addr, uint8_t instance)
{
	struct hid_device *hid;
	bool published = false;
	bool pinned = false;

	/*
	 * Linux USB core retains intf through disconnect(). TinyUSB supplies only
	 * an address pair, so publish disconnect and acquire the firmware HID lease
	 * atomically before callback context may hand teardown to lifecycle.
	 */
	hid_transport_lock();
	hid = usbhid_lookup_locked(dev_addr, instance, 0);
	if (hid)
		published = usbhid_publish_disconnect_locked(hid, &pinned);
	hid_transport_unlock();

	if (pinned)
		usbhid_report_unplug(hid);
	if (pinned)
		usbhid_io_put(hid);
	/*
	 * Physical detach has already published address-wide async cancellation
	 * before TinyUSB reaches HID class close. This interface callback is the
	 * exact token fence and fallback publisher; synchronous cancellation and
	 * destruction remain task-owned.
	 */
	if (published)
		usbhid_lifecycle_kick();
}

void usbhid_backend_hid_mount(uint8_t dev_addr, uint8_t instance,
			      uint8_t const *desc_report, uint16_t desc_len)
{
	tuh_itf_info_t itf_info;
	int ret;

	/*
	 * The SHA-pinned build-local TinyUSB HID class deliberately skips its
	 * duplicate enumeration-buffer report fetch and always mounts with NULL
	 * descriptor data. This callback records only interface metadata; Linux's
	 * four-attempt descriptor read now runs later in lifecycle task context.
	 */
	(void)desc_report;
	(void)desc_len;
	/*
	 * Previous port:
	 *     entry = usbhid_usb_device_prepare(dev_addr);
	 *     ret = usbhid_usb_device_store_pending_probe(entry, instance);
	 *
	 * TinyUSB's class instance is callback-ephemeral. Capture only its stable
	 * interface identity before the class slot can be cleared. The application-
	 * driver snapshot already published the matching cache epoch; HID class
	 * policy and raw descriptor interpretation belong to lifecycle context.
	 */
	memset(&itf_info, 0, sizeof(itf_info));
	if (!tuh_hid_itf_get_info(dev_addr, instance, &itf_info)) {
		usbhid_transport_fault(USBHID_FAULT_PREPROBE);
		return;
	}
	ret = usbhid_probe_publish_mount(
		dev_addr, instance, itf_info.desc.bInterfaceNumber);
	if (ret) {
		usbhid_transport_fault(ret == -ENODEV ? USBHID_FAULT_TOPOLOGY :
				       USBHID_FAULT_PREPROBE);
		return;
	}
}

void usbhid_backend_hid_umount(uint8_t dev_addr, uint8_t instance)
{
	usbhid_invalidate_hid_publication_instance(dev_addr, instance);
	usbhid_publish_disconnect(dev_addr, instance);
}

bool usbhid_backend_native_replace_begin(uint8_t rhport, uint8_t hub_addr,
					 uint8_t hub_port)
{
	bool retained = false;

	/*
	 * Newer TinyUSB removes a mounted same-topology device before accepting a
	 * duplicate ATTACH. Its close callbacks only publish firmware disconnect;
	 * retain the exact cache epoch until lifecycle releases every Linux owner.
	 */
	hid_transport_lock();
	for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
		struct usbhid_usb_device *entry = &usbhid_usb_devices[i];

		if (!entry->valid || entry->dev.rhport != rhport ||
		    entry->dev.hub_addr != hub_addr ||
		    entry->dev.hub_port != hub_port)
			continue;
		entry->native_replace_pending = true;
		retained = true;
		break;
	}
	hid_transport_unlock();
	return retained;
}

bool usbhid_backend_cache_available(void)
{
	bool available = false;

	/*
	 * Linux USB core admits a new usb_device only when it can retain the object
	 * through disconnect. TinyUSB has one enumeration owner, so this bounded
	 * predicate plus its deferred-ATTACH FIFO reserves the next free epoch
	 * without allocating or waiting in the host callback.
	 */
	hid_transport_lock();
	if (usbhid_usb_devices) {
		for (size_t i = 0; i < USBHID_USB_DEVICE_SLOTS; i++) {
			const struct usbhid_usb_device *entry =
				&usbhid_usb_devices[i];

			if (!entry->valid && !entry->retiring) {
				available = true;
				break;
			}
		}
	}
	hid_transport_unlock();
	return available;
}

void usbhid_backend_device_mount(uint8_t dev_addr)
{
	struct usbhid_usb_device *entry;
	bool published = false;

	/*
	 * Previous port rebuilt/upserted topology here with:
	 *     entry = usbhid_usb_device_prepare(dev_addr);
	 *
	 * The application driver runs first for every configuration interface and
	 * publishes the cache epoch while the raw descriptor stream is available;
	 * HID mount also requires that same epoch. tuh_mount_cb is only the exact
	 * enum_full_complete fence, so find that owned epoch and publish the bit
	 * under the same transport mutex as the reset claimant set.
	 */
	hid_transport_lock();
	entry = usbhid_usb_device_find(dev_addr);
	if (entry) {
		entry->mount_complete = true;
		published = true;
	}
	hid_transport_unlock();
	if (!published) {
		usbhid_transport_fault(USBHID_FAULT_TOPOLOGY);
		return;
	}

	/*
	 * Previous port:
	 * if (usbhid_reset_device_mounted(entry) ||
	 *     usbhid_usb_device_old_epoch_retiring(entry))
	 * 	usbhid_lifecycle_kick();
	 * else if (usbhid_usb_device_has_waiting_hid(entry))
	 * 	usbhid_usb_device_queue_descriptor(entry);
	 *
	 * TinyUSB invokes this from its host callback owner. Publish the completed
	 * mount state and wake lifecycle, but neither advance reset nor continue
	 * pre-probe from the callback. The lifecycle task owns both state-machine
	 * progress and the guarded device-descriptor usb_control_msg().
	 */
	usbhid_lifecycle_kick();
}

static void usbhid_backend_device_detach(uint8_t dev_addr)
{
	struct usbhid_usb_device *retired;
	int ret;

	retired = usbhid_usb_device_mark_retiring(dev_addr);
	if (!retired)
		return;

	/* Host detach revokes every class publication for this device epoch. */
	usbhid_usb_device_invalidate_hid_publications(retired);
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = NULL;
		bool pinned = false;

		hid_transport_lock();
		hid = usbhid_devices[i];
		if (hid) {
			struct usbhid_device *usbhid = hid->driver_data;

			if (interface_to_usbdev(usbhid->intf) != &retired->dev) {
				hid = NULL;
			} else if (!usbhid_publish_disconnect_locked(hid, &pinned)) {
				hid = NULL;
			}
		}
		hid_transport_unlock();

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
	// OUT/SET payloads have exact enqueue-time ownership in the logical usbhid
	// FIFO. GET receive backing exists only while its CTRL head is active.
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
	// HCD and parser borrower here. The logical usbhid FIFO independently drains
	// its compact request nodes and active GET backing before this point.
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

	mutex_lock(&usbhid->mutex);

	// clear_bit(HID_DISCONNECTED, &usbhid->iofl);
	// Firmware keeps physical disconnect in disconnect_queued and reopens the
	// transport gate below only after restart allocation succeeds.
	hid_transport_lock();
	if (usbhid->disconnect_queued)
		ret = -ENODEV;
	hid_transport_unlock();
	if (ret)
		goto fail;

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
	 * Linux usbhid_stop() is reversible while physical disconnect is not.
	 * Keep the firmware stop gate closed through allocation and reopen only
	 * while this exact interface generation remains physically present.
	 */
	hid_transport_lock();
	if (usbhid->disconnect_queued)
		ret = -ENODEV;
	else
		usbhid->transport_stopping = false;
	hid_transport_unlock();
	if (ret)
		goto fail;

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

	mutex_unlock(&usbhid->mutex);
	return 0;

fail:
	hid_transport_lock();
	usbhid->transport_stopping = true;
	hid_transport_unlock();
	// usb_free_urb(usbhid->urbin);
	// usb_free_urb(usbhid->urbout);
	// usb_free_urb(usbhid->urbctrl);
	// usbhid->urbin = NULL;
	// usbhid->urbout = NULL;
	// usbhid->urbctrl = NULL;
	// TinyUSB owns endpoint objects rather than allocating Linux URBs.
	hid_free_buffers(dev, hid);
	mutex_unlock(&usbhid->mutex);
	return ret;
}

static void usbhid_stop(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	mutex_lock(&usbhid->mutex);

	// usb_kill_urb(usbhid->urbin);
	// usb_kill_urb(usbhid->urbout);
	// usb_kill_urb(usbhid->urbctrl);
	//
	// hid_cancel_delayed_stuff(usbhid);
	// Firmware has no URBs/reset_work object. Stop plus the combined teardown
	// barrier drain the equivalent IN, OUT/control, retry, and clear-halt owners.
	usbhid_report_stop(hid);
	if (hid_async_cancel_device(hid))
		async_msg("ERR: HID_ASYNC_CANCEL_FAIL");
	usbhid_teardown_wait(hid);
	usbhid_report_release(hid);
	// usb_free_urb(usbhid->urbin);
	// usb_free_urb(usbhid->urbctrl);
	// usb_free_urb(usbhid->urbout);
	// usbhid->urbin = NULL;
	// usbhid->urbctrl = NULL;
	// usbhid->urbout = NULL;
	// TinyUSB endpoint ownership was synchronously revoked above.
	hid_free_buffers(hid_to_usb_dev(hid), hid);
	mutex_unlock(&usbhid->mutex);
}

static int hid_set_idle(struct usb_device *dev, int ifnum, int report, int idle)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
		HID_REQ_SET_IDLE, USB_TYPE_CLASS | USB_RECIP_INTERFACE,
		(idle << 8) | report, ifnum, NULL, 0, USB_CTRL_SET_TIMEOUT);
}

static int hid_get_class_descriptor(struct usb_device *dev, int ifnum,
		unsigned char type, void *buf, int size)
{
	int result, retries = 4;

	memset(buf, 0, size);

	do {
		result = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				USB_REQ_GET_DESCRIPTOR, USB_RECIP_INTERFACE | USB_DIR_IN,
				(type << 8), ifnum, buf, size, USB_CTRL_GET_TIMEOUT);
		retries--;
	} while (result < size && retries);
	return result;
}

static int usbhid_open(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	int ret;

	mutex_lock(&usbhid->mutex);

	// set_bit(HID_OPENED, &usbhid->iofl);
	// TinyUSB transport state uses an explicit gate under its task mutex. The
	// non-ALWAYS path starts in RESUMING to retain upstream's initial drain.
	hid_transport_lock();
	if (usbhid->transport_stopping) {
		ret = -ENODEV;
	} else {
		usbhid->report_open_state = hid->quirks & HID_QUIRK_ALWAYS_POLL ?
			USBHID_REPORT_OPEN : USBHID_REPORT_RESUMING;
		ret = 0;
	}
	hid_transport_unlock();
	if (ret)
		goto done;

	/*
	 * Upstream usbhid_open() calls hid_start_in() after hidinput opens the
	 * device. Do the TinyUSB receive submit here, not from probe, so unbound
	 * or ignored devices do not feed reports into hid_input_report().
	 */
	if (hid->quirks & HID_QUIRK_ALWAYS_POLL)
		goto done;

	// res = usb_autopm_get_interface(usbhid->intf);
	// Firmware has no runtime-PM owner; the interface is already active here.
	// usbhid->intf->needs_remote_wakeup = 1;
	// Firmware has no Linux remote-wakeup policy at this boundary.
	// set_bit(HID_RESUME_RUNNING, &usbhid->iofl);
	// The RESUMING state published above gates task-side parser delivery.
	// set_bit(HID_IN_POLLING, &usbhid->iofl);
	// report_start() publishes the corresponding wanted/revision transition.
	// res = hid_start_in(hid);
	// TinyUSB interrupt-IN ownership is task-owned. The bridge waits only until
	// the host owner has made Linux's equivalent first usb_submit_urb() attempt.
	ret = usbhid_report_start(hid);
	if (ret) {
		hid_transport_lock();
		usbhid->report_open_state = USBHID_REPORT_CLOSED;
		hid_transport_unlock();
		goto done;
	}

	// usb_autopm_put_interface(usbhid->intf);
	// No runtime-PM reference was acquired above.
	/*
	 * In case events are generated while nobody was listening,
	 * some are released when the device is re-opened.
	 * Wait 50 msec for the queue to empty before allowing events
	 * to go through hid.
	 */
	// if (res == 0)
	// 	msleep(50);
	// This port names the same result variable ret.
	if (ret == 0)
		msleep(50);

	// clear_bit(HID_RESUME_RUNNING, &usbhid->iofl);
	// Unplug/stop may have closed the interface while the lifecycle task slept.
	hid_transport_lock();
	if (usbhid->report_open_state == USBHID_REPORT_RESUMING)
		usbhid->report_open_state = USBHID_REPORT_OPEN;
	hid_transport_unlock();

done:
	mutex_unlock(&usbhid->mutex);
	return ret;
}

static void usbhid_set_probe_raw_event(struct hid_device *hid, bool enabled)
{
	struct usbhid_device *usbhid = hid->driver_data;

	hid_transport_lock();
	usbhid->probe_raw_event = enabled;
	hid_transport_unlock();
}

static void usbhid_close(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;

	mutex_lock(&usbhid->mutex);

	/*
	 * Make sure we don't restart data acquisition due to
	 * a resumption we no longer care about by avoiding racing
	 * with hid_start_in().
	 */
	// spin_lock_irq(&usbhid->lock);
	// clear_bit(HID_OPENED, &usbhid->iofl);
	// if (!(hid->quirks & HID_QUIRK_ALWAYS_POLL))
	// 	clear_bit(HID_IN_POLLING, &usbhid->iofl);
	// spin_unlock_irq(&usbhid->lock);
	// report_close() publishes the equivalent OPENED/POLLING transition in one
	// transport-mutex scope, without masking interrupts.
	usbhid_report_close(hid);

	// if (!(hid->quirks & HID_QUIRK_ALWAYS_POLL)) {
	// 	hid_cancel_delayed_stuff(usbhid);
	// 	usb_kill_urb(usbhid->urbin);
	// 	usbhid->intf->needs_remote_wakeup = 0;
	// }
	// There is no Linux PM wake flag. The non-ALWAYS report_close() path cancels
	// retry state and fences an armed TinyUSB transfer before returning.

	mutex_unlock(&usbhid->mutex);
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
	struct usb_device *dev = interface_to_usbdev (intf);
	struct usbhid_device *usbhid = hid->driver_data;
	struct hid_descriptor hdesc_storage;
	struct hid_descriptor *hdesc;
	struct hid_class_descriptor *hcdesc;
	__u8 fixed_opt_descriptors_size;
	u32 quirks = 0;
	unsigned long transport_quirks = 0;
	unsigned int rsize = 0;
	char *rdesc;
	int ret;

	quirks = hid_lookup_quirk(hid);

	if (quirks & HID_QUIRK_IGNORE)
		return -ENODEV;

	/*
	 * The exact Wacom 0x5048 and wireless-receiver descriptors contain large
	 * VARIABLE feature reports whose single vendor usage is repeated up to 261
	 * times. The pinned Wacom driver maps only the explicitly declared usage
	 * while retaining every report value and byte for its real control paths.
	 */
	if (hid->vendor == 0x056a &&
	    (hid->product == 0x0084 || hid->product == 0x5048)) {
		quirks |= HID_QUIRK_EXPLICIT_FEATURE_USAGES;
		hid->quirks |= HID_QUIRK_EXPLICIT_FEATURE_USAGES;
		transport_quirks |= HID_QUIRK_EXPLICIT_FEATURE_USAGES;
	}

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

	rdesc = kmalloc(rsize, GFP_KERNEL);
	if (!rdesc)
		return -ENOMEM;

	hid_set_idle(dev, interface->desc.bInterfaceNumber, 0, 0);
	ret = hid_get_class_descriptor(dev, interface->desc.bInterfaceNumber,
				  HID_DT_REPORT, rdesc, rsize);
	if (ret < 0) {
		dbg_hid("reading report descriptor failed\n");
		kfree(rdesc);
		goto err;
	}

	ret = hid_parse_report(hid, rdesc, rsize);
	kfree(rdesc);
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

	// hid_device_probe() recomputes table quirks after parse(); keep
	// transport-added quirks in initial_quirks as well.
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
	hid_transport_lock();
	sync->done = true;
	hid_transport_unlock();
	/* Publishing done releases the stack-owned sync; do not touch it again. */
	xTaskNotifyGive(waiter);
}

static int usbhid_sync_wait(struct usbhid_sync_request *sync, int ret)
{
	bool done;

	if (ret)
		return ret;

	do {
		hid_transport_lock();
		done = sync->done;
		hid_transport_unlock();
		if (!done)
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	} while (!done);
	return sync->status;
}

static bool usbhid_io_get(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool acquired = false;

	hid_transport_lock();
	if (!usbhid->transport_stopping) {
		usbhid->io_pending++;
		acquired = true;
	}
	hid_transport_unlock();
	return acquired;
}

void usbhid_wait_wake_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	struct usbhid_io_waiter *waiter;

	if (!usbhid)
		return;
	/* Linux wake_up_all(): notifications are edges; each waiter owns a predicate. */
	for (waiter = usbhid->io_waiters; waiter; waiter = waiter->next) {
		if (waiter->task)
			(void)xTaskNotifyGive(waiter->task);
	}
}

/*
 * Upstream Linux: USB disconnect wakes protocol wait queues through the
 * device/driver lifetime machinery. Firmware binds the one HID++ waiter to the
 * exact TinyUSB interface generation so callback-side stop can publish an
 * allocation-free cancellation edge before lifecycle teardown.
 */
static struct hid_device *usbhid_protocol_owner(struct hid_device *hid)
{
	/*
	 * Upstream Linux: a DJ child shares its receiver's USB transport.
	 * Firmware resolves that physical owner before accessing usbhid state.
	 */
	if (hid->group == HID_GROUP_LOGITECH_DJ_DEVICE)
		return to_hid_device(hid->dev.parent);
	return hid;
}

int hid_compat_waitqueue_bind(wait_queue_head_t *wait, struct hid_device *hid)
{
	struct usbhid_device *usbhid =
		usbhid_protocol_owner(hid)->driver_data;
	int ret = 0;

	hid_transport_lock();
	if (usbhid->transport_stopping) {
		ret = -ENODEV;
	} else {
		wait->transport_next = usbhid->protocol_waits;
		usbhid->protocol_waits = wait;
	}
	hid_transport_unlock();
	return ret;
}

void hid_compat_waitqueue_unbind(wait_queue_head_t *wait,
				 struct hid_device *hid)
{
	struct usbhid_device *usbhid =
		usbhid_protocol_owner(hid)->driver_data;
	wait_queue_head_t **link;

	hid_transport_lock();
	link = &usbhid->protocol_waits;
	while (*link != wait)
		link = &(*link)->transport_next;
	*link = wait->transport_next;
	wait->transport_next = NULL;
	hid_transport_unlock();
}

void hid_compat_waitqueue_state_lock(wait_queue_head_t *wait)
{
	(void)wait;
	hid_transport_lock();
}

void hid_compat_waitqueue_state_unlock(wait_queue_head_t *wait)
{
	(void)wait;
	hid_transport_unlock();
}

static void usbhid_waiter_link_locked(struct usbhid_device *usbhid,
				     struct usbhid_io_waiter *waiter)
{
	waiter->next = usbhid->io_waiters;
	usbhid->io_waiters = waiter;
}

static void usbhid_waiter_unlink_locked(struct usbhid_device *usbhid,
					struct usbhid_io_waiter *waiter)
{
	struct usbhid_io_waiter **link = &usbhid->io_waiters;

	while (*link != waiter)
		link = &(*link)->next;
	*link = waiter->next;
	waiter->next = NULL;
}

static bool usbhid_waiter_has_other_ordinary_locked(
		const struct usbhid_device *usbhid,
		const struct usbhid_io_waiter *self)
{
	for (const struct usbhid_io_waiter *waiter = usbhid->io_waiters;
	     waiter; waiter = waiter->next) {
		/* Concurrent teardown fences must not wait on one another. */
		if (waiter != self && !waiter->teardown)
			return true;
	}
	return false;
}

void usbhid_io_put(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	bool io_owned;

	hid_transport_lock();
	io_owned = usbhid->io_pending != 0;
	if (!io_owned) {
		hid_transport_unlock();
		/*
		 * HID/device unmount reaches this release from TinyUSB's host
		 * callback owner. Defer its diagnostic to lifecycle and stop before
		 * underflow rather than asserting in the host callback boundary.
		 */
		usbhid_transport_fault(USBHID_FAULT_IO_ACCOUNTING);
		return;
	}
	usbhid->io_pending--;
	// wake_up(&usbhid->wait);
	// The port's aggregate io_pending replaces upstream CTRL/OUT running bits;
	// a PARKED lane makes ordinary idle a nonzero count, so every release is a
	// possible predicate transition. With no linked waiter this is a cheap no-op.
	usbhid_wait_wake_locked(hid);
	hid_transport_unlock();
}

/*
 * Linux usb_interface storage outlives a reversible hid_hw_stop(). The
 * firmware lifecycle owner likewise keeps registry objects alive until its
 * callback returns, but must reject a physical disconnect already published by
 * TinyUSB.
 */
struct hid_device *usbhid_lifecycle_find_hid(const struct usb_device *dev,
					      unsigned int ifnum)
{
	struct hid_device *found = NULL;

	hid_transport_lock();
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid;

		if (!hid)
			continue;
		/* Registry publication follows hid->driver_data = usbhid. */
		usbhid = hid->driver_data;
		if (usbhid->driver_ready &&
		    !usbhid->disconnect_queued &&
		    interface_to_usbdev(usbhid->intf) == dev &&
		    usbhid->intf->cur_altsetting->desc.bInterfaceNumber == ifnum) {
			found = hid;
			break;
		}
	}
	hid_transport_unlock();
	return found;
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

	hid_transport_lock();
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
	hid_transport_unlock();
	return found;
}

void usbhid_ifnum_io_put(struct hid_device *hid)
{
	usbhid_io_put(hid);
}

static struct usbhid_report_request *usbhid_request_head_locked(
		const struct usbhid_device *usbhid, enum usbhid_report_lane lane)
{
	return lane == USBHID_REPORT_LANE_OUT ? usbhid->out_head :
					       usbhid->ctrl_head;
}

static bool usbhid_request_lane_full_locked(
		const struct usbhid_device *usbhid, enum usbhid_report_lane lane)
{
	return lane == USBHID_REPORT_LANE_OUT ?
		usbhid->out_count >= HID_OUTPUT_FIFO_SIZE - 1u :
		usbhid->ctrl_count >= HID_CONTROL_FIFO_SIZE - 1u;
}

static bool usbhid_request_order_before(u32 left, u32 right)
{
	return (int32_t)(left - right) < 0;
}

static int usbhid_request_charge_locked(struct usbhid_device *usbhid,
		enum usbhid_report_lane lane, u16 memory_charge)
{
	if (usbhid->transport_stopping)
		return -ENODEV;
	if (usbhid_request_lane_full_locked(usbhid, lane))
		return -EBUSY;
	if (usbhid_report_queue_bytes >
	    USBHID_REPORT_QUEUE_MEMORY_MAX - memory_charge)
		return -ENOMEM;
	usbhid_report_queue_bytes += memory_charge;
	return 0;
}

static void usbhid_report_charge_release_locked(u16 memory_charge)
{
	usbhid_report_queue_bytes -= memory_charge;
}

static void usbhid_request_append_locked(
		struct usbhid_device *usbhid, struct usbhid_report_request *request)
{
	struct usbhid_report_request *previous_head =
		usbhid_request_head_locked(
			usbhid, (enum usbhid_report_lane)request->lane);
	bool lane_empty;

	lane_empty = usbhid_request_head_locked(
		usbhid, (enum usbhid_report_lane)request->lane) == NULL;
	if (lane_empty)
		hid_async_admission_logical_link_locked(
			&request->admission, usbhid->dev_addr,
			request->device_generation,
			request->lane == USBHID_REPORT_LANE_OUT,
			request->lane == USBHID_REPORT_LANE_OUT ?
				usbhid->usb_altsetting.interrupt_out_endpoint : 0);
	if (request->lane == USBHID_REPORT_LANE_OUT) {
		if (usbhid->out_tail)
			usbhid->out_tail->next = request;
		else
			usbhid->out_head = request;
		usbhid->out_tail = request;
		usbhid->out_count++;
	} else {
		if (usbhid->ctrl_tail)
			usbhid->ctrl_tail->next = request;
		else
			usbhid->ctrl_head = request;
		usbhid->ctrl_tail = request;
		usbhid->ctrl_count++;
	}
	/*
	 * Upstream restarts a stopped CTRL/OUT FIFO when a later report is queued.
	 * A pre-wire TinyUSB submit failure parks the same logical head here; only a
	 * later enqueue on that lane gives it a new admission ticket, avoiding a
	 * task-side retry loop when the host submitter reports a persistent error.
	 */
	if (previous_head &&
	    previous_head->state == USBHID_REPORT_REQUEST_PARKED) {
		hid_async_admission_logical_link_locked(
			&previous_head->admission, usbhid->dev_addr,
			previous_head->device_generation,
			previous_head->lane == USBHID_REPORT_LANE_OUT,
			previous_head->lane == USBHID_REPORT_LANE_OUT ?
				usbhid->usb_altsetting.interrupt_out_endpoint : 0);
		previous_head->state = USBHID_REPORT_REQUEST_QUEUED;
	}
}

static void usbhid_request_pop_locked(struct usbhid_report_request *request)
{
	struct usbhid_device *usbhid = request->hid->driver_data;
	struct usbhid_report_request **head;
	struct usbhid_report_request **tail;

	if (request->lane == USBHID_REPORT_LANE_OUT) {
		head = &usbhid->out_head;
		tail = &usbhid->out_tail;
	} else {
		head = &usbhid->ctrl_head;
		tail = &usbhid->ctrl_tail;
	}

	*head = request->next;
	if (!*head)
		*tail = NULL;
	request->next = NULL;
	if (request->lane == USBHID_REPORT_LANE_OUT)
		usbhid->out_count--;
	else
		usbhid->ctrl_count--;
	hid_async_admission_unlink_locked(&request->admission);
	if (*head)
		hid_async_admission_logical_link_locked(
			&(*head)->admission, usbhid->dev_addr,
			(*head)->device_generation,
			request->lane == USBHID_REPORT_LANE_OUT,
			request->lane == USBHID_REPORT_LANE_OUT ?
				usbhid->usb_altsetting.interrupt_out_endpoint : 0);
}

static bool usbhid_request_runnable_locked(void)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid &&
		    ((usbhid->ctrl_head &&
		      usbhid->ctrl_head->state == USBHID_REPORT_REQUEST_QUEUED) ||
		     (usbhid->out_head &&
		      usbhid->out_head->state == USBHID_REPORT_REQUEST_QUEUED)))
			return true;
	}
	return false;
}

static struct usbhid_report_request *usbhid_request_select_locked(void)
{
	struct usbhid_report_request *selected = NULL;
	const u8 lane_count = HID_HOST_MAX_DEVICES * 2u;

	for (u8 index = 0; index < lane_count; index++) {
		struct hid_device *hid = usbhid_devices[index / 2u];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
		enum usbhid_report_lane lane = index & 1u ?
			USBHID_REPORT_LANE_OUT : USBHID_REPORT_LANE_CTRL;
		struct usbhid_report_request *request = usbhid ?
			usbhid_request_head_locked(usbhid, lane) : NULL;

		if (!request || request->state != USBHID_REPORT_REQUEST_QUEUED ||
		    (!request->cancel_requested && !usbhid->transport_stopping &&
		     !hid_async_admission_can_enter_locked(&request->admission)) ||
		    (selected && !usbhid_request_order_before(
			    request->admission.order,
			    selected->admission.order)))
			continue;
		selected = request;
	}
	return selected;
}

static void usbhid_request_destroy(struct usbhid_report_request *request)
{
	u16 memory_charge;

	if (!request)
		return;
	memory_charge = request->memory_charge;
	kfree(request->input);
	kfree(request);
	/* Keep the conservative budget charged until both heap blocks are gone. */
	hid_transport_lock();
	usbhid_report_charge_release_locked(memory_charge);
	hid_transport_unlock();
}

static int usbhid_request_prepare_input(
		struct usbhid_report_request *request)
{
	struct usbhid_device *usbhid = request->hid->driver_data;
	struct usb_device *dev = hid_to_usb_dev(request->hid);
	struct usbhid_control_input *input;
	u32 maxpacket = dev->descriptor.bMaxPacketSize0;
	u32 report_len = hid_report_len(request->report);
	u32 transfer_size;
	u32 bufsize;
	size_t allocation_size;
	size_t memory_charge;
	bool attach;
	bool accounted;

	if (request->input)
		return 0;
	if (!maxpacket)
		maxpacket = 8;
	transfer_size = report_len + !report_len;
	transfer_size = DIV_ROUND_UP(transfer_size, maxpacket) * maxpacket;
	transfer_size = min_t(u32, transfer_size, usbhid->bufsize);
	bufsize = report_len + 7 + (request->report->id == 0);
	bufsize = max(bufsize, transfer_size);
	if (bufsize > UINT16_MAX)
		return -EMSGSIZE;
	allocation_size = sizeof(*input) + bufsize;
	memory_charge = USBHID_REPORT_HEAP_CHARGE(allocation_size);
	if (memory_charge > UINT16_MAX ||
	    memory_charge > USBHID_REPORT_QUEUE_MEMORY_MAX)
		return -ENOSPC;

	/* Reserve the complete heap charge before allocating outside the mutex. */
	hid_transport_lock();
	attach = request->state == USBHID_REPORT_REQUEST_SUBMITTING &&
		 !request->cancel_requested && !usbhid->transport_stopping;
	accounted = attach && usbhid_report_queue_bytes <=
		USBHID_REPORT_QUEUE_MEMORY_MAX - memory_charge;
	if (accounted) {
		request->memory_charge += (u16)memory_charge;
		usbhid_report_queue_bytes += memory_charge;
	}
	hid_transport_unlock();
	if (!attach)
		return -ENODEV;
	if (!accounted)
		return -ENOSPC;

	input = kzalloc(allocation_size, GFP_KERNEL);
	if (!input) {
		hid_transport_lock();
		request->memory_charge -= (u16)memory_charge;
		usbhid_report_queue_bytes -= memory_charge;
		hid_transport_unlock();
		return -ENOMEM;
	}
	input->request = request;
	input->bufsize = (u16)bufsize;

	hid_transport_lock();
	attach = request->state == USBHID_REPORT_REQUEST_SUBMITTING &&
		 !request->cancel_requested && !usbhid->transport_stopping;
	if (attach) {
		/* Timeout abandonment and publication share request->parser_owner. */
		request->input = input;
	}
	hid_transport_unlock();
	if (!attach) {
		kfree(input);
		/* The allocation is gone before its conservative charge is reusable. */
		hid_transport_lock();
		request->memory_charge -= (u16)memory_charge;
		usbhid_report_queue_bytes -= memory_charge;
		hid_transport_unlock();
		return -ENODEV;
	}
	return 0;
}

void usbhid_request_capacity_available_locked(void)
{
	/* The queued heads are the predicate; this coalesced bit is only a kick. */
	usbhid_report_queue_kick_pending = true;
}

void usbhid_request_cancel_device_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;
	struct usbhid_report_request *request;

	if (!usbhid)
		return;
	for (request = usbhid->ctrl_head; request; request = request->next) {
		request->cancel_requested = true;
		if (request->state == USBHID_REPORT_REQUEST_PARKED)
			request->state = USBHID_REPORT_REQUEST_QUEUED;
	}
	for (request = usbhid->out_head; request; request = request->next) {
		request->cancel_requested = true;
		if (request->state == USBHID_REPORT_REQUEST_PARKED)
			request->state = USBHID_REPORT_REQUEST_QUEUED;
	}
	usbhid_report_queue_kick_pending = true;
}

void usbhid_request_cancel_dev_addr_locked(u8 dev_addr)
{
	for (size_t i = 0; i < HID_HOST_MAX_DEVICES; i++) {
		struct hid_device *hid = usbhid_devices[i];
		struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

		if (usbhid && usbhid->dev_addr == dev_addr)
			usbhid_request_cancel_device_locked(hid);
	}
}

bool usbhid_request_queue_idle_locked(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid ? hid->driver_data : NULL;

	return !usbhid || (!usbhid->ctrl_head && !usbhid->out_head);
}

bool usbhid_request_process(void)
{
	struct usbhid_report_request *request;
	struct usbhid_device *usbhid;
	u8 *data;
	bool retired = false;
	bool wake_again = false;
	bool logical_changed = false;
	int ret;

	hid_transport_lock();
	if (!usbhid_report_queue_kick_pending) {
		hid_transport_unlock();
		return false;
	}
	usbhid_report_queue_kick_pending = false;
	request = usbhid_request_select_locked();
	if (!request) {
		hid_transport_unlock();
		return false;
	}
	usbhid = request->hid->driver_data;
	if (request->cancel_requested || usbhid->transport_stopping) {
		struct hid_device *hid = request->hid;

		usbhid_request_pop_locked(request);
		wake_again = usbhid_request_runnable_locked();
		usbhid_report_queue_kick_pending |= wake_again;
		hid_transport_unlock();
		usbhid_request_destroy(request);
		usbhid_io_put(hid);
		hid_async_logical_queue_changed();
		return true;
	}
	request->state = USBHID_REPORT_REQUEST_SUBMITTING;
	hid_transport_unlock();

	if (request->reqtype == HID_REQ_GET_REPORT)
		ret = usbhid_request_prepare_input(request);
	else
		ret = 0;
	if (!ret) {
		data = request->reqtype == HID_REQ_GET_REPORT ?
			request->input->data : request->data;
		ret = hid_async_queue_report(request->hid, request->report,
					     (enum hid_class_request)request->reqtype,
					     request->device_generation, data,
					     &request->admission,
					     usbhid_request_complete, request);
	}

	hid_transport_lock();
	usbhid = request->hid->driver_data;
	if (!ret) {
		request->state = USBHID_REPORT_REQUEST_ACTIVE;
		logical_changed = true;
	} else if (ret == -EBUSY && !request->cancel_requested &&
		   !usbhid->transport_stopping) {
		request->state = USBHID_REPORT_REQUEST_QUEUED;
	} else {
		usbhid_request_pop_locked(request);
		retired = true;
		logical_changed = true;
	}
	if (!ret || retired) {
		wake_again = usbhid_request_runnable_locked();
		usbhid_report_queue_kick_pending |= wake_again;
	}
	hid_transport_unlock();
	if (logical_changed)
		hid_async_logical_queue_changed();

	if (retired) {
		struct hid_device *hid = request->hid;

		if (ret != -ENODEV)
			async_msg(ret == -ENOSPC ? "WARN: HID_REPORT_QUEUE_MEMORY" :
				  ret == -ENOMEM ? "ERR: HID_REPORT_NOMEM" :
						     "ERR: HID_ASYNC_REQ_FAIL");
		usbhid_request_destroy(request);
		usbhid_io_put(hid);
	}
	return true;
}

static void usbhid_teardown_wait(struct hid_device *hid)
{
	int ret;

	/*
	 * Upstream usb_kill_urb() synchronously fences IN, OUT, and CTRL ownership.
	 * TinyUSB has no killable URB object, so one exact-interface wait combines
	 * the async slot, direct-IN owner, and aggregate I/O predicates after their
	 * producers have stopped. Every last-owner transition supplies a wake edge.
	 */
	ret = usbhid_wait_transport(hid, true);
	if (ret)
		async_msg("ERR: HID_TEARDOWN_WAIT");
}

static void usbhid_control_report_done(struct hid_device *hid, void *context,
					       int status)
{
	struct usbhid_control_input *input = context;
	struct usbhid_report_request *request = input ? input->request : NULL;
	u32 serial = input ? input->async_serial : 0;
	bool wake_again = false;

	if (status < 0 && status != -ENODEV)
		async_msg("ERR: HID_CTRL_PARSE_FAIL");
	/*
	 * TinyUSB adapter: the async executor retains this request slot across
	 * Linux-shaped control completion until the parser consumer releases it.
	 * Release while io_pending still keeps hid and its transport state alive.
	 */
	if (serial)
		hid_async_control_report_release(hid, serial);
	hid_transport_lock();
	usbhid_request_pop_locked(request);
	wake_again = usbhid_request_runnable_locked();
	usbhid_report_queue_kick_pending |= wake_again;
	hid_transport_unlock();
	usbhid_request_destroy(request);
	usbhid_io_put(hid);
	hid_async_logical_queue_changed();
}

/* Caller holds hid_transport_lock(); return a published completion, if any. */
static struct usbhid_control_input *usbhid_control_input_abandon_locked(
		struct usbhid_device *usbhid, TaskHandle_t owner)
{
	struct usbhid_control_input *input = usbhid->owned_control_input;
	struct usbhid_report_request *request;

	if (input && input->request &&
	    (!owner || input->request->parser_owner == owner)) {
		usbhid->owned_control_input = NULL;
	} else {
		input = NULL;
	}
	for (request = usbhid->ctrl_head; request; request = request->next) {
		if (!request->parser_owner ||
		    (owner && request->parser_owner != owner))
			continue;
		request->parser_owner = NULL;
	}
	return input;
}

/*
 * A pre-wire failure clears upstream's CTRL_RUNNING state while preserving its
 * FIFO head for a later enqueue/restart. If hid_hw_wait() therefore returns,
 * its task can no longer be the synchronous parser owner of any GET left in
 * that stopped lane. Clear the owner under the same lock as the idle decision
 * so a concurrent same-lane enqueue cannot restart it with a stale task handle.
 */
static void usbhid_control_parked_owner_abandon_locked(
		struct usbhid_device *usbhid, TaskHandle_t owner)
{
	struct usbhid_report_request *request;

	if (!owner || !usbhid->ctrl_head ||
	    usbhid->ctrl_head->state != USBHID_REPORT_REQUEST_PARKED)
		return;
	for (request = usbhid->ctrl_head; request; request = request->next) {
		if (request->parser_owner != owner)
			continue;
		request->parser_owner = NULL;
	}
}

static void usbhid_request_complete(const struct hid_async_request *req,
					    int status)
{
	struct usbhid_report_request *request = req->context;
	struct usbhid_control_input *input = request->input;
	struct hid_device *hid = request->hid;
	bool wake_again = false;
	bool parked = false;
	bool publish_owned = false;
	u16 len;
	int ret;

	if (status < 0) {
		if (status != -ENODEV)
			async_msg("ERR: HID_ASYNC_REQ_FAIL");
		if (status != -ENODEV && !req->wire_started) {
			/*
			 * Upstream hid_submit_ctrl()/hid_submit_out() leaves the FIFO tail
			 * unchanged when usb_submit_urb() rejects the head. Keep this exact
			 * request and retry only when a later enqueue restarts its lane.
			 */
			hid_transport_lock();
			if (!request->cancel_requested &&
			    !((struct usbhid_device *)hid->driver_data)->transport_stopping) {
				request->state = USBHID_REPORT_REQUEST_PARKED;
				parked = true;
			}
			hid_transport_unlock();
			if (parked)
				return;
			/* A concurrent cancel/stop owns retirement, never a parked head. */
			goto retire;
		}
		goto retire;
	}

	if (req->reqtype != HID_REQ_GET_REPORT)
		goto retire;

	len = min_t(u16, req->actual_len, input->bufsize);
	/*
	 * TinyUSB adapter: the completion-owned input buffer is also the direct EP0
	 * destination and remains owned through hid_ctrl(). Pin this executor slot
	 * before publishing its payload to its parser consumer.
	 */
	ret = hid_async_control_report_hold(req);
	if (ret) {
		if (ret != -ENODEV)
			async_msg("ERR: HID_CTRL_HOLD_FAIL");
		goto retire;
	}
	hid_transport_lock();
	request->state = USBHID_REPORT_REQUEST_PARSING;
	hid_transport_unlock();
	input->async_serial = req->serial;
	input->len = len;
	/*
	 * Upstream hid_ctrl() parses in the URB callback. A probe-time GET in this
	 * port completes on the async task and is routed back to its lifecycle
	 * caller. That caller may consume it either inside hid_hw_wait(), while it
	 * still owns driver_input_lock, or at the outer activation fence after probe
	 * released the lock. Publish the consumer under the same mutex used by
	 * timeout/teardown abandonment, so no task handle can be stranded between
	 * the decision and publication.
	 */
	hid_transport_lock();
	if (((struct usbhid_device *)hid->driver_data)->transport_stopping) {
		ret = -ENODEV;
	} else if (request->parser_owner) {
		struct usbhid_device *usbhid = hid->driver_data;

		usbhid->owned_control_input = input;
		publish_owned = true;
		ret = 0;
		// wake_up(&usbhid->wait);
		// The owner must parse and release this input before the queue is idle.
		usbhid_wait_wake_locked(hid);
	} else {
		ret = 0;
	}
	hid_transport_unlock();
	if (publish_owned)
		return;
	if (!ret)
		ret = usbhid_control_report_submit(req->hid, req->report->type,
						   input->data, input->bufsize,
						   len,
						   usbhid_control_report_done,
						   input);
	if (ret) {
		if (ret != -ENODEV)
			async_msg("ERR: HID_CTRL_DISPATCH_FAIL");
		hid_async_control_report_release(hid,
							 input->async_serial);
		goto retire;
	}
	return;

retire:
	hid_transport_lock();
	usbhid_request_pop_locked(request);
	wake_again = usbhid_request_runnable_locked();
	usbhid_report_queue_kick_pending |= wake_again;
	hid_transport_unlock();
	usbhid_request_destroy(request);
	usbhid_io_put(hid);
	hid_async_logical_queue_changed();
}

/*
 * parser_owner selects the single consumer; it does not keep
 * driver_input_lock held. An inner hid_hw_wait() consumes while its lifecycle
 * task still owns the lock, whereas the outer activation fence may consume
 * after hid_device_probe() released it.
 */
static int usbhid_control_input_parse(struct hid_device *hid,
				      struct usbhid_control_input *input)
{
	enum hid_report_type type =
		(enum hid_report_type)input->request->report->type;

	if (sema_owned_by_current(&hid->driver_input_lock))
		return hid_safe_input_report_locked(
			hid, type, input->data, input->bufsize, input->len, 0);

	return hid_safe_input_report(
		hid, type, input->data, input->bufsize, input->len, 0);
}

/* Consume only the completion published for this recorded parser task. */
static bool usbhid_control_input_process_owned(struct hid_device *hid)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_control_input *input = NULL;
	TaskHandle_t task = xTaskGetCurrentTaskHandle();
	bool process = false;

	hid_transport_lock();
	if (usbhid->owned_control_input &&
	    usbhid->owned_control_input->request &&
	    usbhid->owned_control_input->request->parser_owner == task) {
		input = usbhid->owned_control_input;
		usbhid->owned_control_input = NULL;
		process = !usbhid->transport_stopping;
	}
	hid_transport_unlock();
	if (!input)
		return false;

	usbhid_control_report_done(
		hid, input,
		process ? usbhid_control_input_parse(hid, input) : -ENODEV);
	return true;
}

/*
 * A timed-out hid_hw_wait() must not leave a later GET completion addressed to
 * a stack/lifecycle task which no longer consumes it. Teardown abandons every
 * owner; an ordinary waiter abandons only requests tagged with itself. If the
 * completion won the exact deadline race, return the already-published input
 * so its current owner can finish it before leaving the wait path.
 */
static struct usbhid_control_input *usbhid_control_input_abandon(
		struct hid_device *hid, TaskHandle_t owner, bool *stopping)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_control_input *input;

	hid_transport_lock();
	input = usbhid_control_input_abandon_locked(usbhid, owner);
	*stopping = usbhid->transport_stopping;
	hid_transport_unlock();
	return input;
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
static void usbhid_request(struct hid_device *hid, struct hid_report *report,
				   enum hid_class_request reqtype)
{
	struct usbhid_report_request *request = NULL;
	struct usbhid_device *usbhid;
	enum usbhid_report_lane lane;
	size_t allocation_size;
	size_t memory_charge;
	u32 snapshot_size = 0;
	u32 wire_len;
	u32 generation;
	bool interrupt_out;
	bool request_locked = false;
	bool queue_full = false;
	bool memory_full = false;
	bool charged = false;
	int ret;

	if (reqtype == HID_REQ_GET_REPORT && (hid->quirks & HID_QUIRK_NOGET))
		return;
	if (reqtype != HID_REQ_GET_REPORT && reqtype != HID_REQ_SET_REPORT)
		return;
	if (!usbhid_io_get(hid))
		return;
	usbhid = hid->driver_data;
	ret = hid_async_device_epoch_snapshot(usbhid->dev_addr, &generation);
	if (ret)
		goto fail;

	wire_len = hid_report_len(report);
	if (wire_len > UINT16_MAX) {
		ret = -EMSGSIZE;
		goto fail;
	}
	interrupt_out = reqtype == HID_REQ_SET_REPORT &&
		report->type == HID_OUTPUT_REPORT &&
		usbhid->usb_altsetting.has_interrupt_out &&
		usbhid->usb_altsetting.interrupt_out_endpoint;
	lane = interrupt_out ? USBHID_REPORT_LANE_OUT :
			       USBHID_REPORT_LANE_CTRL;
	if (reqtype == HID_REQ_SET_REPORT)
		snapshot_size = wire_len + 7 + (report->id == 0);
	allocation_size = sizeof(*request) + snapshot_size;
	memory_charge = USBHID_REPORT_HEAP_CHARGE(allocation_size);
	if (allocation_size > UINT16_MAX || memory_charge > UINT16_MAX) {
		ret = -EMSGSIZE;
		goto fail;
	}
	if (memory_charge > USBHID_REPORT_QUEUE_MEMORY_MAX) {
		memory_full = true;
		ret = -ENOMEM;
		goto fail;
	}

	/*
	 * Upstream holds usbhid->lock across FIFO allocation, report snapshot, and
	 * insertion. Allocation cannot run under the shared transport mutex here, so
	 * one task-context PI mutex preserves that construction order without
	 * masking callbacks or interrupts.
	 */
	(void)xSemaphoreTake(usbhid_request_mutex, portMAX_DELAY);
	request_locked = true;

	/*
	 * The PI mutex serializes every logical enqueue, so the lane count cannot
	 * grow while this allocation is in progress. Charge only the shared heap
	 * budget here; the real FIFO node receives its admission order when it is
	 * published below.
	 */
	hid_transport_lock();
	ret = usbhid_request_charge_locked(usbhid, lane,
					   (u16)memory_charge);
	hid_transport_unlock();
	if (ret) {
		queue_full = ret == -EBUSY;
		memory_full = ret == -ENOMEM;
		goto fail;
	}
	charged = true;
	request = kzalloc(allocation_size, GFP_KERNEL);
	if (!request) {
		ret = -ENOMEM;
		goto fail;
	}
	request->hid = hid;
	request->report = report;
	request->device_generation = generation;
	request->memory_charge = (u16)memory_charge;
	charged = false;
	request->wire_len = (u16)wire_len;
	request->reqtype = (u8)reqtype;
	request->lane = (u8)lane;
	request->state = USBHID_REPORT_REQUEST_QUEUED;
	if (reqtype == HID_REQ_GET_REPORT &&
	    sema_owned_by_current(&hid->driver_input_lock))
		request->parser_owner = xTaskGetCurrentTaskHandle();
	if (reqtype == HID_REQ_SET_REPORT) {
		/*
		 * usbhid->ctrl[usbhid->ctrlhead].raw_report =
		 * 	hid_alloc_report_buf(report, GFP_ATOMIC);
		 * hid_output_report(report,
		 * 		   usbhid->ctrl[usbhid->ctrlhead].raw_report);
		 * The firmware combines the compact FIFO node and raw_report backing
		 * into one allocation, reducing heap metadata and fragmentation while
		 * preserving the exact upstream enqueue-time snapshot.
		 */
		hid_output_report(report, request->data);
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
	 * TinyUSB adapter: retain upstream's per-interface logical FIFO, but expose
	 * only its current CTRL and OUT heads to the bounded physical executor. A
	 * GET allocates receive backing only when it reaches the head, then keeps
	 * that lane active through parser completion. The existing io_pending lease
	 * covers both stages, so hid_hw_wait() observes the complete FIFO drain.
	 */
	hid_transport_lock();
	if (usbhid->transport_stopping) {
		ret = -ENODEV;
	} else {
		usbhid_request_append_locked(usbhid, request);
		ret = 0;
		usbhid_report_queue_kick_pending = true;
	}
	hid_transport_unlock();
	(void)xSemaphoreGive(usbhid_request_mutex);
	request_locked = false;
	if (!ret) {
		hid_async_report_queue_kick();
		return;
	}

fail:
	/* A charge belongs either to this stack frame or to the allocated node. */
	if (charged) {
		hid_transport_lock();
		usbhid_report_charge_release_locked((u16)memory_charge);
		hid_transport_unlock();
	}
	if (request_locked)
		(void)xSemaphoreGive(usbhid_request_mutex);
	if (request)
		usbhid_request_destroy(request);
	if (queue_full)
		async_msg(lane == USBHID_REPORT_LANE_OUT ?
			  "WARN: HID_OUT_QUEUE_FULL" : "WARN: HID_CTRL_QUEUE_FULL");
	else if (memory_full)
		async_msg("WARN: HID_REPORT_QUEUE_MEMORY");
	else if (ret != -ENODEV)
		async_msg(ret == -ENOMEM ? "ERR: HID_REPORT_NOMEM" :
						     "ERR: HID_ASYNC_REQ_FAIL");
	usbhid_io_put(hid);
}

// static int usbhid_wait_io(struct hid_device *hid)
// {
// 	struct usbhid_device *usbhid = hid->driver_data;
//
// 	if (!wait_event_timeout(usbhid->wait,
// 				(!test_bit(HID_CTRL_RUNNING, &usbhid->iofl) &&
// 				!test_bit(HID_OUT_RUNNING, &usbhid->iofl)),
// 					10*HZ)) {
// 		dbg_hid("timeout waiting for ctrl or out queue to clear\n");
// 		return -1;
// 	}
//
// 	return 0;
// }
/*
 * TinyUSB adapter: io_pending is the stronger firmware I/O/lifetime lease,
 * including parser completion. Preserve upstream's outer 10*HZ bound for
 * hid_hw_wait(); teardown itself remains an unbounded lifetime fence like
 * usb_kill_urb(). A GET queued while probe owns driver_input_lock cannot run on
 * the report task, so its matching lifecycle waiter consumes that completion
 * directly, either before or after probe releases the lock. The intrusive
 * waiter list is the firmware waitqueue; task notifications carry only
 * coalesced wake edges.
 */
static bool usbhid_wait_transport_idle(struct hid_device *hid, bool teardown,
				       TaskHandle_t owner)
{
	struct usbhid_device *usbhid = hid->driver_data;
	u32 parked_leases = 0;
	bool idle;

	hid_transport_lock();
	if (!teardown) {
		/*
		 * Upstream wait_io() waits for *_RUNNING, not for a FIFO which
		 * usb_submit_urb() rejected before reaching the wire. Keep those
		 * logical requests alive for a later same-lane enqueue/restart, but
		 * exclude the whole stopped lane from this ordinary wait predicate.
		 */
		if (usbhid->ctrl_head &&
		    usbhid->ctrl_head->state == USBHID_REPORT_REQUEST_PARKED)
			parked_leases += usbhid->ctrl_count;
		if (usbhid->out_head &&
		    usbhid->out_head->state == USBHID_REPORT_REQUEST_PARKED)
			parked_leases += usbhid->out_count;
	}
	idle = usbhid->io_pending == parked_leases;
	if (idle && !teardown)
		usbhid_control_parked_owner_abandon_locked(usbhid, owner);
	hid_transport_unlock();
	if (!idle)
		return false;
	if (!teardown)
		return true;
	return hid_async_device_idle(hid) && usbhid_report_idle(hid);
}

static int usbhid_wait_transport(struct hid_device *hid, bool teardown)
{
	struct usbhid_device *usbhid = hid->driver_data;
	struct usbhid_io_waiter waiter = {
		.task = xTaskGetCurrentTaskHandle(),
		.teardown = teardown,
	};
	TickType_t started = xTaskGetTickCount();
	TickType_t timeout = pdMS_TO_TICKS(10000u);
	bool stopping;
	bool timed_out = false;

	/* Register before the first predicate test, exactly like prepare_to_wait(). */
	hid_transport_lock();
	usbhid_waiter_link_locked(usbhid, &waiter);
	hid_transport_unlock();

	for (;;) {
		struct usbhid_control_input *abandoned;
		TickType_t elapsed;
		TickType_t wait_ticks = portMAX_DELAY;
		bool other_waiter = false;

		if (teardown) {
			abandoned = usbhid_control_input_abandon(hid, NULL, &stopping);
			if (abandoned) {
				usbhid_control_report_done(hid, abandoned, -ENODEV);
				continue;
			}
		}
		if (usbhid_control_input_process_owned(hid))
			continue;
		if (usbhid_wait_transport_idle(hid, teardown, waiter.task)) {
			if (!teardown)
				break;
			hid_transport_lock();
				other_waiter =
					usbhid_waiter_has_other_ordinary_locked(usbhid,
									 &waiter);
			hid_transport_unlock();
			if (!other_waiter)
				break;
		}
		if (!teardown) {
			elapsed = xTaskGetTickCount() - started;
			if (elapsed >= timeout) {
				abandoned = usbhid_control_input_abandon(
					hid, waiter.task, &stopping);
				if (abandoned) {
					if (stopping) {
						usbhid_control_report_done(
							hid, abandoned, -ENODEV);
					} else {
						usbhid_control_report_done(
							hid, abandoned,
							usbhid_control_input_parse(
								hid,
								abandoned));
					}
					continue;
				}
				timed_out = true;
				break;
			}
			wait_ticks = timeout - elapsed;
		}
		(void)ulTaskNotifyTake(pdTRUE, wait_ticks);
	}

	hid_transport_lock();
	usbhid_waiter_unlink_locked(usbhid, &waiter);
	stopping = usbhid->transport_stopping;
	/* A teardown waiter may be sleeping on the last ordinary waiter leaving. */
	usbhid_wait_wake_locked(hid);
	hid_transport_unlock();
	if (timed_out) {
		async_msg("ERR: HID_WAIT_TIMEOUT");
		return -ETIMEDOUT;
	}
	/* transport_stopping is teardown's entry condition, not its result. */
	if (teardown)
		return 0;
	return stopping ? -ENODEV : 0;
}

static int usbhid_wait_io(struct hid_device *hid)
{
	return usbhid_wait_transport(hid, false);
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
	/*
	 * Upstream Linux:
	 * struct usb_ctrlrequest *dr;
	 * int ret;
	 *
	 * dr = kmalloc_obj(struct usb_ctrlrequest, GFP_NOIO);
	 * if (!dr)
	 * 	return -ENOMEM;
	 *
	 * dr->bRequestType = requesttype;
	 * dr->bRequest = request;
	 * dr->wValue = cpu_to_le16(value);
	 * dr->wIndex = cpu_to_le16(index);
	 * dr->wLength = cpu_to_le16(size);
	 *
	 * ret = usb_internal_control_msg(dev, pipe, dr, data, size, timeout);
	 *
	 * if (dev->quirks & USB_QUIRK_DELAY_CTRL_MSG)
	 * 	msleep(200);
	 *
	 * kfree(dr);
	 * return ret;
	 *
	 * This port has neither generic URBs nor Linux DMA setup allocation. The
	 * fixed request slot stores these setup fields, and the task-side bridge
	 * preserves the same synchronous lifetime over TinyUSB's async owner.
	 */
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
	ret = hid_async_wait_queue_usb_control_msg(
		hid_owner, dev_addr, generation, request, requesttype, value,
		index, data, size, timeout, usbhid_sync_complete, &sync);
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
	ret = hid_async_wait_queue_usb_interrupt_out(
		hid_owner, dev_addr, generation, ep_addr, data, (u16)len,
		timeout, usbhid_sync_complete, &sync);
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
	.set_probe_raw_event = usbhid_set_probe_raw_event,
	.max_buffer_size = HID_MAX_BUFFER_SIZE,
};

bool hid_is_usb(const struct hid_device *hdev)
{
	return hdev->ll_driver == &usb_hid_driver;
}
EXPORT_SYMBOL_GPL(hid_is_usb);

void hid_port_usage_cap_drop(struct hid_device *hdev)
{
	(void)hdev;
	async_msg("WARN: HID_USAGE_CAP_DROP");
}
