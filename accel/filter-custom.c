#include "filter-custom.h"

#include <string.h>

#define CUSTOM_MOTION_TIMEOUT_MS 1000u
#define FIRST_MOTION_INTERVAL_MS 7u

static inline int32_t
q10_div(int32_t a_q10, int32_t b_q10)
{
	return (a_q10 << Q10_SHIFT) / b_q10;
}

static inline uint32_t
abs32(int32_t value)
{
	return value >= 0 ? (uint32_t)value : (uint32_t)(-value);
}

static uint32_t
isqrt_u32(uint32_t value)
{
	uint32_t bit = 1u << 30;
	uint32_t result = 0;

	while (bit > value)
		bit >>= 2;

	while (bit != 0) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}

		bit >>= 2;
	}

	return result;
}

static uint32_t
distance_q10(int32_t x_q10, int32_t y_q10)
{
	uint32_t ax = abs32(x_q10);
	uint32_t ay = abs32(y_q10);

	return isqrt_u32(ax * ax + ay * ay);
}

static void
custom_accel_function_reset(struct custom_accel_function *accel_function)
{
	accel_function->last_time_ms = 0;
	accel_function->last_delta_time_ms = FIRST_MOTION_INTERVAL_MS;
}

static void
custom_accel_function_load(struct custom_accel_function *accel_function,
			   bool configured,
			   int32_t step_q10,
			   size_t npoints,
			   const int32_t *points_q10)
{
	accel_function->configured = configured;
	accel_function->step_q10 = step_q10;
	accel_function->npoints = npoints;
	memcpy(accel_function->points_q10, points_q10, npoints * sizeof(*points_q10));
	custom_accel_function_reset(accel_function);
}

static void
custom_accel_function_load_default(struct custom_accel_function *accel_function,
				   bool configured)
{
	static const int32_t default_points_q10[2] = { 0, Q10_ONE };

	custom_accel_function_load(accel_function,
				   configured,
				   Q10_ONE,
				   2,
				   default_points_q10);
}

static bool
custom_validate_points(int32_t step_q10,
		       size_t npoints,
		       const int32_t *points_q10)
{
	if (!points_q10)
		return false;

	if (npoints < CUSTOM_NPOINTS_MIN ||
	    npoints > CUSTOM_NPOINTS_MAX)
		return false;

	if (step_q10 <= 0 || step_q10 > CUSTOM_STEP_MAX_Q10)
		return false;

	for (size_t idx = 0; idx < npoints; idx++) {
		if (points_q10[idx] < CUSTOM_POINT_MIN_Q10 ||
		    points_q10[idx] > CUSTOM_POINT_MAX_Q10)
			return false;
	}

	return true;
}

static struct custom_accel_function *
custom_accelerator_get_function(struct custom_accelerator *accelerator,
				enum custom_accel_function_type type)
{
	switch (type) {
	case CUSTOM_FALLBACK:
		return &accelerator->fallback;
	case CUSTOM_MOTION:
		return accelerator->motion.configured ? &accelerator->motion : &accelerator->fallback;
	case CUSTOM_SCROLL:
		return accelerator->scroll.configured ? &accelerator->scroll : &accelerator->fallback;
	}

	return &accelerator->fallback;
}

static int32_t
custom_accel_function_output_speed(const struct custom_accel_function *accel_function,
				   int32_t speed_q10)
{
	size_t idx;
	int32_t x0_q10;
	int32_t x1_q10;
	int32_t y0_q10;
	int32_t y1_q10;
	int32_t numerator;

	if (speed_q10 <= 0)
		return 0;

	idx = (size_t)(speed_q10 / accel_function->step_q10);
	if (idx > accel_function->npoints - 2)
		idx = accel_function->npoints - 2;

	x0_q10 = accel_function->step_q10 * (int32_t)idx;
	x1_q10 = accel_function->step_q10 * (int32_t)(idx + 1);
	y0_q10 = accel_function->points_q10[idx];
	y1_q10 = accel_function->points_q10[idx + 1];

	numerator = y0_q10 * (x1_q10 - speed_q10) +
		    y1_q10 * (speed_q10 - x0_q10);

	return (int32_t)(numerator / accel_function->step_q10);
}

static int32_t
custom_accel_function_profile(const struct custom_accel_function *accel_function,
			      int32_t speed_q10)
{
	if (speed_q10 <= 0)
		return 0;

	return q10_div(custom_accel_function_output_speed(accel_function,
							  speed_q10),
		       speed_q10);
}

static struct coords_q10
custom_accel_function_apply(struct custom_accel_function *accel_function,
			    int32_t dx,
			    int32_t dy,
			    uint32_t time_ms)
{
	uint32_t delta_time_ms =
		time_ms > accel_function->last_time_ms ? time_ms - accel_function->last_time_ms
						       : accel_function->last_delta_time_ms;
	uint32_t distance;
	int32_t speed_q10;
	int32_t speed_out_q10;
	int32_t factor_q10;

	if (delta_time_ms > CUSTOM_MOTION_TIMEOUT_MS)
		delta_time_ms = FIRST_MOTION_INTERVAL_MS;

	distance = distance_q10(dx << Q10_SHIFT, dy << Q10_SHIFT);

	accel_function->last_time_ms = time_ms;
	accel_function->last_delta_time_ms = delta_time_ms;

	speed_q10 = (int32_t)(distance / delta_time_ms);
	speed_out_q10 =
		custom_accel_function_output_speed(accel_function, speed_q10);
	factor_q10 = (speed_out_q10 * (int32_t)delta_time_ms * Q10_ONE) /
		     (int32_t)distance;

	struct coords_q10 accelerated_q10 = {
		.x_q10 = dx * factor_q10,
		.y_q10 = dy * factor_q10,
	};

	return accelerated_q10;
}

void
custom_accelerator_init(struct custom_accelerator *accelerator)
{
	memset(accelerator, 0, sizeof(*accelerator));
	custom_accel_function_load_default(&accelerator->fallback, true);
	custom_accel_function_load_default(&accelerator->motion, false);
	custom_accel_function_load_default(&accelerator->scroll, false);
}

void
custom_accelerator_restart(struct custom_accelerator *accelerator)
{
	custom_accel_function_reset(&accelerator->fallback);
	custom_accel_function_reset(&accelerator->motion);
	custom_accel_function_reset(&accelerator->scroll);
}

bool
custom_accelerator_set_points(struct custom_accelerator *accelerator,
			      enum custom_accel_function_type type,
			      int32_t step_q10,
			      size_t npoints,
			      const int32_t *points_q10)
{
	if (!custom_validate_points(step_q10, npoints, points_q10))
		return false;

	switch (type) {
	case CUSTOM_FALLBACK:
		custom_accel_function_load(&accelerator->fallback,
					   true,
					   step_q10,
					   npoints,
					   points_q10);
		return true;
	case CUSTOM_MOTION:
		custom_accel_function_load(&accelerator->motion,
					   true,
					   step_q10,
					   npoints,
					   points_q10);
		return true;
	case CUSTOM_SCROLL:
		custom_accel_function_load(&accelerator->scroll,
					   true,
					   step_q10,
					   npoints,
					   points_q10);
		return true;
	}

	return false;
}

void
custom_accelerator_clear_points(struct custom_accelerator *accelerator,
				enum custom_accel_function_type type)
{
	switch (type) {
	case CUSTOM_FALLBACK:
		custom_accel_function_load_default(&accelerator->fallback, true);
		return;
	case CUSTOM_MOTION:
		custom_accel_function_load_default(&accelerator->motion, false);
		return;
	case CUSTOM_SCROLL:
		custom_accel_function_load_default(&accelerator->scroll, false);
		return;
	}
}

int32_t
custom_accelerator_profile(struct custom_accelerator *accelerator,
			   enum custom_accel_function_type type,
			   int32_t speed_q10)
{
	const struct custom_accel_function *accel_function =
		custom_accelerator_get_function(accelerator, type);

	return custom_accel_function_profile(accel_function, speed_q10);
}

struct coords_q10
accelerator_filter_custom(struct custom_accelerator *accelerator,
			  enum custom_accel_function_type type,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms)
{
	struct coords_q10 accelerated_q10 = { 0, 0 };
	struct custom_accel_function *accel_function;

	if (dx == 0 && dy == 0)
		return accelerated_q10;

	accel_function = custom_accelerator_get_function(accelerator, type);
	return custom_accel_function_apply(accel_function, dx, dy, time_ms);
}

struct coords_q10
accelerator_filter_custom_fallback(struct custom_accelerator *accelerator,
				   int32_t dx,
				   int32_t dy,
				   uint32_t time_ms)
{
	return accelerator_filter_custom(accelerator,
					 CUSTOM_FALLBACK,
					 dx,
					 dy,
					 time_ms);
}

struct coords_q10
accelerator_filter_custom_motion(struct custom_accelerator *accelerator,
				 int32_t dx,
				 int32_t dy,
				 uint32_t time_ms)
{
	return accelerator_filter_custom(accelerator,
					 CUSTOM_MOTION,
					 dx,
					 dy,
					 time_ms);
}

struct coords_q10
accelerator_filter_custom_scroll(struct custom_accelerator *accelerator,
				 int32_t dx,
				 int32_t dy,
				 uint32_t time_ms)
{
	return accelerator_filter_custom(accelerator,
					 CUSTOM_SCROLL,
					 dx,
					 dy,
					 time_ms);
}
