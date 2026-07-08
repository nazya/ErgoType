#ifndef FILTER_CUSTOM_H
#define FILTER_CUSTOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "filter-adaptive.h"

#define CUSTOM_NPOINTS_MIN 2
#define CUSTOM_NPOINTS_MAX 64
#define CUSTOM_POINT_MIN_Q10 0
#define CUSTOM_POINT_MAX_Q10 ((int32_t)(10000 * Q10_ONE))
#define CUSTOM_STEP_MAX_Q10 ((int32_t)(10000 * Q10_ONE))

enum custom_accel_function_type {
	CUSTOM_FALLBACK = 0,
	CUSTOM_MOTION = 1,
	CUSTOM_SCROLL = 2,
};

struct custom_accel_function {
	uint32_t last_time_ms;
	uint32_t last_delta_time_ms;
	int32_t step_q10;
	size_t npoints;
	bool configured;
	int32_t points_q10[CUSTOM_NPOINTS_MAX];
};

struct custom_accelerator {
	struct custom_accel_function fallback;
	struct custom_accel_function motion;
	struct custom_accel_function scroll;
};

void
custom_accelerator_init(struct custom_accelerator *accelerator);

void
custom_accelerator_restart(struct custom_accelerator *accelerator);

bool
custom_accelerator_set_points(struct custom_accelerator *accelerator,
			      enum custom_accel_function_type type,
			      int32_t step_q10,
			      size_t npoints,
			      const int32_t *points_q10);

void
custom_accelerator_clear_points(struct custom_accelerator *accelerator,
				enum custom_accel_function_type type);

int32_t
custom_accelerator_profile(struct custom_accelerator *accelerator,
			   enum custom_accel_function_type type,
			   int32_t speed_q10);

struct coords_q10
accelerator_filter_custom(struct custom_accelerator *accelerator,
			  enum custom_accel_function_type type,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms);

struct coords_q10
accelerator_filter_custom_fallback(struct custom_accelerator *accelerator,
				   int32_t dx,
				   int32_t dy,
				   uint32_t time_ms);

struct coords_q10
accelerator_filter_custom_motion(struct custom_accelerator *accelerator,
				 int32_t dx,
				 int32_t dy,
				 uint32_t time_ms);

struct coords_q10
accelerator_filter_custom_scroll(struct custom_accelerator *accelerator,
				 int32_t dx,
				 int32_t dy,
				 uint32_t time_ms);

#endif
