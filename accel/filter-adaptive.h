#ifndef FILTER_ADAPTIVE_H
#define FILTER_ADAPTIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "filter-math.h"

#define DEFAULT_MOUSE_DPI 1000
#define MAX_TRACKERS 16

struct coords_q10 {
	int32_t x_q10;
	int32_t y_q10;
};

struct pointer_tracker {
	struct coords_q10 delta;
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

	int32_t last_velocity_q10;
	int32_t threshold_q10;
	int32_t max_accel_q10;
	int32_t incline_q10;
	int32_t speed_adjustment_q10;

	struct pointer_trackers trackers;
};

struct coords_residue {
	int32_t x_q10;
	int32_t y_q10;
};

void
pointer_accelerator_init(struct pointer_accelerator *accel,
			 int dpi,
			 bool use_velocity_averaging);

void
pointer_accelerator_set_speed(struct pointer_accelerator *accel,
			      int32_t speed_adjustment_q10);

void
pointer_accelerator_set_dpi(struct pointer_accelerator *accel,
			    int dpi,
			    uint32_t time_ms);

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms);

struct coords_q10
accelerator_filter_linear(struct pointer_accelerator *accel,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms);

struct coords_q10
accelerator_filter_low_dpi(struct pointer_accelerator *accel,
			   int32_t dx,
			   int32_t dy,
			   uint32_t time_ms);

void
coords_q10_to_int(struct coords_q10 accelerated_q10,
		  struct coords_residue *residue,
		  int32_t *dx,
		  int32_t *dy);

#endif
