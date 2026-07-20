#ifndef FILTER_ADAPTIVE_PRIVATE_H
#define FILTER_ADAPTIVE_PRIVATE_H

#include "filter-adaptive.h"

void
trackers_feed(struct pointer_trackers *trackers,
	      struct coords unaccelerated,
	      uint32_t time_ms);

float
trackers_velocity(struct pointer_trackers *trackers, uint32_t time_ms);

float
calculate_acceleration_simpsons(
	const struct pointer_accelerator *accel,
	float (*profile)(const struct pointer_accelerator *accel,
			 float speed),
	float velocity,
	float last_velocity);

struct coords
normalize_for_dpi(int32_t dx, int32_t dy, int dpi);

#endif
