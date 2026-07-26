/* Firmware-only TinyUSB/FreeRTOS executor for Linux-shaped HID requests. */
#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "FreeRTOS.h"
#include "task.h"
#include "host/hcd.h"
#include "host/hub.h"
#include "host/usbh_pvt.h"

#include "hid_async.h"
#include "hid_transport_sync.h"
#include "stdio_tusb_cdc.h"
#include "usbhid_backend.h"
#include "usbhid_private.h"
#include "usbhid_report.h"

/*
 * Upstream Linux HID transport can block in hid_hw_wait(), usb_control_msg(),
 * and related request paths. TinyUSB host callbacks cannot wait for another
 * TinyUSB callback to complete, so this firmware bridge owns bounded physical-
 * device control/OUT lanes in one executor task and resumes task-side ported
 * call sites through explicit completions.
 * TinyUSB invokes this file's transfer/deferred callbacks from the host owner
 * task, not an ISR. Recoverable callback invariant failures publish a bounded
 * lifecycle fault instead of entering a logger or stopping the host owner.
 */

/* Fixed-pool allocation is a separate local bound before transfer timeout. */
#define HID_ASYNC_ADMISSION_TIMEOUT_TICKS pdMS_TO_TICKS(1000)
/* Match upstream usbhid's five-second watchdog for active ctrl/out I/O. */
#define HID_ASYNC_XFER_TIMEOUT_TICKS pdMS_TO_TICKS(USB_CTRL_SET_TIMEOUT)
#define HID_ASYNC_DEVICE_ADDR_MAX (CFG_TUH_DEVICE_MAX + CFG_TUH_HUB)
#define HID_ASYNC_NORMAL_SLOT_COUNT \
	(HID_ASYNC_REQUEST_QUEUE_LEN + 1u + CFG_TUH_HID)
#define HID_ASYNC_RECOVERY_SLOT_COUNT 1u
#define HID_ASYNC_SLOT_COUNT \
	(HID_ASYNC_NORMAL_SLOT_COUNT + HID_ASYNC_RECOVERY_SLOT_COUNT)
#define HID_ASYNC_ADMISSION_NOTIFY_INDEX 0u
#define HID_ASYNC_NOTIFY_INDEX 1u

_Static_assert(configTASK_NOTIFICATION_ARRAY_ENTRIES > HID_ASYNC_NOTIFY_INDEX,
	       "HID async executor needs notification index 1");
_Static_assert(HID_ASYNC_SLOT_COUNT <= UINT8_MAX,
	       "HID async one-based FIFO links must fit in u8");

enum hid_async_host_action {
	HID_ASYNC_HOST_FENCE,
	HID_ASYNC_HOST_SUBMIT,
	HID_ASYNC_HOST_ABORT,
};

/* Must match the SHA-pinned host-only API in tinyusb-usbh-enum-port.cmake. */
enum hid_async_control_cancel_result {
	HID_ASYNC_CONTROL_CANCEL_BUSY = -2,
	HID_ASYNC_CONTROL_CANCEL_STALE = -1,
	HID_ASYNC_CONTROL_CANCEL_PENDING = 0,
	HID_ASYNC_CONTROL_CANCEL_CALLBACK_DONE = 1,
	HID_ASYNC_CONTROL_CANCEL_ALREADY_IDLE = 2,
};

struct hid_async_host_call {
	TaskHandle_t waiter;
	struct hid_async_request *request;
	u8 dev_addr;
	u8 instance;
	u8 ep_addr;
	u32 generation;
	u32 serial;
	enum hid_async_host_action action;
	int status;
	volatile bool done;
};

enum hid_async_lane {
	HID_ASYNC_LANE_DEVICE_CTRL,
	HID_ASYNC_LANE_DEVICE_OUT,
};

enum hid_async_slot_state {
	HID_ASYNC_SLOT_FREE,
	HID_ASYNC_SLOT_QUEUED,
	HID_ASYNC_SLOT_SUBMIT_PENDING,
	HID_ASYNC_SLOT_ACTIVE,
	HID_ASYNC_SLOT_RETIRING,
	/* Pins HUB_RESET metadata across its immediate host-owner callback. */
	HID_ASYNC_SLOT_HOST_COMPLETING,
	HID_ASYNC_SLOT_COMPLETING,
	HID_ASYNC_SLOT_WAIT_PARSE,
	HID_ASYNC_SLOT_RELEASE_PENDING,
};

enum hid_async_submit_wait {
	HID_ASYNC_SUBMIT_WAIT_NONE,
	HID_ASYNC_SUBMIT_WAIT_CONTROL,
	HID_ASYNC_SUBMIT_WAIT_ENDPOINT,
};

struct hid_async_slot {
	struct hid_async_request req;
	u8 next;
	u8 lane;
	u8 state;
	bool accepting_completion;
	bool completion_ready;
	bool cancel_requested;
	/* True when logical enqueue happened after the global EP0 gate closed. */
	bool gate_parked;
	/* Exact TinyUSB FIFO-prefix cancellation owns the physical EP0 buffer. */
	bool retire_pending;
	int completion_status;
	TickType_t queued_at;
	TickType_t xfer_start;
	u8 submit_wait;
};

/*
 * Device address zero is not a TinyUSB device epoch. Reuse its old generation
 * word for the admission head so this wait queue adds no persistent RAM.
 */
struct hid_async_state {
	struct hid_async_admission_node *admission_head;
	u32 device_generation[HID_ASYNC_DEVICE_ADDR_MAX];
};

_Static_assert(sizeof(struct hid_async_state) ==
	       sizeof(u32) * (HID_ASYNC_DEVICE_ADDR_MAX + 1u),
	       "HID admission head must reuse the address-zero generation word");

static struct hid_async_slot *hid_async_slots;
static TaskHandle_t hid_async_task_handle;
static TaskHandle_t hid_async_host_task_handle;
/* Bounded device-level lanes avoid per-device transport queues. */
static u8 hid_async_device_ctrl_head;
static u8 hid_async_device_ctrl_tail;
static u8 hid_async_device_out_head;
static u8 hid_async_device_out_tail;
static u8 hid_async_ctrl_active;
/* Coordinated remove/re-enumeration temporarily owns TinyUSB's global EP0. */
static bool hid_async_control_gate;
static u8 hid_async_schedule_cursor;
static u32 hid_async_serial;
static u32 hid_async_admission_order;
static struct hid_async_state hid_async_state;

static int hid_async_submit(struct hid_async_request *req);
static void hid_async_xfer_complete(tuh_xfer_t *xfer);
static void hid_async_host_call_sync(struct hid_async_host_call *call);
static void hid_async_notify_task(void);
static void hid_async_admission_wake_all_locked(void);

/* SHA-pinned TinyUSB host-owner extension generated in the build directory. */
int usbh_port_control_cancel_on_host(uint8_t daddr,
				     tuh_xfer_cb_t complete_cb,
				     uintptr_t user_data);
/* -1: gone/invalid, 0: owned, 1: ready; host-owner context only. */
int usbh_port_control_submit_ready_on_host(uint8_t daddr,
					   tuh_xfer_cb_t complete_cb);
int usbh_port_edpt_submit_ready_on_host(uint8_t daddr, uint8_t ep_addr);

static bool hid_async_lane_is_out(enum hid_async_lane lane)
{
	return lane == HID_ASYNC_LANE_DEVICE_OUT;
}

static u32 hid_async_device_generation_locked(u8 dev_addr)
{
	bool address_valid = dev_addr &&
		dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX;

	if (!address_valid)
		async_msg("ERR: HID_ADDR_RANGE");
	configASSERT(address_valid);
	if (!address_valid)
		return 0;
	return hid_async_state.device_generation[dev_addr - 1u];
}

static void hid_async_device_generation_advance_locked(u8 dev_addr)
{
	bool address_valid = dev_addr &&
		dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX;

	if (!address_valid)
		async_msg("ERR: HID_ADDR_RANGE");
	configASSERT(address_valid);
	if (!address_valid)
		return;
	hid_async_state.device_generation[dev_addr - 1u]++;
}

static bool hid_async_request_bypasses_control_gate(
		const struct hid_async_request *req)
{
	return req->kind == HID_ASYNC_REQUEST_HUB_RESET;
}

static bool hid_async_slot_parked_by_control_gate(
		const struct hid_async_slot *slot)
{
	return hid_async_control_gate &&
	       !hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
	       !hid_async_request_bypasses_control_gate(&slot->req);
}

static struct hid_async_slot *hid_async_slot_for_request_locked(
		struct hid_async_request *req)
{
	if (!hid_async_slots || !req)
		return NULL;
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (&hid_async_slots[i].req == req)
			return &hid_async_slots[i];
	}
	return NULL;
}

static void hid_async_call_on_host(void *data)
{
	struct hid_async_host_call *call = data;
	struct hid_async_slot *slot;
	TaskHandle_t waiter = call->waiter;
	bool gated = false;
	bool is_current = false;

	/*
	 * usbh_defer_func() runs this callback in TinyUSB's host owner. Submission
	 * and generation-less abort may therefore touch the host core here. They may
	 * only target the request which asked for this call. A physical
	 * unmount advances the generation first; class/HCD close then owns the old
	 * transfer and an abort must not hit a newly reused USB address.
	 */
	hid_transport_lock();
	slot = hid_async_slot_for_request_locked(call->request);
	if (slot && call->dev_addr &&
	    call->dev_addr <= HID_ASYNC_DEVICE_ADDR_MAX &&
	    slot->req.serial == call->serial &&
	    slot->req.dev_addr == call->dev_addr &&
	    slot->req.instance == call->instance &&
	    slot->req.generation == call->generation &&
	    hid_async_device_generation_locked(call->dev_addr) ==
		call->generation) {
		if (call->action == HID_ASYNC_HOST_SUBMIT)
			is_current = slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING &&
				     !slot->cancel_requested &&
				     (!slot->req.hid ||
				      !((struct usbhid_device *)
					slot->req.hid->driver_data)->transport_stopping);
		else if (call->action == HID_ASYNC_HOST_ABORT)
			is_current = slot->state == HID_ASYNC_SLOT_RETIRING &&
				     !slot->completion_ready;
		if (is_current && call->action == HID_ASYNC_HOST_SUBMIT)
			gated = hid_async_slot_parked_by_control_gate(slot);
	}
	hid_transport_unlock();

	call->status = call->action == HID_ASYNC_HOST_FENCE ? 0 : -ENODEV;
	if (call->action == HID_ASYNC_HOST_SUBMIT) {
		/* A gate which raced the defer parks this request for release. */
		call->status = gated ? -EAGAIN :
			       is_current ? hid_async_submit(call->request) :
					    -ENODEV;
		if (call->status == -EBUSY) {
			bool wait_armed = false;

			/*
			 * An earlier idle edge can be consumed before this deferred call,
			 * then an ordinary TinyUSB completion callback can immediately
			 * acquire the same resource again. Re-arm after the host-owner
			 * busy observation and before returning to its event loop. The
			 * next owner cannot complete before this registration is durable.
			 */
			hid_transport_lock();
			slot = hid_async_slot_for_request_locked(call->request);
			if (slot && slot->req.serial == call->serial &&
			    slot->req.dev_addr == call->dev_addr &&
			    slot->req.instance == call->instance &&
			    slot->req.generation == call->generation &&
			    slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING &&
			    !slot->cancel_requested &&
			    hid_async_device_generation_locked(call->dev_addr) ==
				call->generation) {
				slot->submit_wait = hid_async_lane_is_out(
					(enum hid_async_lane)slot->lane) ?
					HID_ASYNC_SUBMIT_WAIT_ENDPOINT :
					HID_ASYNC_SUBMIT_WAIT_CONTROL;
				wait_armed = true;
			}
			hid_transport_unlock();
			if (!wait_armed)
				call->status = -ENODEV;
		}
	} else if (call->action == HID_ASYNC_HOST_ABORT && is_current) {
		call->status = 0;
		if (call->ep_addr) {
			(void)tuh_edpt_abort_xfer(call->dev_addr, call->ep_addr);
		} else {
			/*
			 * Linux usb_kill_urb() waits for one exact giveback. The pinned host
			 * core provides the equivalent global EP0 cancel transaction: raw
			 * abort success completes now; a lost race retains this callback and
			 * buffer through the exact cancel-time event-queue prefix.
			 */
			call->status = usbh_port_control_cancel_on_host(
				call->dev_addr, hid_async_xfer_complete,
				(uintptr_t)call->serial);

			if (call->status == HID_ASYNC_CONTROL_CANCEL_PENDING ||
			    call->status == HID_ASYNC_CONTROL_CANCEL_BUSY) {
				bool wait_armed = false;

				hid_transport_lock();
				slot = hid_async_slot_for_request_locked(call->request);
				if (slot && slot->req.serial == call->serial &&
				    slot->req.dev_addr == call->dev_addr &&
				    slot->req.instance == call->instance &&
				    slot->req.generation == call->generation &&
				    slot->state == HID_ASYNC_SLOT_RETIRING &&
				    !slot->completion_ready &&
				    hid_async_device_generation_locked(call->dev_addr) ==
					call->generation) {
					if (call->status ==
					    HID_ASYNC_CONTROL_CANCEL_PENDING)
						slot->retire_pending = true;
					else
						slot->submit_wait =
							HID_ASYNC_SUBMIT_WAIT_CONTROL;
					wait_armed = true;
				}
				hid_transport_unlock();
				if (!wait_armed)
					call->status = -ENODEV;
			}
		}
	}
	hid_transport_lock();
	call->done = true;
	hid_transport_unlock();
	/* Publishing done releases the stack-owned call; do not touch it again. */
	xTaskNotifyGive(waiter);
}

static u8 hid_async_tinyusb_report_type(enum hid_report_type type)
{
	return (u8)type + 1;
}

static void hid_async_notify_task(void)
{
	TaskHandle_t task;

	hid_transport_lock();
	task = hid_async_task_handle;
	hid_transport_unlock();
	if (task)
		xTaskNotifyGiveIndexed(task, HID_ASYNC_NOTIFY_INDEX);
}

void hid_async_report_queue_kick(void)
{
	hid_async_notify_task();
}

void hid_async_logical_queue_changed(void)
{
	hid_transport_lock();
	hid_async_admission_wake_all_locked();
	hid_transport_unlock();
	hid_async_notify_task();
}

void hid_async_host_control_ready(void)
{
	bool wake = false;

	/*
	 * Like usbcore waking queued URBs when EP0 retires, the callback publishes
	 * only an edge. The queued slot keeps the durable resource predicate, so a
	 * completion racing the deferred submit return cannot be lost.
	 */
	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		/*
		 * A REMOVE can suppress the exact callback retained by an EP0 cancel
		 * prefix. The global IDLE edge is then its only terminal. Retry through
		 * the host owner once more: that deferred call also fences a normal
		 * completion callback which published IDLE immediately before giveback.
		 */
		if (slot->state == HID_ASYNC_SLOT_RETIRING &&
		    slot->retire_pending) {
			slot->retire_pending = false;
			wake = true;
		}
		if ((slot->state == HID_ASYNC_SLOT_QUEUED ||
		     slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING ||
		     slot->state == HID_ASYNC_SLOT_RETIRING) &&
		    slot->submit_wait == HID_ASYNC_SUBMIT_WAIT_CONTROL) {
			slot->submit_wait = HID_ASYNC_SUBMIT_WAIT_NONE;
			wake = true;
		}
	}
	hid_transport_unlock();
	if (wake)
		hid_async_notify_task();
}

void hid_async_host_endpoint_ready(u8 dev_addr, u8 ep_addr)
{
	bool wake = false;

	/* Endpoint ownership is independent; never wake on unrelated mouse IN. */
	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if ((slot->state == HID_ASYNC_SLOT_QUEUED ||
		     slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING) &&
		    slot->submit_wait == HID_ASYNC_SUBMIT_WAIT_ENDPOINT &&
		    slot->req.dev_addr == dev_addr &&
		    slot->req.ep_addr == ep_addr) {
			slot->submit_wait = HID_ASYNC_SUBMIT_WAIT_NONE;
			wake = true;
		}
	}
	hid_transport_unlock();
	if (wake)
		hid_async_notify_task();
}

void hid_async_host_task_register(void)
{
	/* TinyUSB host callbacks and tuh_task() share this single owner task. */
	hid_transport_lock();
	hid_async_host_task_handle = xTaskGetCurrentTaskHandle();
	hid_transport_unlock();
}

bool hid_async_sync_call_allowed(void)
{
	TaskHandle_t caller;
	TaskHandle_t executor;
	TaskHandle_t host;

	if (xPortIsInsideInterrupt() ||
	    xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
		return false;
	caller = xTaskGetCurrentTaskHandle();

	hid_transport_lock();
	executor = hid_async_task_handle;
	host = hid_async_host_task_handle;
	hid_transport_unlock();

	/* Either owner would wait for work which only that same task can advance. */
	return caller != executor && caller != host;
}

int hid_async_device_epoch_snapshot(u8 dev_addr, u32 *generation)
{
	if (!hid_async_slots || !generation || !dev_addr ||
	    dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	hid_transport_lock();
	*generation = hid_async_device_generation_locked(dev_addr);
	hid_transport_unlock();
	return 0;
}

static struct hid_async_slot *hid_async_slot_from_id(u8 id)
{
	if (!id || id > HID_ASYNC_SLOT_COUNT)
		return NULL;
	return &hid_async_slots[id - 1u];
}

static u8 hid_async_slot_id(const struct hid_async_slot *slot)
{
	return (u8)(slot - hid_async_slots) + 1u;
}

static void hid_async_slot_fifo_locked(struct hid_async_slot *slot,
				       u8 **head, u8 **tail)
{
	bool output_lane;

	if (slot->lane == HID_ASYNC_LANE_DEVICE_CTRL) {
		*head = &hid_async_device_ctrl_head;
		*tail = &hid_async_device_ctrl_tail;
		return;
	}
	output_lane = slot->lane == HID_ASYNC_LANE_DEVICE_OUT;
	if (!output_lane)
		async_msg("ERR: HID_FIFO_LANE");
	configASSERT(output_lane);
	*head = &hid_async_device_out_head;
	*tail = &hid_async_device_out_tail;
}

static u8 hid_async_normal_free_count_locked(void)
{
	u8 count = 0;

	for (u8 i = 0; i < HID_ASYNC_NORMAL_SLOT_COUNT; i++) {
		if (hid_async_slots[i].state == HID_ASYNC_SLOT_FREE)
			count++;
	}
	return count;
}

static bool hid_async_admission_same_key(
		const struct hid_async_admission_node *left,
		const struct hid_async_admission_node *right)
{
	if (left->dev_addr != right->dev_addr ||
	    left->generation != right->generation ||
	    left->output_lane != right->output_lane)
		return false;
	return !left->output_lane || left->ep_addr == right->ep_addr;
}

static bool hid_async_admission_front_locked(
		const struct hid_async_admission_node *node)
{
	const struct hid_async_admission_node *cursor;

	for (cursor = hid_async_state.admission_head;
	     cursor && cursor != node; cursor = cursor->next) {
		if (hid_async_admission_same_key(cursor, node))
			return false;
	}
	return cursor == node;
}

static void hid_async_admission_wake_all_locked(void)
{
	struct hid_async_admission_node *node;

	/* Linux wake_up_all() also publishes only an edge; callers retry state. */
	for (node = hid_async_state.admission_head; node; node = node->next) {
		if (node->task)
			(void)xTaskNotifyIndexed(
				node->task, HID_ASYNC_ADMISSION_NOTIFY_INDEX,
				1u, eSetBits);
	}
}

static void hid_async_normal_capacity_changed_locked(void)
{
	/* Every owner retries its exact endpoint-front predicate after this edge. */
	hid_async_admission_wake_all_locked();
	if (hid_async_normal_free_count_locked()) {
		usbhid_request_capacity_available_locked();
		if (hid_async_task_handle)
			xTaskNotifyGiveIndexed(hid_async_task_handle,
					       HID_ASYNC_NOTIFY_INDEX);
	}
}

static void hid_async_admission_link_locked(
		struct hid_async_admission_node *node)
{
	struct hid_async_admission_node **link =
		&hid_async_state.admission_head;

	while (*link)
		link = &(*link)->next;
	node->next = NULL;
	node->order = ++hid_async_admission_order;
	if (!node->order)
		node->order = ++hid_async_admission_order;
	node->linked = true;
	*link = node;
}

void hid_async_admission_logical_link_locked(
		struct hid_async_admission_node *node, u8 dev_addr,
		u32 generation, bool output_lane, u8 ep_addr)
{
	memset(node, 0, sizeof(*node));
	node->generation = generation;
	node->dev_addr = dev_addr;
	node->ep_addr = output_lane ? ep_addr : 0;
	node->output_lane = output_lane;
	hid_async_admission_link_locked(node);
}

void hid_async_admission_unlink_locked(
		struct hid_async_admission_node *node)
{
	struct hid_async_admission_node **link =
		&hid_async_state.admission_head;
	bool found;

	if (!node->linked)
		return;
	while (*link && *link != node)
		link = &(*link)->next;
	found = *link == node;
	if (!found)
		async_msg("ERR: HID_ADMIT_LINK");
	configASSERT(found);
	if (!found) {
		memset(node, 0, sizeof(*node));
		return;
	}
	*link = node->next;
	memset(node, 0, sizeof(*node));
	hid_async_normal_capacity_changed_locked();
}

bool hid_async_admission_can_enter_locked(
		const struct hid_async_admission_node *node)
{
	const struct hid_async_admission_node *cursor;
	u8 older_front = 0;

	if (!hid_async_admission_front_locked(node))
		return false;
	for (cursor = hid_async_state.admission_head;
	     cursor && cursor != node; cursor = cursor->next) {
		if (hid_async_admission_front_locked(cursor))
			older_front++;
	}
	return cursor == node &&
		hid_async_normal_free_count_locked() > older_front;
}

static int hid_async_slot_queue_locked(const struct hid_async_request *req,
				       enum hid_async_lane lane,
				       struct hid_async_admission_node *admission)
{
	struct hid_async_slot *slot = NULL;
	struct hid_async_slot *tail_slot;
	bool recovery = req->kind == HID_ASYNC_REQUEST_HUB_RESET;
	bool tail_valid;
	bool tail_open;
	bool head_empty;
	u8 first = req->kind == HID_ASYNC_REQUEST_HUB_RESET ?
		HID_ASYNC_NORMAL_SLOT_COUNT : 0;
	u8 end = req->kind == HID_ASYNC_REQUEST_HUB_RESET ?
		HID_ASYNC_SLOT_COUNT : HID_ASYNC_NORMAL_SLOT_COUNT;
	u8 *head;
	u8 *tail;
	u8 id;

	if (!recovery && !hid_async_admission_can_enter_locked(admission))
		return -EBUSY;

	/* The recovery slot cannot be retained by ordinary parser/control work. */
	for (u8 i = first; i < end; i++) {
		if (hid_async_slots[i].state == HID_ASYNC_SLOT_FREE) {
			slot = &hid_async_slots[i];
			break;
		}
	}
	if (!slot)
		return -EBUSY;

	memset(slot, 0, sizeof(*slot));
	slot->req = *req;
	slot->lane = (u8)lane;
	slot->state = HID_ASYNC_SLOT_QUEUED;
	slot->queued_at = xTaskGetTickCount();
	slot->gate_parked = hid_async_control_gate &&
		!hid_async_lane_is_out(lane) &&
		!hid_async_request_bypasses_control_gate(req);
	id = hid_async_slot_id(slot);
	hid_async_slot_fifo_locked(slot, &head, &tail);
	if (*tail) {
		tail_slot = hid_async_slot_from_id(*tail);
		tail_valid = tail_slot != NULL;
		if (!tail_valid)
			async_msg("ERR: HID_FIFO_TAIL");
		configASSERT(tail_valid);
		if (!tail_valid) {
			memset(slot, 0, sizeof(*slot));
			return -EIO;
		}
		tail_open = tail_slot->next == 0;
		if (!tail_open)
			async_msg("ERR: HID_FIFO_CHAIN");
		configASSERT(tail_open);
		if (!tail_open) {
			memset(slot, 0, sizeof(*slot));
			return -EIO;
		}
		tail_slot->next = id;
	} else {
		head_empty = *head == 0;
		if (!head_empty)
			async_msg("ERR: HID_FIFO_HEAD");
		configASSERT(head_empty);
		if (!head_empty) {
			memset(slot, 0, sizeof(*slot));
			return -EIO;
		}
		*head = id;
	}
	*tail = id;
	if (admission)
		hid_async_admission_unlink_locked(admission);
	return 0;
}

static bool hid_async_slot_is_head_locked(struct hid_async_slot *slot)
{
	struct hid_async_slot *previous;
	bool previous_valid;
	bool slot_linked;
	u8 *head;
	u8 *tail;
	u8 cursor;
	u8 id = hid_async_slot_id(slot);

	hid_async_slot_fifo_locked(slot, &head, &tail);
	(void)tail;
	if (slot->lane != HID_ASYNC_LANE_DEVICE_CTRL &&
	    slot->lane != HID_ASYNC_LANE_DEVICE_OUT)
		return *head == id;

	/*
	 * Linux USB core orders URBs per device endpoint, not across unrelated
	 * devices. The fixed pool uses one control list and one OUT list for HID
	 * and generic requests, so skip older entries with a different physical
	 * ordering key instead of imposing a firmware-only global head-of-line
	 * block. A completed GET_REPORT remains stored through hid_ctrl()-shaped
	 * parsing but no longer owns EP0; retain only usbhid's same-interface
	 * control-report order while letting generic or sibling requests pass it.
	 */
	cursor = *head;
	while (cursor && cursor != id) {
		previous = hid_async_slot_from_id(cursor);
		previous_valid = previous != NULL;
		if (!previous_valid)
			async_msg("ERR: HID_FIFO_CHAIN");
		configASSERT(previous_valid);
		if (!previous_valid)
			return false;
		if (previous->state == HID_ASYNC_SLOT_WAIT_PARSE ||
		    previous->state == HID_ASYNC_SLOT_RELEASE_PENDING) {
			if (previous->state == HID_ASYNC_SLOT_WAIT_PARSE &&
			    slot->req.kind == HID_ASYNC_REQUEST_REPORT &&
			    previous->req.kind == HID_ASYNC_REQUEST_REPORT &&
			    previous->req.hid == slot->req.hid)
				return false;
			cursor = previous->next;
			continue;
		}
		/* HUB_RESET owns EP0 while older normal control work is gated. */
		if (hid_async_request_bypasses_control_gate(&slot->req) &&
		    hid_async_slot_parked_by_control_gate(previous)) {
			cursor = previous->next;
			continue;
		}
		if (previous->req.dev_addr == slot->req.dev_addr &&
		    previous->req.generation == slot->req.generation &&
		    (slot->lane == HID_ASYNC_LANE_DEVICE_CTRL ||
		     previous->req.ep_addr == slot->req.ep_addr))
			return false;
		cursor = previous->next;
	}
	slot_linked = cursor == id;
	if (!slot_linked)
		async_msg("ERR: HID_FIFO_LINK");
	configASSERT(slot_linked);
	return slot_linked;
}

static void hid_async_slot_release_locked(struct hid_async_slot *slot)
{
	struct hid_async_slot *previous_slot = NULL;
	bool fifo_balanced;
	bool previous_valid;
	bool slot_linked;
	u8 *head;
	u8 *tail;
	u8 previous = 0;
	u8 cursor;
	u8 id = hid_async_slot_id(slot);

	/*
	 * Upstream USB core wakes usb_kill_urb() from the final URB giveback.
	 * This fixed-slot release is the equivalent lifetime edge: wake every
	 * interface predicate owner before dropping the slot's last HID pointer.
	 */
	if (slot->req.hid)
		usbhid_wait_wake_locked(slot->req.hid);
	hid_async_slot_fifo_locked(slot, &head, &tail);
	cursor = *head;
	while (cursor && cursor != id) {
		previous = cursor;
		previous_slot = hid_async_slot_from_id(cursor);
		previous_valid = previous_slot != NULL;
		if (!previous_valid)
			async_msg("ERR: HID_FIFO_CHAIN");
		configASSERT(previous_valid);
		if (!previous_valid)
			return;
		cursor = previous_slot->next;
	}
	slot_linked = cursor == id;
	if (!slot_linked)
		async_msg("ERR: HID_FIFO_RELEASE");
	configASSERT(slot_linked);
	if (!slot_linked)
		return;
	if (previous_slot)
		previous_slot->next = slot->next;
	else
		*head = slot->next;
	if (*tail == id)
		*tail = previous;
	fifo_balanced = (*head == 0) == (*tail == 0);
	if (!fifo_balanced)
		async_msg("ERR: HID_FIFO_BALANCE");
	configASSERT(fifo_balanced);
	memset(slot, 0, sizeof(*slot));
	if (id <= HID_ASYNC_NORMAL_SLOT_COUNT)
		hid_async_normal_capacity_changed_locked();
}

int hid_async_init(void)
{
	size_t slots_size = sizeof(*hid_async_slots) * HID_ASYNC_SLOT_COUNT;

	/*
	 * Linux USB core orders transfers by physical endpoint. One shared fixed
	 * pool owns only promoted physical heads. Its control and OUT lists use per-
	 * device/per-endpoint scans; the logical per-HID FIFO in usbhid owns queued
	 * SET_REPORT snapshots. Lifecycle owns descriptor scratch outside this task.
	 */
	hid_async_slots = pvPortMalloc(slots_size);
	if (!hid_async_slots)
		return -ENOMEM;
	if (!hid_transport_sync_init()) {
		vPortFree(hid_async_slots);
		hid_async_slots = NULL;
		return -ENOMEM;
	}
	memset(hid_async_slots, 0, slots_size);
	hid_async_schedule_cursor = HID_ASYNC_SLOT_COUNT - 1u;
	hid_async_control_gate = false;

	return 0;
}

/*
 * report_stop() publishes usbhid->transport_stopping under the same transport
 * mutex. Therefore a request is either fully queued before teardown starts
 * or rejected after it; cancellation plus the durable slot scan drains the
 * former set.
 */
static int hid_async_queue_hid_request(struct hid_async_request *req,
				       u32 generation,
				       struct hid_async_admission_node *admission)
{
	struct usbhid_device *usbhid;
	enum hid_async_lane lane;
	int ret = 0;

	if (!hid_async_slots)
		return -ENODEV;

	hid_transport_lock();
	if (!req->hid)
		ret = -ENODEV;
	else {
		usbhid = req->hid->driver_data;
		if (usbhid->transport_stopping)
			ret = -ENODEV;
		else if (!req->dev_addr ||
			 req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
			ret = -ENODEV;
		else if (generation !=
			 hid_async_device_generation_locked(req->dev_addr)) {
			ret = -ENODEV;
		} else {
			req->generation = generation;
			/*
			 * Linux USB core orders all submissions to the same physical
			 * endpoint, regardless of which HID hook created them.
			 */
			lane = req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT ?
				HID_ASYNC_LANE_DEVICE_OUT :
				HID_ASYNC_LANE_DEVICE_CTRL;
				ret = hid_async_slot_queue_locked(req, lane, admission);
		}
	}
	hid_transport_unlock();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

int hid_async_queue_report(struct hid_device *hid, struct hid_report *report,
			   enum hid_class_request reqtype,
			   u32 generation,
			   u8 *data,
			   struct hid_async_admission_node *admission,
			   hid_async_complete_t complete, void *context)
{
	struct usb_device *dev;
	struct usbhid_device *usbhid;
	struct hid_async_request req;
	bool interrupt_out;
	u32 maxpacket;
	u32 len;
	int ret;

	len = hid_report_len(report);
	dev = hid_to_usb_dev(hid);
	usbhid = hid->driver_data;
	if (reqtype == HID_REQ_GET_REPORT) {
		/* Match upstream hid_submit_ctrl() EP0 receive sizing. */
		maxpacket = dev->descriptor.bMaxPacketSize0;
		if (!maxpacket)
			maxpacket = 8;
		len += !len;
		len = DIV_ROUND_UP(len, maxpacket) * maxpacket;
		len = min_t(u32, len, usbhid->bufsize);
	}

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_REPORT;
	req.hid = hid;
	req.report = report;
	req.reqtype = reqtype;
	req.dev_addr = usbhid->dev_addr;
	req.instance = usbhid->instance;
	req.report_id = report->id;
	req.report_type = hid_async_tinyusb_report_type(report->type);
	req.len = (u16)len;
	req.data = data;
	req.complete = complete;
	req.context = context;

	interrupt_out = reqtype == HID_REQ_SET_REPORT &&
			report->type == HID_OUTPUT_REPORT &&
			usbhid->usb_altsetting.has_interrupt_out &&
			usbhid->usb_altsetting.interrupt_out_endpoint;

	if (reqtype == HID_REQ_SET_REPORT) {
		/*
		 * usbhid->ctrl/out[].raw_report = hid_alloc_report_buf(report,
		 *                                                    GFP_ATOMIC);
		 * hid_output_report(report, usbhid->ctrl/out[].raw_report);
		 * The compact logical usbhid FIFO already owns that exact enqueue-time
		 * snapshot. The physical slot borrows it only while this head is active.
		 */
		if (interrupt_out) {
			/* hid_output_report() already produced the endpoint wire image. */
			req.kind = HID_ASYNC_REQUEST_OUTPUT_REPORT;
			req.ep_addr =
				usbhid->usb_altsetting.interrupt_out_endpoint;
		}
	}

	ret = hid_async_queue_hid_request(&req, generation, admission);
	if (ret)
		return ret;

	if (reqtype == HID_REQ_SET_REPORT)
		async_msg(interrupt_out ? "DBG: HID_REPORT_OUT_Q" :
					  "DBG: HID_REPORT_SET_Q");
	return 0;
}

/*
 * Upstream Linux USB core owns control and per-endpoint queues while task
 * callers sleep. The firmware captures the address epoch before enqueue,
 * retains the caller-owned buffer pointer, and wakes the caller after
 * task-context completion; neither TinyUSB callbacks nor the executor itself
 * may wait here.
 * A matching HID owner is non-owning slot metadata. Synchronous callers hold
 * an io_pending lease; report recovery uses its report-lifecycle barrier.
 * Teardown publishes transport_stopping and drains every owned slot before
 * destroying either object, while the owner tag makes interface stop cancel
 * work immediately instead of leaving it to the physical-device timeout.
 */
static int hid_async_queue_usb_request(struct hid_async_request *req,
				       u32 generation,
				       enum hid_async_lane lane,
				       struct hid_async_admission_node *admission)
{
	struct usbhid_device *usbhid;
	bool recovery = req->kind == HID_ASYNC_REQUEST_HUB_RESET;
	int ret;

	if (!hid_async_slots || !req->dev_addr ||
	    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	hid_transport_lock();
	if (generation != hid_async_device_generation_locked(req->dev_addr)) {
		ret = -ENODEV;
	} else if (req->hid) {
		usbhid = req->hid->driver_data;
		if (usbhid->transport_stopping ||
		    usbhid->dev_addr != req->dev_addr) {
			ret = -ENODEV;
		} else {
			req->instance = usbhid->instance;
			req->generation = generation;
			ret = 0;
		}
	} else {
		req->generation = generation;
		ret = 0;
	}
	if (!ret && !recovery && !admission->linked) {
		TaskHandle_t task = admission->task;

		memset(admission, 0, sizeof(*admission));
		admission->task = task;
		admission->generation = generation;
		admission->dev_addr = req->dev_addr;
		admission->ep_addr = hid_async_lane_is_out(lane) ?
			req->ep_addr : 0;
		admission->output_lane = hid_async_lane_is_out(lane);
		hid_async_admission_link_locked(admission);
	}
	if (!ret)
		ret = hid_async_slot_queue_locked(req, lane,
						  recovery ? NULL : admission);
	if (ret != -EBUSY && admission)
		hid_async_admission_unlink_locked(admission);
	hid_transport_unlock();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

static void hid_async_admission_cancel(
		struct hid_async_admission_node *admission)
{
	hid_transport_lock();
	hid_async_admission_unlink_locked(admission);
	hid_transport_unlock();
}

static int hid_async_wait_queue_usb_request(struct hid_async_request *req,
					    u32 generation,
					    enum hid_async_lane lane)
{
	struct hid_async_admission_node admission = {
		.task = xTaskGetCurrentTaskHandle(),
	};
	TickType_t started;
	TickType_t timeout;
	int ret;

	if (!hid_async_sync_call_allowed())
		return -EAGAIN;

	/*
	 * Linux usb_internal_control_msg() allocates before usb_start_wait_urb()
	 * starts the transfer timeout. This fixed-pool admission is the allocation
	 * equivalent: use an independent deadline, then give an accepted request
	 * its complete queued-at/wire interval.
	 */
	timeout = HID_ASYNC_ADMISSION_TIMEOUT_TICKS;
	if (req->timeout_ticks && (TickType_t)req->timeout_ticks < timeout)
		timeout = (TickType_t)req->timeout_ticks;
	if (!timeout)
		timeout = 1;
	started = xTaskGetTickCount();
	/* Notifications are edges; discard an unrelated old admission edge. */
	(void)ulTaskNotifyTakeIndexed(HID_ASYNC_ADMISSION_NOTIFY_INDEX, pdTRUE, 0);

	for (;;) {
		TickType_t elapsed;

		/* Link before the first test, exactly like Linux prepare_to_wait(). */
		ret = hid_async_queue_usb_request(req, generation, lane, &admission);
		if (ret != -EBUSY)
			return ret;
		elapsed = xTaskGetTickCount() - started;
		if (elapsed >= timeout)
			break;
		if (!ulTaskNotifyTakeIndexed(HID_ASYNC_ADMISSION_NOTIFY_INDEX,
					      pdTRUE, timeout - elapsed))
			break;
	}

	/* Resolve release/cancel which raced either form of the deadline edge. */
	ret = hid_async_queue_usb_request(req, generation, lane, &admission);
	if (ret != -EBUSY)
		return ret;
	hid_async_admission_cancel(&admission);
	return -EBUSY;
}

static u32 hid_async_timeout_from_ms(int timeout)
{
	TickType_t ticks = pdMS_TO_TICKS((u32)timeout);

	return ticks ? (u32)ticks : 1u;
}

int hid_async_control_gate_acquire(void)
{
	int ret = 0;

	if (!hid_async_slots)
		return -ENODEV;

	hid_transport_lock();
	if (hid_async_control_gate) {
		ret = -EBUSY;
	} else {
		hid_async_control_gate = true;
	}
	hid_transport_unlock();
	if (!ret)
		hid_async_notify_task();
	return ret;
}

bool hid_async_control_gate_idle(void)
{
	bool idle;

	if (!hid_async_slots)
		return false;

	hid_transport_lock();
	idle = hid_async_control_gate && !hid_async_ctrl_active;
	hid_transport_unlock();
	return idle;
}

static void hid_async_control_gate_publish_idle(void)
{
	/*
	 * Upstream usbcore wakes reset work when the owned URB/EP0 lane retires.
	 * The firmware gate predicate stays in this executor; publish only its
	 * coalesced idle edge to the lifecycle owner.
	 */
	if (hid_async_control_gate_idle())
		usbhid_backend_control_gate_idle();
}

void hid_async_control_gate_release(u32 paused_ticks)
{
	TickType_t now = xTaskGetTickCount();
	bool released = false;

	if (!hid_async_slots)
		return;

	hid_transport_lock();
	if (hid_async_control_gate) {
		for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
			struct hid_async_slot *slot = &hid_async_slots[i];

			if (slot->state != HID_ASYNC_SLOT_QUEUED ||
			    hid_async_lane_is_out(
				(enum hid_async_lane)slot->lane) ||
			    hid_async_request_bypasses_control_gate(&slot->req))
				continue;
			/* Gate time is not part of a parked request's watchdog. */
			if (slot->gate_parked) {
				slot->queued_at = now;
				slot->gate_parked = false;
			} else {
				slot->queued_at += (TickType_t)paused_ticks;
			}
		}
		hid_async_control_gate = false;
		released = true;
	}
	hid_transport_unlock();
	if (released)
		hid_async_notify_task();
}

int hid_async_queue_hub_port_reset(u8 hub_addr, u32 generation, u8 hub_port,
				   hid_async_complete_t complete, void *context)
{
	struct hid_async_request req;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_HUB_RESET;
	req.dev_addr = hub_addr;
	req.hub_port = hub_port;
	req.timeout_ticks = hid_async_timeout_from_ms(USB_CTRL_SET_TIMEOUT);
	req.complete = complete;
	req.context = context;
	return hid_async_queue_usb_request(&req, generation,
					   HID_ASYNC_LANE_DEVICE_CTRL, NULL);
}

static int hid_async_queue_usb_control_common(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout,
		struct hid_async_admission_node *admission,
		hid_async_complete_t complete, void *context, bool wait)
{
	struct hid_async_request req;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_CONTROL;
	req.hid = owner;
	req.dev_addr = dev_addr;
	req.control_request = request;
	req.control_requesttype = requesttype;
	req.control_value = value;
	req.control_index = index;
	req.len = size;
	req.data = data;
	req.timeout_ticks = hid_async_timeout_from_ms(timeout);
	req.complete = complete;
	req.context = context;
	if (wait)
		return hid_async_wait_queue_usb_request(
			&req, generation, HID_ASYNC_LANE_DEVICE_CTRL);
	return hid_async_queue_usb_request(
		&req, generation, HID_ASYNC_LANE_DEVICE_CTRL, admission);
}

int hid_async_queue_usb_control_msg_admitted(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout,
		struct hid_async_admission_node *admission,
		hid_async_complete_t complete, void *context)
{
	return hid_async_queue_usb_control_common(owner, dev_addr, generation,
		request, requesttype, value, index, data, size, timeout,
		admission, complete, context, false);
}

int hid_async_wait_queue_usb_control_msg(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size, int timeout,
		hid_async_complete_t complete, void *context)
{
	return hid_async_queue_usb_control_common(owner, dev_addr, generation,
		request, requesttype, value, index, data, size, timeout,
		NULL, complete, context, true);
}

static int hid_async_queue_usb_interrupt_out_common(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 ep_addr, void *data, u16 size, int timeout,
		hid_async_complete_t complete, void *context)
{
	struct hid_async_request req;

	memset(&req, 0, sizeof(req));
	req.kind = HID_ASYNC_REQUEST_USB_INTERRUPT;
	req.hid = owner;
	req.dev_addr = dev_addr;
	req.ep_addr = ep_addr;
	req.len = size;
	req.data = data;
	req.timeout_ticks = hid_async_timeout_from_ms(timeout);
	req.complete = complete;
	req.context = context;
	return hid_async_wait_queue_usb_request(
		&req, generation, HID_ASYNC_LANE_DEVICE_OUT);
}

int hid_async_wait_queue_usb_interrupt_out(
		struct hid_device *owner, u8 dev_addr, u32 generation,
		u8 ep_addr, void *data, u16 size, int timeout,
		hid_async_complete_t complete, void *context)
{
	return hid_async_queue_usb_interrupt_out_common(owner, dev_addr,
		generation, ep_addr, data, size, timeout, complete, context);
}

static int hid_async_submit_control(struct hid_async_request *req)
{
	struct usbhid_device *usbhid = req->hid->driver_data;
	u8 *data = req->data;
	u16 len = req->len;
	int ready;
	tusb_control_request_t const request = {
		.bmRequestType_bit = {
			.recipient = TUSB_REQ_RCPT_INTERFACE,
			.type = TUSB_REQ_TYPE_CLASS,
			.direction = req->reqtype == HID_REQ_GET_REPORT ?
				     TUSB_DIR_IN : TUSB_DIR_OUT,
		},
		.bRequest = (u8)req->reqtype,
		.wValue = tu_htole16(TU_U16(req->report_type, req->report_id)),
		.wIndex = tu_htole16((u16)
			usbhid->usb_altsetting.desc.bInterfaceNumber),
		.wLength = tu_htole16(len),
	};
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = 0,
		.setup = &request,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	ready = usbh_port_control_submit_ready_on_host(
		req->dev_addr, hid_async_xfer_complete);
	if (ready < 0)
		return -ENODEV;
	if (!ready)
		return -EBUSY;
	/* READY -> false is an HCD submission failure, not a busy retry. */
	return tuh_control_xfer(&xfer) ? 0 : -EIO;
}

static int hid_async_submit_usb_control(struct hid_async_request *req)
{
	u8 *data = req->data;
	u16 len = req->len;
	int ready;
	tusb_control_request_t const request = {
		.bmRequestType = req->control_requesttype,
		.bRequest = req->control_request,
		.wValue = tu_htole16(req->control_value),
		.wIndex = tu_htole16(req->control_index),
		.wLength = tu_htole16(len),
	};
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = 0,
		.setup = &request,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	ready = usbh_port_control_submit_ready_on_host(
		req->dev_addr, hid_async_xfer_complete);
	if (ready < 0)
		return -ENODEV;
	if (!ready)
		return -EBUSY;
	return tuh_control_xfer(&xfer) ? 0 : -EIO;
}

static int hid_async_submit_hub_reset(struct hid_async_request *req)
{
	int ready = usbh_port_control_submit_ready_on_host(
		req->dev_addr, hid_async_xfer_complete);

	if (ready < 0)
		return -ENODEV;
	if (!ready)
		return -EBUSY;
	/* Match TinyUSB hub attach: SET_FEATURE(PORT_RESET) on the parent EP0. */
	return hub_port_reset(req->dev_addr, req->hub_port,
			      hid_async_xfer_complete,
			      (uintptr_t)req->serial) ? 0 : -EIO;
}

static int hid_async_submit_interrupt_out(struct hid_async_request *req)
{
	u8 *data = req->data;
	u16 len = req->len;
	int ready;
	tuh_xfer_t xfer = {
		.daddr = req->dev_addr,
		.ep_addr = req->ep_addr,
		.buflen = len,
		.buffer = len ? data : NULL,
		.complete_cb = hid_async_xfer_complete,
		.user_data = (uintptr_t)req->serial,
	};

	ready = usbh_port_edpt_submit_ready_on_host(req->dev_addr,
						      req->ep_addr);
	if (ready < 0)
		return -ENODEV;
	if (!ready)
		return -EBUSY;
	return tuh_edpt_xfer(&xfer) ? 0 : -EIO;
}

static int hid_async_submit(struct hid_async_request *req)
{
	if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		return hid_async_submit_interrupt_out(req);
	} else if (req->kind == HID_ASYNC_REQUEST_USB_INTERRUPT) {
		return hid_async_submit_interrupt_out(req);
	} else if (req->kind == HID_ASYNC_REQUEST_USB_CONTROL) {
		return hid_async_submit_usb_control(req);
	} else if (req->kind == HID_ASYNC_REQUEST_HUB_RESET) {
		return hid_async_submit_hub_reset(req);
	} else {
		return hid_async_submit_control(req);
	}
}

static int hid_async_submit_current(struct hid_async_request *req)
{
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.request = req,
		.dev_addr = req->dev_addr,
		.instance = req->instance,
		.generation = req->generation,
		.serial = req->serial,
		.action = HID_ASYNC_HOST_SUBMIT,
		.status = -EIO,
	};

	if (req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	/* TinyUSB owns its global EP0 and class endpoint state in the host task. */
	hid_async_host_call_sync(&call);
	return call.status;
}

static TickType_t hid_async_request_xfer_timeout(
		const struct hid_async_request *req)
{
	if (req->timeout_ticks)
		return (TickType_t)req->timeout_ticks;
	return HID_ASYNC_XFER_TIMEOUT_TICKS;
}

static int hid_async_xfer_status(u8 result)
{
	switch (result) {
	case XFER_RESULT_SUCCESS:
		return 0;
	case XFER_RESULT_STALLED:
		return -EPIPE;
	case XFER_RESULT_TIMEOUT:
		return -ETIMEDOUT;
	default:
		return -EIO;
	}
}

static void hid_async_host_call_sync(struct hid_async_host_call *call)
{
	bool done;

	hid_transport_lock();
	call->done = false;
	hid_transport_unlock();
	(void)ulTaskNotifyTake(pdTRUE, 0);
	usbh_defer_func(hid_async_call_on_host, call, false);
	do {
		hid_transport_lock();
		done = call->done;
		hid_transport_unlock();
		if (!done)
			(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	} while (!done);
}

int hid_async_cancel_device(struct hid_device *hid)
{
	if (!hid_async_slots)
		return -ENODEV;

	/*
	 * Like usb_kill_urb() on one interface's URBs, cancel only requests owned by
	 * this exact HID. An address/instance pair can be reused after a fast replug
	 * and must not select work from the new interface epoch or a sibling.
	 * Do not unlink slots or invoke continuations here: report_stop()/unplug()
	 * already published transport_stopping, so hid_async_task retires/completes
	 * each slot in task context and the combined teardown predicate fences its
	 * final release before destruction.
	 */
	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state != HID_ASYNC_SLOT_FREE && slot->req.hid == hid)
			slot->cancel_requested = true;
	}
	usbhid_request_cancel_device_locked(hid);
	/* Admission waiters are not slots yet; wake them to observe stop state. */
	hid_async_admission_wake_all_locked();
	hid_transport_unlock();
	hid_async_notify_task();
	return 0;
}

bool hid_async_device_idle(struct hid_device *hid)
{
	bool idle = true;

	/* usb_kill_urb() may return only after no executor slot retains this HID. */
	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (hid_async_slots[i].state != HID_ASYNC_SLOT_FREE &&
		    hid_async_slots[i].req.hid == hid) {
			idle = false;
			break;
		}
	}
	if (idle)
		idle = usbhid_request_queue_idle_locked(hid);
	hid_transport_unlock();
	return idle;
}

int hid_async_cancel_dev_addr(u8 dev_addr)
{
	u32 generation;

	if (!hid_async_slots)
		return -ENODEV;
	if (!dev_addr || dev_addr > HID_ASYNC_DEVICE_ADDR_MAX)
		return -ENODEV;

	hid_transport_lock();
	generation = hid_async_device_generation_locked(dev_addr);
	hid_async_device_generation_advance_locked(dev_addr);
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state != HID_ASYNC_SLOT_FREE &&
		    slot->req.dev_addr == dev_addr &&
		    slot->req.generation == generation)
			slot->cancel_requested = true;
	}
	usbhid_request_cancel_dev_addr_locked(dev_addr);
	/* A pre-slot synchronous caller must observe this generation advance. */
	hid_async_admission_wake_all_locked();
	hid_transport_unlock();
	hid_async_notify_task();
	return 0;
}

static TickType_t hid_async_ticks_left(TickType_t now, TickType_t start,
				       TickType_t timeout)
{
	TickType_t elapsed = now - start;

	return elapsed >= timeout ? 0 : timeout - elapsed;
}

static bool hid_async_slot_invalid_locked(struct hid_async_slot *slot)
{
	struct usbhid_device *usbhid;

	if (slot->cancel_requested ||
	    !slot->req.dev_addr ||
	    slot->req.dev_addr > HID_ASYNC_DEVICE_ADDR_MAX ||
	    slot->req.generation !=
		hid_async_device_generation_locked(slot->req.dev_addr))
		return true;
	if (!slot->req.hid)
		return false;
	usbhid = slot->req.hid->driver_data;
	return usbhid->transport_stopping;
}

static void hid_async_slot_clear_physical_locked(struct hid_async_slot *slot)
{
	if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
	    hid_async_ctrl_active == hid_async_slot_id(slot))
		hid_async_ctrl_active = 0;
}

static void hid_async_log_result(const struct hid_async_request *req,
				 int status, bool submit_failure)
{
	if (submit_failure)
		return;

	if (req->kind == HID_ASYNC_REQUEST_USB_CONTROL &&
	    req->control_request == TUSB_REQ_CLEAR_FEATURE &&
	    req->control_requesttype == TUSB_REQ_RCPT_ENDPOINT &&
	    req->control_value == TUSB_REQ_FEATURE_EDPT_HALT && !req->len) {
		/* Preserve the recovery trace after merging its EP0 wire builder. */
		async_msg(status < 0 ? "ERR: HID_CLEAR_HALT_FAIL" :
				       "DBG: HID_CLEAR_HALT_OK");
	} else if (req->kind == HID_ASYNC_REQUEST_OUTPUT_REPORT) {
		async_msg(status < 0 ? "ERR: HID_REPORT_OUT_FAIL" :
				       "DBG: HID_REPORT_OUT_OK");
	} else if (req->report && req->reqtype == HID_REQ_SET_REPORT) {
		async_msg(status < 0 ? "ERR: HID_REPORT_SET_FAIL" :
				       "DBG: HID_REPORT_SET_OK");
	}
}

static void hid_async_finish_slot(struct hid_async_slot *slot, int status,
				  bool canceled, bool submit_failure)
{
	struct hid_async_request *req = &slot->req;
	bool control_lane;
	bool finish_state_valid = true;

	hid_transport_lock();
	control_lane = !hid_async_lane_is_out(
		(enum hid_async_lane)slot->lane);
	slot->accepting_completion = false;
	slot->completion_ready = false;
	hid_async_slot_clear_physical_locked(slot);
	slot->state = HID_ASYNC_SLOT_COMPLETING;
	hid_transport_unlock();

	if (canceled) {
		if (req->complete)
			req->complete(req, -ENODEV);
	} else {
		hid_async_log_result(req, status, submit_failure);
		if (req->complete)
			req->complete(req, status);
	}

	hid_transport_lock();
	if (slot->state == HID_ASYNC_SLOT_COMPLETING ||
	    slot->state == HID_ASYNC_SLOT_RELEASE_PENDING)
		hid_async_slot_release_locked(slot);
	else
		finish_state_valid = slot->state == HID_ASYNC_SLOT_WAIT_PARSE;
	hid_transport_unlock();
	if (!finish_state_valid) {
		async_msg("ERR: HID_FINISH_STATE");
		configASSERT(finish_state_valid);
		return;
	}
	/* Publish after release so a retry can reuse this exact bounded slot. */
	if (control_lane)
		hid_async_control_gate_publish_idle();
}

int hid_async_control_report_hold(const struct hid_async_request *req)
{
	struct hid_async_slot *slot;
	int ret = -ENODEV;

	if (!req || !req->hid || !req->report ||
	    req->reqtype != HID_REQ_GET_REPORT)
		return -EINVAL;

	hid_transport_lock();
	slot = hid_async_slot_for_request_locked((struct hid_async_request *)req);
	if (slot && slot->state == HID_ASYNC_SLOT_COMPLETING &&
	    slot->req.serial == req->serial &&
	    !hid_async_slot_invalid_locked(slot)) {
		slot->state = HID_ASYNC_SLOT_WAIT_PARSE;
		ret = 0;
	}
	hid_transport_unlock();
	return ret;
}

void hid_async_control_report_release(struct hid_device *hid, u32 serial)
{
	bool released = false;

	if (!hid || !serial || !hid_async_slots)
		return;

	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];

		if (slot->state == HID_ASYNC_SLOT_WAIT_PARSE &&
		    slot->req.hid == hid && slot->req.serial == serial) {
			slot->state = HID_ASYNC_SLOT_RELEASE_PENDING;
			released = true;
			break;
		}
	}
	hid_transport_unlock();
	if (released)
		hid_async_notify_task();
}

/*
 * Retire one physical transfer before its stable pool slot can be reused.
 * Other endpoint callbacks remain durable in their own slots while this
 * executor waits for TinyUSB's ordered host-owner abort/fence events.
 */
static int hid_async_retire_slot(struct hid_async_slot *slot)
{
	struct hid_async_request *req = &slot->req;
	struct hid_async_host_call call = {
		.waiter = xTaskGetCurrentTaskHandle(),
		.request = req,
		.dev_addr = req->dev_addr,
		.instance = req->instance,
		.ep_addr = hid_async_lane_is_out(
			(enum hid_async_lane)slot->lane) ? req->ep_addr : 0,
		.generation = req->generation,
		.serial = req->serial,
		.action = HID_ASYNC_HOST_ABORT,
	};
	bool completed;
	bool callback_lost = false;
	bool ep0 = !call.ep_addr;
	bool retire_pending;
	bool resource_wait;

	hid_transport_lock();
	if (slot->state == HID_ASYNC_SLOT_HOST_COMPLETING) {
		hid_transport_unlock();
		return -EAGAIN;
	}
	completed = slot->completion_ready;
	if (!completed)
		slot->state = HID_ASYNC_SLOT_RETIRING;
	retire_pending = slot->retire_pending;
	resource_wait = slot->submit_wait == HID_ASYNC_SUBMIT_WAIT_CONTROL;
	hid_transport_unlock();
	if (completed)
		return 1;
	if (retire_pending || resource_wait)
		return -EAGAIN;

	if (ep0) {
		/*
		 * One global host-owned transaction now supplies Linux's exact
		 * usb_kill_urb() lifetime boundary. A pending cancel keeps this stable
		 * slot, callback identity, and caller buffer alive until the captured
		 * TinyUSB FIFO prefix produces its terminal callback. If enumeration
		 * already owns that fence, the exact control-idle edge retries us later.
		 */
		hid_async_host_call_sync(&call);
		if (call.status == HID_ASYNC_CONTROL_CANCEL_PENDING ||
		    call.status == HID_ASYNC_CONTROL_CANCEL_BUSY)
			return -EAGAIN;
		if (call.status == HID_ASYNC_CONTROL_CANCEL_STALE)
			async_msg("ERR: HID_EP0_OWNER_MISMATCH");
	} else {
		hid_async_host_call_sync(&call);
		/* The FIFO host call retires any completion queued by the abort. */
		call.action = HID_ASYNC_HOST_FENCE;
		hid_async_host_call_sync(&call);
	}

	hid_transport_lock();
	slot->accepting_completion = false;
	completed = slot->completion_ready;
	callback_lost = ep0 && !completed &&
		call.status == HID_ASYNC_CONTROL_CANCEL_CALLBACK_DONE &&
		!hid_async_slot_invalid_locked(slot);
	hid_async_slot_clear_physical_locked(slot);
	hid_transport_unlock();
	if (callback_lost)
		async_msg("ERR: HID_EP0_CALLBACK_LOST");
	return completed ? 1 : 0;
}

static bool hid_async_process_released(void)
{
	bool processed = false;

	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		if (hid_async_slots[i].state !=
				HID_ASYNC_SLOT_RELEASE_PENDING)
			continue;
		hid_async_slot_release_locked(&hid_async_slots[i]);
		processed = true;
		break;
	}
	hid_transport_unlock();
	if (processed)
		hid_async_control_gate_publish_idle();
	return processed;
}

static bool hid_async_process_active(void)
{
	struct hid_async_slot *slot = NULL;
	TickType_t now;
	TickType_t timeout = 0;
	bool canceled = false;
	bool completed = false;
	bool retiring = false;
	bool timed_out = false;
	int retire_status;
	int status = 0;

	hid_transport_lock();
	/*
	 * Transfer timestamps are published under this mutex. Sample afterwards so
	 * a concurrently published timestamp cannot be newer than this comparison.
	 */
	now = xTaskGetTickCount();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state == HID_ASYNC_SLOT_RETIRING) {
			completed = candidate->completion_ready;
			canceled = hid_async_slot_invalid_locked(candidate);
			/*
			 * Logical cancellation does not release TinyUSB's physical owner.
			 * Keep the stable slot while an exact cancel callback/fence or
			 * resource-idle edge is still outstanding.
			 */
			if (!completed &&
			    (candidate->retire_pending ||
			     candidate->submit_wait != HID_ASYNC_SUBMIT_WAIT_NONE))
				continue;
			retiring = true;
			timeout = hid_async_request_xfer_timeout(&candidate->req);
			timed_out = candidate->req.timeout_ticks ?
				now - candidate->queued_at >= timeout :
				now - candidate->xfer_start >= timeout;
			slot = candidate;
			break;
		}
		if (candidate->state != HID_ASYNC_SLOT_ACTIVE)
			continue;
		timeout = hid_async_request_xfer_timeout(&candidate->req);
		completed = candidate->completion_ready;
		canceled = hid_async_slot_invalid_locked(candidate);
		timed_out = candidate->req.timeout_ticks ?
			now - candidate->queued_at >= timeout :
			now - candidate->xfer_start >= timeout;
		if (completed || canceled || timed_out) {
			slot = candidate;
			break;
		}
	}
	hid_transport_unlock();
	if (!slot)
		return false;

	if (!completed && (retiring || canceled || timed_out)) {
		retire_status = hid_async_retire_slot(slot);
		if (retire_status == -EAGAIN)
			return false;
		completed = retire_status > 0;
	}

	hid_transport_lock();
	if (completed)
		status = slot->completion_status;
	canceled = hid_async_slot_invalid_locked(slot);
	hid_async_slot_clear_physical_locked(slot);
	hid_transport_unlock();

	if (canceled) {
		status = -ENODEV;
	} else if (!completed && timed_out) {
		async_msg("ERR: HID_XFER_TO");
		status = -ETIMEDOUT;
	}
	hid_async_finish_slot(slot, status, canceled, false);
	return true;
}

static bool hid_async_process_queued_terminal(void)
{
	struct hid_async_slot *slot = NULL;
	TickType_t now;
	bool canceled = false;
	bool timed_out = false;

	hid_transport_lock();
	now = xTaskGetTickCount();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];

		if (candidate->state != HID_ASYNC_SLOT_QUEUED)
			continue;
		canceled = hid_async_slot_invalid_locked(candidate);
		timed_out = !hid_async_slot_parked_by_control_gate(candidate) &&
			candidate->req.timeout_ticks &&
			now - candidate->queued_at >=
			(TickType_t)candidate->req.timeout_ticks;
		if (canceled || timed_out) {
			slot = candidate;
			break;
		}
	}
	hid_transport_unlock();
	if (!slot)
		return false;

	/* Queued work has no physical owner, so it can leave any FIFO position. */
	hid_async_finish_slot(slot, canceled ? -ENODEV : -ETIMEDOUT,
				 canceled, timed_out);
	return true;
}

static struct hid_async_slot *hid_async_find_ready_head(void)
{
	struct hid_async_slot *slot = NULL;

	hid_transport_lock();
	for (u8 n = 0; n < HID_ASYNC_SLOT_COUNT; n++) {
		u8 index = (u8)((hid_async_schedule_cursor + 1u + n) %
				HID_ASYNC_SLOT_COUNT);
		struct hid_async_slot *candidate = &hid_async_slots[index];

		if (candidate->state != HID_ASYNC_SLOT_QUEUED ||
		    !hid_async_slot_is_head_locked(candidate) ||
		    hid_async_slot_invalid_locked(candidate) ||
		    hid_async_slot_parked_by_control_gate(candidate))
			continue;
		if (!hid_async_lane_is_out(
			(enum hid_async_lane)candidate->lane) &&
		    hid_async_ctrl_active)
			continue;
		if (candidate->submit_wait != HID_ASYNC_SUBMIT_WAIT_NONE)
			continue;
		hid_async_schedule_cursor = index;
		slot = candidate;
		break;
	}
	hid_transport_unlock();
	return slot;
}

static bool hid_async_start_slot(struct hid_async_slot *slot)
{
	struct hid_async_request *req = &slot->req;
	TickType_t now;
	bool canceled;
	bool control_lane;
	bool control_lane_available = true;
	bool gated;
	bool submit_state_valid = true;
	int ret;

	hid_transport_lock();
	control_lane = !hid_async_lane_is_out(
		(enum hid_async_lane)slot->lane);
	canceled = hid_async_slot_invalid_locked(slot);
	gated = hid_async_slot_parked_by_control_gate(slot);
	if (!canceled && !gated) {
		if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane))
			control_lane_available = hid_async_ctrl_active == 0;
		if (control_lane_available) {
			if (!req->serial) {
				req->serial = ++hid_async_serial;
				if (!req->serial)
					req->serial = ++hid_async_serial;
			}
			req->actual_len = 0;
			slot->completion_ready = false;
			slot->accepting_completion = true;
			/*
			 * Arm the idle edge before deferring into the host task. An old
			 * owner can retire before the deferred call returns to this task.
			 */
			slot->submit_wait = control_lane ?
				HID_ASYNC_SUBMIT_WAIT_CONTROL :
				HID_ASYNC_SUBMIT_WAIT_ENDPOINT;
			slot->state = HID_ASYNC_SLOT_SUBMIT_PENDING;
			if (!hid_async_lane_is_out(
					(enum hid_async_lane)slot->lane))
				hid_async_ctrl_active = hid_async_slot_id(slot);
		}
	}
	hid_transport_unlock();
	if (!control_lane_available) {
		async_msg("ERR: HID_CTRL_OWNER");
		configASSERT(control_lane_available);
		return false;
	}
	if (gated && !canceled)
		return false;

	if (canceled) {
		hid_async_finish_slot(slot, -ENODEV, true, false);
		return true;
	}

	ret = hid_async_submit_current(req);
	now = xTaskGetTickCount();
	hid_transport_lock();
	if (!ret) {
		slot->submit_wait = HID_ASYNC_SUBMIT_WAIT_NONE;
		req->wire_started = true;
		if (slot->state == HID_ASYNC_SLOT_SUBMIT_PENDING) {
			slot->state = HID_ASYNC_SLOT_ACTIVE;
			slot->xfer_start = now;
		} else {
			/* A fast HUB_RESET completion owns/published this state. */
			submit_state_valid =
				slot->state == HID_ASYNC_SLOT_HOST_COMPLETING ||
				(slot->state == HID_ASYNC_SLOT_ACTIVE &&
				 slot->completion_ready);
		}
	} else if (ret == -EBUSY) {
		/* The host owner re-armed this wait after its exact BUSY snapshot. */
		slot->accepting_completion = false;
		slot->state = HID_ASYNC_SLOT_QUEUED;
		hid_async_slot_clear_physical_locked(slot);
	} else if (ret == -EAGAIN) {
		/* The local reset gate raced submission; it owns the wake predicate. */
		slot->accepting_completion = false;
		slot->submit_wait = HID_ASYNC_SUBMIT_WAIT_NONE;
		slot->state = HID_ASYNC_SLOT_QUEUED;
		hid_async_slot_clear_physical_locked(slot);
	} else {
		slot->accepting_completion = false;
		slot->submit_wait = HID_ASYNC_SUBMIT_WAIT_NONE;
		hid_async_slot_clear_physical_locked(slot);
	}
	canceled = hid_async_slot_invalid_locked(slot);
	hid_transport_unlock();
	if (!submit_state_valid) {
		async_msg("ERR: HID_SUBMIT_STATE");
		configASSERT(submit_state_valid);
		return false;
	}

	if (!ret) {
		return true;
	}
	if (ret == -EBUSY || ret == -EAGAIN) {
		/* A durable resource/gate predicate owns the next submit attempt. */
		if (control_lane)
			hid_async_control_gate_publish_idle();
		return true;
	}

	hid_async_finish_slot(slot, canceled ? -ENODEV : ret,
				 canceled, true);
	return true;
}

static TickType_t hid_async_next_wait(void)
{
	TickType_t wait = portMAX_DELAY;
	TickType_t now;

	hid_transport_lock();
	now = xTaskGetTickCount();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *slot = &hid_async_slots[i];
		TickType_t left;
		TickType_t timeout;

		if (slot->state == HID_ASYNC_SLOT_RELEASE_PENDING ||
		    slot->completion_ready ||
		    ((slot->state == HID_ASYNC_SLOT_ACTIVE ||
		      slot->state == HID_ASYNC_SLOT_QUEUED) &&
		     hid_async_slot_invalid_locked(slot))) {
			wait = 0;
			break;
		}
		if (slot->state == HID_ASYNC_SLOT_ACTIVE) {
			timeout = hid_async_request_xfer_timeout(&slot->req);
			left = hid_async_ticks_left(now,
					slot->req.timeout_ticks ?
					slot->queued_at : slot->xfer_start,
						    timeout);
			wait = min_t(TickType_t, wait, left);
			continue;
		}
		if (slot->state != HID_ASYNC_SLOT_QUEUED)
			continue;
		if (hid_async_slot_parked_by_control_gate(slot))
			continue;
		if (slot->req.timeout_ticks) {
			left = hid_async_ticks_left(now, slot->queued_at,
				(TickType_t)slot->req.timeout_ticks);
			wait = min_t(TickType_t, wait, left);
			if (!left)
				continue;
		}
		if (!hid_async_slot_is_head_locked(slot))
			continue;
		if (!hid_async_lane_is_out((enum hid_async_lane)slot->lane) &&
		    hid_async_ctrl_active)
			continue;
		if (slot->submit_wait == HID_ASYNC_SUBMIT_WAIT_NONE) {
			wait = 0;
			break;
		}
		/* Exact TinyUSB owner release or cancellation wakes this slot. */
	}
	hid_transport_unlock();
	return wait;
}

void hid_async_task(void *pvParameters)
{
	(void)pvParameters;
	hid_transport_lock();
	hid_async_task_handle = xTaskGetCurrentTaskHandle();
	hid_transport_unlock();

	for (;;) {
		struct hid_async_slot *slot;
		TickType_t wait;

		if (usbhid_request_process())
			continue;
		if (hid_async_process_released())
			continue;
		if (hid_async_process_active())
			continue;
		if (hid_async_process_queued_terminal())
			continue;
		slot = hid_async_find_ready_head();
		if (slot) {
			(void)hid_async_start_slot(slot);
			continue;
		}

		wait = hid_async_next_wait();
		(void)ulTaskNotifyTakeIndexed(HID_ASYNC_NOTIFY_INDEX, pdTRUE,
					       wait);
	}
}

static void hid_async_xfer_complete(tuh_xfer_t *xfer)
{
	struct hid_async_slot *slot = NULL;
	void *host_context = NULL;
	u32 serial = (u32)xfer->user_data;
	u8 hub_addr = 0;
	u8 hub_port = 0;
	u32 actual_len = 0;
	int host_status = 0;
	enum usbhid_async_invariant invariant =
		USBHID_ASYNC_INVARIANT_NONE;
	bool completed = false;
	bool host_complete = false;
	bool hub_slot_owned = false;
	bool hub_slot_valid = true;

	hid_transport_lock();
	for (u8 i = 0; i < HID_ASYNC_SLOT_COUNT; i++) {
		struct hid_async_slot *candidate = &hid_async_slots[i];
		struct hid_async_request *req = &candidate->req;

		/* Serial selects the stable request; the remaining tuple validates it. */
		if (!serial || req->serial != serial ||
		    (candidate->state != HID_ASYNC_SLOT_ACTIVE &&
		     candidate->state != HID_ASYNC_SLOT_SUBMIT_PENDING &&
		     candidate->state != HID_ASYNC_SLOT_RETIRING) ||
		    !candidate->accepting_completion)
			continue;

		slot = candidate;
		slot->accepting_completion = false;
		actual_len = xfer->actual_len;
		/*
		 * TinyUSB 2.1.1 carries PIO's eight-byte SETUP completion into
		 * actual_len when an EP0 request has no data stage. Linux reports
		 * transferred payload bytes, so accept that pinned transport spelling
		 * but publish the same zero result as usbcore.
		 */
		if (!xfer->ep_addr && !req->len &&
		    (actual_len == 0 ||
		     actual_len == sizeof(tusb_control_request_t)))
			actual_len = 0;
		if (req->dev_addr != xfer->daddr || !req->dev_addr ||
		    req->dev_addr > HID_ASYNC_DEVICE_ADDR_MAX ||
		    req->generation !=
			hid_async_device_generation_locked(req->dev_addr) ||
		    (hid_async_lane_is_out(
			(enum hid_async_lane)candidate->lane) ?
			xfer->ep_addr != req->ep_addr : xfer->ep_addr != 0) ||
		    actual_len > req->len)
			invariant = USBHID_ASYNC_INVARIANT_XFER_TUPLE;
		if (invariant != USBHID_ASYNC_INVARIANT_NONE) {
			/* Physical giveback is terminal; task context retires the bad tuple. */
			req->actual_len = 0;
			slot->completion_status = -EIO;
			slot->completion_ready = true;
			completed = true;
			break;
		}

		req->actual_len = (u16)actual_len;
		slot->completion_status = hid_async_xfer_status(xfer->result);
		if (req->kind == HID_ASYNC_REQUEST_HUB_RESET) {
			/* Pin this stable slot until the immediate host handoff returns. */
			slot->state = HID_ASYNC_SLOT_HOST_COMPLETING;
			hub_addr = req->dev_addr;
			hub_port = req->hub_port;
			host_context = req->context;
			host_status = slot->completion_status;
			host_complete = true;
		} else {
			slot->completion_ready = true;
			completed = true;
		}
		break;
	}
	hid_transport_unlock();

	if (host_complete) {
		/* TinyUSB hub status must not clear C_PORT_RESET before ATTACH. */
		usbhid_backend_hub_reset_host_complete(hub_addr, hub_port,
						       host_context, host_status);

		hid_transport_lock();
		/* HOST_COMPLETING prevents SMP teardown from recycling this slot. */
		hub_slot_owned = slot &&
			slot->state == HID_ASYNC_SLOT_HOST_COMPLETING;
		hub_slot_valid = hub_slot_owned &&
			slot->req.serial == serial &&
			slot->req.kind == HID_ASYNC_REQUEST_HUB_RESET;
		if (hub_slot_owned) {
			/*
			 * The host handoff has returned, so never strand its pinned slot.
			 * A bad identity retires through the ordinary task completion path;
			 * callback context publishes only the diagnostic bit below.
			 */
			if (!hub_slot_valid)
				slot->completion_status = -EIO;
			slot->state = HID_ASYNC_SLOT_ACTIVE;
			slot->completion_ready = true;
			completed = true;
		}
		hid_transport_unlock();
		if (!hub_slot_valid)
			invariant = USBHID_ASYNC_INVARIANT_HUB_PIN;
	}
	if (invariant != USBHID_ASYNC_INVARIANT_NONE)
		usbhid_backend_async_invariant_failed(invariant);
	if (completed)
		hid_async_notify_task();
}
