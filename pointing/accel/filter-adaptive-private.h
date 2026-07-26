#ifndef FILTER_ADAPTIVE_PRIVATE_H
#define FILTER_ADAPTIVE_PRIVATE_H

#include "filter-adaptive.h"

void
trackers_feed(struct pointer_trackers *trackers,
	      struct coords_q10 unaccelerated,
	      uint32_t time_ms);

int32_t
trackers_velocity(struct pointer_trackers *trackers, uint32_t time_ms);

int32_t
calculate_acceleration_simpsons(
	const struct pointer_accelerator *accel,
	int32_t (*profile)(const struct pointer_accelerator *accel,
			   int32_t speed_q10),
	int32_t velocity_q10,
	int32_t last_velocity_q10);

struct coords_q10
normalize_for_dpi(int32_t dx, int32_t dy, int dpi);

int32_t
q10_div(int32_t a_q10, int32_t b_q10);

#endif
