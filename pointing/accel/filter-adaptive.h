#ifndef FILTER_ADAPTIVE_H
#define FILTER_ADAPTIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "filter-math.h"

#define DEFAULT_MOUSE_DPI 1000
#define MAX_TRACKERS 16

struct coords {
	float x;
	float y;
};

struct pointer_tracker {
	struct coords delta;
	uint32_t time_ms;
	uint32_t dir;
};

struct pointer_trackers {
	struct pointer_tracker trackers[MAX_TRACKERS];
	int ntrackers;
	unsigned cur_tracker;
};

struct pointer_accelerator {
	int dpi;

	float last_velocity;
	float threshold;
	float max_accel;
	float incline;
	float speed_adjustment;

	struct pointer_trackers trackers;
};

struct coords_residue {
	float x;
	float y;
};

void
pointer_accelerator_init(struct pointer_accelerator *accel,
			 int dpi,
			 bool use_velocity_averaging);

void
pointer_accelerator_set_speed(struct pointer_accelerator *accel,
			      int32_t speed_adjustment,
			      int32_t scale);

void
pointer_accelerator_set_dpi(struct pointer_accelerator *accel,
			    int dpi,
			    uint32_t time_ms);

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms);

struct coords
accelerator_filter_linear(struct pointer_accelerator *accel,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms);

struct coords
accelerator_filter_low_dpi(struct pointer_accelerator *accel,
			   int32_t dx,
			   int32_t dy,
			   uint32_t time_ms);

void
coords_to_int(struct coords accelerated,
	      struct coords_residue *residue,
	      int32_t *dx,
	      int32_t *dy);

#endif
