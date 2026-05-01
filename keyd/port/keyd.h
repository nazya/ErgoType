/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef KEYD_H_
#define KEYD_H_

#include "device.h"
#include "keys.h"

enum event_type {
	EV_DEV_ADD,
	EV_DEV_REMOVE,
	EV_DEV_EVENT,
	EV_TIMEOUT,
};

struct event {
	enum event_type type;
	struct device_event *devev;
	int timestamp;
};

int run_daemon(void);
int evloop(int (*event_handler)(struct event *ev));

#endif
