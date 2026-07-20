#ifndef FILTER_CUSTOM_H
#define FILTER_CUSTOM_H

#include <stddef.h>
#include <stdint.h>

#include "filter-adaptive.h"

struct custom_accel_function {
	uint32_t last_time_ms;
	uint32_t last_delta_time_ms;
	int32_t scale;
	int32_t step;
	size_t npoints;
	const int32_t *points;
};

void
custom_accel_function_init(struct custom_accel_function *accel_function,
			   int32_t step,
			   size_t npoints,
			   const int32_t *points,
			   int32_t scale);

struct coords
custom_accel_function_filter(struct custom_accel_function *accel_function,
			     int32_t dx,
			     int32_t dy,
			     uint32_t time_ms);

#endif
