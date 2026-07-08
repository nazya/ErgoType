#include <string.h>

#include "FreeRTOS.h"
#include "portable.h"
#include "queue.h"

#include "accel/filter.h"
#include "devmon.h"
#include "evdev.h"
#include "uapi/linux/input-event-codes.h"

#define EVDEV_QUEUE_REMOVE_RESERVE 1u
#define EVDEV_QUEUE_REPAIR_RESERVE 1u
#define EVDEV_QUEUE_NORMAL_RESERVE \
	(EVDEV_QUEUE_REMOVE_RESERVE + EVDEV_QUEUE_REPAIR_RESERVE)

enum {
	EVDEV_FILTER_CURVE_SCALE = 20,
};

static const int32_t evdev_move_filter_points_q10[] = {
	FILTER_Q10(0, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(1, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(4, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(10, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(20, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(50, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(90, EVDEV_FILTER_CURVE_SCALE),
};

static const int32_t evdev_scroll_filter_points_q10[] = {
	FILTER_Q10(0, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(1, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(4, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(10, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(20, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(50, EVDEV_FILTER_CURVE_SCALE),
	FILTER_Q10(90, EVDEV_FILTER_CURVE_SCALE),
};

static const struct filter_params evdev_move_filter_params = {
	.profile = FILTER_PROFILE_CUSTOM,
	.speed_q10 = FILTER_Q10(0, 1),
	.custom_step_q10 = FILTER_Q10(30, EVDEV_FILTER_CURVE_SCALE),
	.custom_points_q10 = evdev_move_filter_points_q10,
	.custom_npoints = sizeof(evdev_move_filter_points_q10) /
			  sizeof(evdev_move_filter_points_q10[0]),
	.adaptive_velocity_averaging = true,
};

static const struct filter_params evdev_scroll_filter_params = {
	.profile = FILTER_PROFILE_CUSTOM,
	.speed_q10 = FILTER_Q10(0, 1),
	.custom_step_q10 = FILTER_Q10(30, EVDEV_FILTER_CURVE_SCALE),
	.custom_points_q10 = evdev_scroll_filter_points_q10,
	.custom_npoints = sizeof(evdev_scroll_filter_points_q10) /
			  sizeof(evdev_scroll_filter_points_q10[0]),
	.adaptive_velocity_averaging = true,
};

static struct filter_state *evdev_move_filter;
static struct filter_state *evdev_scroll_filter;
static int evdev_filters_initialized;

/*
 * Upstream evdev buffers events for userspace clients. Firmware replaces that
 * per-client fd buffer with this device queue boundary.
 */
struct evdev_device {
	struct port_input_dev port_dev;
	struct evdev_stats stats;
	int32_t rel_x;
	int32_t rel_y;
	int32_t wheel_lo_x;
	int32_t wheel_lo_y;
	int32_t wheel_hi_x;
	int32_t wheel_hi_y;
};

static BaseType_t evdev_send_input_reserved(struct port_input_dev *port_dev,
					    const struct input_event *ev,
					    UBaseType_t reserve)
{
	BaseType_t ret;

	if (uxQueueMessagesWaiting(port_dev->ev_queue) >= DEVICE_EVENT_QUEUE_LEN - reserve)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(port_dev->ev_queue, ev, 0);

	return ret;
}

static BaseType_t evdev_send_input(struct port_input_dev *port_dev,
				   const struct input_event *ev)
{
	return evdev_send_input_reserved(port_dev, ev, EVDEV_QUEUE_NORMAL_RESERVE);
}

static BaseType_t evdev_send_repair_input(struct port_input_dev *port_dev,
					  const struct input_event *ev)
{
	return evdev_send_input_reserved(port_dev, ev, EVDEV_QUEUE_REMOVE_RESERVE);
}

static BaseType_t evdev_queue_reset(struct port_input_dev *port_dev)
{
	struct input_event dropped;
	struct input_event ev = {0};
	BaseType_t ret;

	ev.type = DEVICE_INPUT_RESET;

	if (uxQueueMessagesWaiting(port_dev->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		(void)xQueueReceive(port_dev->ev_queue, &dropped, 0);
	if (uxQueueMessagesWaiting(port_dev->ev_queue) >=
	    DEVICE_EVENT_QUEUE_LEN - EVDEV_QUEUE_REMOVE_RESERVE)
		ret = pdFAIL;
	else
		ret = xQueueSendToBack(port_dev->ev_queue, &ev, 0);

	return ret;
}

static void evdev_note_input_drop(struct evdev_device *evdev,
				  const struct input_event *ev)
{
	if (ev->type == EV_KEY) {
		evdev->stats.key_dropped++;
		if (!ev->value) {
			evdev->stats.key_release_dropped++;
			if (evdev_queue_reset(&evdev->port_dev) == pdPASS)
				evdev->stats.key_reset_queued++;
			else
				evdev->stats.key_reset_dropped++;
		}
	} else if (ev->type == EV_REL) {
		evdev->stats.rel_dropped++;
	} else if (ev->type == EV_ABS) {
		evdev->stats.abs_dropped++;
	} else {
		evdev->stats.other_dropped++;
	}
}

static void evdev_queue_input_event(struct evdev_device *evdev,
				    const struct input_event *ev)
{
	BaseType_t ret;

	ret = (ev->type == EV_KEY && !ev->value) ?
		      evdev_send_repair_input(&evdev->port_dev, ev) :
		      evdev_send_input(&evdev->port_dev, ev);
	if (ret == pdPASS)
		return;

	evdev_note_input_drop(evdev, ev);
}

static void evdev_queue_rel(struct evdev_device *evdev,
			    uint16_t code,
			    int32_t value)
{
	if (!value)
		return;

	struct input_event ev = {0};

	ev.type = EV_REL;
	ev.code = code;
	ev.value = value;
	evdev_queue_input_event(evdev, &ev);
}

static void evdev_flush_relative_motion(struct evdev_device *evdev)
{
	filter_process(evdev_move_filter,
		       &evdev_move_filter_params,
		       &evdev->rel_x,
		       &evdev->rel_y);
	evdev_queue_rel(evdev, REL_X, evdev->rel_x);
	evdev_queue_rel(evdev, REL_Y, evdev->rel_y);

	evdev->rel_x = 0;
	evdev->rel_y = 0;
}

static void evdev_flush_wheels(struct evdev_device *evdev)
{
	filter_process(evdev_scroll_filter,
		       &evdev_scroll_filter_params,
		       &evdev->wheel_hi_x,
		       &evdev->wheel_hi_y);
	evdev_queue_rel(evdev, REL_HWHEEL_HI_RES, evdev->wheel_hi_x);
	evdev_queue_rel(evdev, REL_WHEEL_HI_RES, evdev->wheel_hi_y);
	evdev_queue_rel(evdev, REL_HWHEEL, evdev->wheel_lo_x);
	evdev_queue_rel(evdev, REL_WHEEL, evdev->wheel_lo_y);

	evdev->wheel_hi_x = 0;
	evdev->wheel_hi_y = 0;
	evdev->wheel_lo_x = 0;
	evdev->wheel_lo_y = 0;
}

static void evdev_dispatch_input_event(struct evdev_device *evdev,
				       const struct input_event *ev)
{
	switch (ev->type) {
	case EV_REL:
		switch (ev->code) {
		case REL_X:
			evdev->rel_x += ev->value;
			return;
		case REL_Y:
			evdev->rel_y += ev->value;
			return;
		case REL_HWHEEL:
			evdev->wheel_lo_x += ev->value;
			return;
		case REL_WHEEL:
			evdev->wheel_lo_y += ev->value;
			return;
		case REL_HWHEEL_HI_RES:
			evdev->wheel_hi_x += ev->value;
			return;
		case REL_WHEEL_HI_RES:
			evdev->wheel_hi_y += ev->value;
			return;
		default:
			break;
		}
		break;
	case EV_SYN:
		if (ev->code == SYN_REPORT) {
			evdev_flush_relative_motion(evdev);
			evdev_flush_wheels(evdev);
		}
		break;
	default:
		break;
	}

	evdev_queue_input_event(evdev, ev);
}

static int evdev_filter_init_once(void)
{
	if (evdev_filters_initialized)
		return 0;

	evdev_move_filter = pvPortMalloc(sizeof *evdev_move_filter);
	evdev_scroll_filter = pvPortMalloc(sizeof *evdev_scroll_filter);
	if (!evdev_move_filter || !evdev_scroll_filter) {
		vPortFree(evdev_move_filter);
		vPortFree(evdev_scroll_filter);
		evdev_move_filter = NULL;
		evdev_scroll_filter = NULL;
		return -1;
	}

	filter_init(evdev_move_filter,
		    &evdev_move_filter_params,
		    DEFAULT_MOUSE_DPI);
	filter_init(evdev_scroll_filter,
		    &evdev_scroll_filter_params,
		    DEFAULT_MOUSE_DPI);
	evdev_filters_initialized = 1;
	return 0;
}

void *evdev_register_device(const struct port_input_dev *port_dev)
{
	int ret;
	struct evdev_device *evdev = pvPortMalloc(sizeof *evdev);
	if (!evdev)
		return NULL;
	memset(evdev, 0, sizeof *evdev);
	evdev->port_dev = *port_dev;
	evdev->port_dev.ev_queue = xQueueCreate(DEVICE_EVENT_QUEUE_LEN, sizeof(struct input_event));
	if (!evdev->port_dev.ev_queue) {
		vPortFree(evdev);
		return NULL;
	}
	if (evdev_filter_init_once() < 0) {
		vQueueDelete(evdev->port_dev.ev_queue);
		vPortFree(evdev);
		return NULL;
	}
	ret = devmon_add_device(&evdev->port_dev);
	if (ret < 0) {
		vQueueDelete(evdev->port_dev.ev_queue);
		vPortFree(evdev);
		return NULL;
	}

	return evdev;
}

void evdev_unregister_device(void *dev)
{
	struct input_event ev = {0};
	struct evdev_device *evdev = dev;
	struct port_input_dev *port_dev = &evdev->port_dev;
	struct input_event dropped;

	ev.type = DEVICE_INPUT_REMOVED;
	// ret = evdev_send_input_reserved(device, &ev, 0);
	// configASSERT(ret == pdPASS);
	// TinyUSB unmount path must not assert/block; drop one stale event if
	// needed so removal reaches keyd.
	if (uxQueueMessagesWaiting(port_dev->ev_queue) >= DEVICE_EVENT_QUEUE_LEN)
		(void)xQueueReceive(port_dev->ev_queue, &dropped, 0);
	(void)xQueueSendToBack(port_dev->ev_queue, &ev, 0);
	vPortFree(dev);
}

void evdev_input_event(void *dev, uint16_t type, uint16_t code, int32_t value)
{
	struct input_event ev = {0};
	struct evdev_device *evdev = dev;

	ev.type = type;
	ev.code = code;
	ev.value = value;
	evdev_dispatch_input_event(evdev, &ev);
}

void evdev_get_stats(void *dev, struct evdev_stats *stats)
{
	struct evdev_device *evdev = dev;

	*stats = evdev->stats;
}
