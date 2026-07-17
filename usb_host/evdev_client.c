#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"
#include "semphr.h"

#include "devmon.h"
#include "evdev.h"
#include "linux/include/linux/hid.h"
#include "stdio_tusb_cdc.h"
#include <linux/input-event-codes.h>

#define EVDEV_QUEUE_REMOVE_RESERVE 1u
#define EVDEV_QUEUE_RELEASE_RESERVE 1u
#define EVDEV_QUEUE_NORMAL_RESERVE \
	(EVDEV_QUEUE_REMOVE_RESERVE + EVDEV_QUEUE_RELEASE_RESERVE)

struct evdev_client {
	// unsigned int head;
	// unsigned int tail;
	// unsigned int packet_head; /* [future] position of the first element of next packet */
	// spinlock_t buffer_lock; /* protects access to buffer, head and tail */
	// wait_queue_head_t wait;
	// struct fasync_struct *fasync;
	// struct evdev *evdev;
	// Firmware stores the same link for evdev_client_write().
	struct evdev *evdev;
	// Firmware has no struct file, but ff-core uses file pointer identity as
	// the force-feedback effect owner token.
	struct file file;
	// struct list_head node;
	// enum input_clock_type clk_type;
	// bool revoked;
	// unsigned long *evmasks[EV_CNT];
	// unsigned int bufsize;
	// struct input_event buffer[] __counted_by(bufsize);
	// Firmware keeps the upstream field name, but backs the per-client
	// buffer with a FreeRTOS queue instead of a ring array.
	QueueHandle_t buffer;
};

/*
 * Upstream Linux: no equivalent. Linux file lifetime keeps evdev_client valid
 * during evdev_write(). Firmware calls the same writer from KeyD on CORE0
 * while disconnect runs on CORE1, so serialize writer calls with detachment.
 */
static SemaphoreHandle_t evdev_writer_mutex;

static void evdev_copy_absinfo(struct input_absinfo_snapshot *dst,
			       const struct input_absinfo *src)
{
	dst->value = src->value;
	dst->minimum = src->minimum;
	dst->maximum = src->maximum;
	dst->fuzz = src->fuzz;
	dst->flat = src->flat;
	dst->resolution = src->resolution;
}

static struct port_input_dev evdev_port_input_dev(const struct input_dev *src,
						  uint16_t vendor,
						  uint16_t product)
{
	struct port_input_dev port_dev = {0};

	port_dev.vendor = vendor;
	port_dev.product = product;
	port_dev.name = src->name ? src->name : "usb-hid";
	memcpy(port_dev.keybit, src->keybit, sizeof(port_dev.keybit));
	memcpy(port_dev.relbit, src->relbit, sizeof(port_dev.relbit));
	memcpy(port_dev.absbit, src->absbit, sizeof(port_dev.absbit));
	memcpy(port_dev.propbit, src->propbit, sizeof(port_dev.propbit));

	/*
	 * This is the compression point from full Linux input_dev state to the
	 * devmon queue snapshot. KeyD currently mirrors only EVIOCGABS(ABS_X/Y);
	 * extend this struct/function if a future path needs more absinfo.
	 */
	if (test_bit(ABS_X, src->absbit) && test_bit(ABS_Y, src->absbit)) {
		evdev_copy_absinfo(&port_dev.abs_x, &src->absinfo[ABS_X]);
		evdev_copy_absinfo(&port_dev.abs_y, &src->absinfo[ABS_Y]);
	}

	return port_dev;
}

static struct evdev_client *evdev_register_device(const struct port_input_dev *src,
						  struct evdev *evdev)
{
	struct evdev_client *client;
	struct port_input_dev port_dev;
	int ret;

	if (!evdev_writer_mutex) {
		evdev_writer_mutex = xSemaphoreCreateMutex();
		if (!evdev_writer_mutex)
			return NULL;
	}

	client = pvPortMalloc(sizeof *client);
	if (!client)
		return NULL;
	memset(client, 0, sizeof *client);
	client->buffer = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
	if (!client->buffer) {
		vPortFree(client);
		return NULL;
	}
	client->evdev = evdev;
	port_dev = *src;
	port_dev.ev_queue = client->buffer;
	// file->private_data = client;
	// Firmware has no eventX file; devmon stores the evdev_client pointer
	// in the writer object as the equivalent private_data.
	port_dev.writer.client = client;
	port_dev.writer.write = evdev_client_write;
	port_dev.writer.upload_ff = evdev_client_upload_ff;
	port_dev.writer.erase_ff = evdev_client_erase_ff;
	ret = devmon_add_device(&port_dev);
	if (ret < 0) {
		vQueueDelete(client->buffer);
		vPortFree(client);
		return NULL;
	}

	return client;
}

struct evdev_client *evdev_register_input_device(struct input_dev *src,
						 struct evdev *evdev)
{
	struct hid_device *hid = input_get_drvdata(src);
	struct port_input_dev port_dev;

	if (!hid && src->dev.parent && src->dev.parent->bus == &hid_bus_type) {
		// input_set_drvdata(input_dev, hid);
		// Some upstream drivers allocate extra input_dev objects from &hdev->dev
		// without storing HID drvdata. The firmware evdev client needs that link.
		hid = to_hid_device(src->dev.parent);
		input_set_drvdata(src, hid);
	}

	clear_bit(EV_REP, src->evbit);
	port_dev = evdev_port_input_dev(src, hid->vendor, hid->product);
	return evdev_register_device(&port_dev, evdev);
}

int evdev_client_write(struct evdev_client *client,
		       const struct input_event *events, size_t count)
{
	int ret;

	// struct evdev_client *client = file->private_data;
	// struct evdev *evdev = client->evdev;
	// Firmware has no struct file, so KeyD reaches the same synchronous writer
	// through evdev_writer.client. Disconnect clears evdev under this mutex.
	xSemaphoreTake(evdev_writer_mutex, portMAX_DELAY);
	ret = client->evdev ? evdev_write(client->evdev, events, count) : -ENODEV;
	xSemaphoreGive(evdev_writer_mutex);

	return ret;
}

int evdev_client_upload_ff(struct evdev_client *client, struct ff_effect *effect)
{
	int ret;

	// error = input_ff_upload(dev, &effect, file);
	// Firmware uses client->file as the ff-core owner token and the same mutex
	// as direct writes so disconnect cannot free evdev during the upload.
	xSemaphoreTake(evdev_writer_mutex, portMAX_DELAY);
	ret = client->evdev ?
		evdev_upload_ff(client->evdev, effect, &client->file) : -ENODEV;
	xSemaphoreGive(evdev_writer_mutex);

	return ret;
}

int evdev_client_erase_ff(struct evdev_client *client, int effect_id)
{
	int ret;

	// return input_ff_erase(dev, (int)(unsigned long) p, file);
	// Firmware uses client->file as the ff-core owner token and serializes the
	// direct KeyD call with disconnect.
	xSemaphoreTake(evdev_writer_mutex, portMAX_DELAY);
	ret = client->evdev ?
		evdev_erase_ff(client->evdev, effect_id, &client->file) : -ENODEV;
	xSemaphoreGive(evdev_writer_mutex);

	return ret;
}

void evdev_unregister_device(struct evdev_client *client)
{
	struct input_event ev = {0};
	struct input_event dropped;

	// if (evdev->exist && !client->revoked)
	// 	input_flush_device(&evdev->handle, file);
	// This upstream evdev_release() flush belongs to userspace fd close.
	// Firmware has no close(fd) lifecycle here; unregister is physical
	// disconnect, so sending FF cleanup reports to the HID device is too late.
	// Add that flush only if a real close-like evdev client lifecycle appears.
	// Keep the client allocation as a disconnected writer tombstone until KeyD
	// consumes DEVICE_INPUT_REMOVED; only the evdev target is detached here.
	xSemaphoreTake(evdev_writer_mutex, portMAX_DELAY);
	client->evdev = NULL;
	xSemaphoreGive(evdev_writer_mutex);
	ev.type = DEVICE_INPUT_REMOVED;
	// ret = evdev_send_input_reserved(device, &ev, 0);
	// configASSERT(ret == pdPASS);
	// TinyUSB unmount path must not assert/block; drop one stale event if
	// needed so removal reaches keyd.
	if (uxQueueMessagesWaiting(client->buffer) >= DEVICE_EVENT_QUEUE_LEN)
		(void)xQueueReceive(client->buffer, &dropped, 0);
	(void)xQueueSendToBack(client->buffer, &ev, 0);
	// vPortFree(client);
	// KeyD owns the client after this removal event and frees it together with
	// its device; keeping it alive makes the synchronous writer pointer safe.
}

void __pass_event(struct evdev_client *client,
		  const struct input_event *event)
{
	// client->buffer[client->head++] = *event;
	// client->head &= client->bufsize - 1;
	//
	// if (unlikely(client->head == client->tail)) {
	// 	/*
	// 	 * This effectively "drops" all unconsumed events, leaving
	// 	 * EV_SYN/SYN_DROPPED plus the newest event in the queue.
	// 	 */
	// 	client->tail = (client->head - 2) & (client->bufsize - 1);
	//
	// 	client->buffer[client->tail] = (struct input_event) {
	// 		.input_event_sec = event->input_event_sec,
	// 		.input_event_usec = event->input_event_usec,
	// 		.type = EV_SYN,
	// 		.code = SYN_DROPPED,
	// 		.value = 0,
	// 	};
	//
	// 	client->packet_head = client->tail;
	// }
	//
	// if (event->type == EV_SYN && event->code == SYN_REPORT) {
	// 	client->packet_head = client->head;
	// 	kill_fasync(&client->fasync, SIGIO, POLL_IN);
	// }
	//
	// Linux __pass_event() stores into the per-client ring buffer and handles
	// overflow with EV_SYN/SYN_DROPPED. Firmware passes compact input_event
	// records toward devmon instead.
	UBaseType_t reserve = EVDEV_QUEUE_NORMAL_RESERVE;
	if (event->type == EV_KEY && !event->value)
		reserve = EVDEV_QUEUE_REMOVE_RESERVE;

	if (uxQueueMessagesWaiting(client->buffer) < DEVICE_EVENT_QUEUE_LEN - reserve &&
	    xQueueSendToBack(client->buffer, event, 0) == pdPASS)
		return;

	struct input_event dropped;
	struct input_event syn_dropped = {0};

	async_msg("ERR: EVDEV_INPUT_DROP");
	syn_dropped.type = EV_SYN;
	syn_dropped.code = SYN_DROPPED;

	(void)xQueueReceive(client->buffer, &dropped, 0);
	if (xQueueSendToBack(client->buffer, &syn_dropped, 0) != pdPASS)
		async_msg("ERR: EVDEV_SYN_DROPPED");
}
