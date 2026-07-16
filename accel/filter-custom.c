#include "filter-custom.h"
#include "filter-math.h"

#include <limits.h>

#define MOTION_TIMEOUT_MS 1000u
#define FIRST_MOTION_TIME_INTERVAL_MS 7u

// Upstream calculates the acceleration factor as speed_out / speed_in. The previous
// Q10 port used speed_out * time / distance and made an identity curve non-identity.
// static inline int32_t
// q10_div(int32_t a_q10, int32_t b_q10)
// {
// 	return (a_q10 << Q10_SHIFT) / b_q10;
// }

static uint32_t
fraction_to_fixed(uint32_t numerator,
		  uint32_t denominator,
		  uint32_t first_bit)
{
	uint32_t result = 0;

	for (uint32_t bit = first_bit; bit != 0; bit >>= 1) {
		numerator <<= 1;
		if (numerator >= denominator) {
			numerator -= denominator;
			result |= bit;
		}
	}

	return result;
}

static int32_t
ratio_to_q10(int32_t numerator, uint32_t denominator)
{
	if (abs32(numerator) <= INT32_MAX / Q10_ONE)
		return numerator * Q10_ONE / (int32_t)denominator;

	int32_t whole = numerator / (int32_t)denominator;
	int32_t remainder = numerator % (int32_t)denominator;
	uint32_t fraction = fraction_to_fixed(abs32(remainder),
					      denominator,
					      (uint32_t)Q10_ONE >> 1);

	return whole * Q10_ONE + (remainder < 0 ? -(int32_t)fraction :
							 (int32_t)fraction);
}

static int32_t
intercept_factor_q10(int32_t intercept,
		     uint32_t delta_time_ms,
		     uint32_t distance)
{
	uint32_t multiplier = delta_time_ms * (uint32_t)Q10_ONE;
	if (abs32(intercept) <= INT32_MAX / multiplier)
		return intercept * (int32_t)multiplier / (int32_t)distance;

	int32_t whole = intercept / (int32_t)distance;
	int32_t remainder = intercept % (int32_t)distance;
	uint32_t fraction_q20 = fraction_to_fixed(abs32(remainder),
						  distance,
						  1u << 19);
	int32_t fraction =
		(int32_t)((fraction_q20 * delta_time_ms) >> Q10_SHIFT);
	int32_t result = whole * (int32_t)multiplier;

	result += remainder < 0 ? -fraction : fraction;

	return result;
}

void
custom_accel_function_init(struct custom_accel_function *accel_function,
			   int32_t step,
			   size_t npoints,
			   const int32_t *points,
			   int32_t scale)
{
	accel_function->scale = scale;
	accel_function->step = step;
	accel_function->npoints = npoints;
	accel_function->points = points;
	accel_function->last_time_ms = 0;
	accel_function->last_delta_time_ms = FIRST_MOTION_TIME_INTERVAL_MS;
}

static int32_t
custom_accel_function_calculate_speed(struct custom_accel_function *accel_function,
				      int32_t dx,
				      int32_t dy,
				      uint32_t time_ms,
				      uint32_t *distance_out,
				      uint32_t *delta_time_out)
{
	uint32_t delta_time_ms =
		time_ms > accel_function->last_time_ms ?
			time_ms - accel_function->last_time_ms :
			accel_function->last_delta_time_ms;

	if (delta_time_ms > MOTION_TIMEOUT_MS)
		delta_time_ms = FIRST_MOTION_TIME_INTERVAL_MS;

	uint32_t distance = distance_scaled(dx * accel_function->scale,
					    dy * accel_function->scale);

	accel_function->last_time_ms = time_ms;
	accel_function->last_delta_time_ms = delta_time_ms;
	*distance_out = distance;
	*delta_time_out = delta_time_ms;

	return (int32_t)(distance / delta_time_ms);
}

static int32_t
custom_accel_function_profile(const struct custom_accel_function *accel_function,
			      int32_t speed_in,
			      uint32_t delta_time_ms,
			      uint32_t distance)
{
	size_t idx;
	int32_t y0;
	int32_t y1;

	idx = (size_t)(speed_in / accel_function->step);
	if (idx > accel_function->npoints - 2)
		idx = accel_function->npoints - 2;

	y0 = accel_function->points[idx];
	y1 = accel_function->points[idx + 1];

	// The weighted interpolation used upstream overflows after fixed-point scaling.
	// int32_t x0 = accel_function->step * (int32_t)idx;
	// int32_t x1 = accel_function->step * (int32_t)(idx + 1);
	// int32_t numerator = y0 * (x1 - speed_in) +
	//                     y1 * (speed_in - x0);
	// int32_t speed_out = numerator / accel_function->step;
	// return q10_div(speed_out, speed_in);
	int32_t slope = y1 - y0;
	int32_t intercept = y0 - (int32_t)idx * slope;

	// Step and points stay in config scale; that scale cancels from the factor.
	return ratio_to_q10(slope, (uint32_t)accel_function->step) +
	       intercept_factor_q10(intercept, delta_time_ms, distance);
}

struct coords_q10
custom_accel_function_filter(struct custom_accel_function *accel_function,
			     int32_t dx,
			     int32_t dy,
			     uint32_t time_ms)
{
	struct coords_q10 accelerated_q10 = { 0, 0 };
	uint32_t distance;
	uint32_t delta_time_ms;
	int32_t speed;
	int32_t factor_q10;

	if (dx == 0 && dy == 0)
		return accelerated_q10;

	speed = custom_accel_function_calculate_speed(accel_function,
						     dx,
						     dy,
						     time_ms,
						     &distance,
						     &delta_time_ms);
	if (distance == 0)
		return (struct coords_q10){ 0, 0 };

	factor_q10 = custom_accel_function_profile(accel_function,
						   speed,
						   delta_time_ms,
						   distance);

	accelerated_q10 = (struct coords_q10) {
		.x_q10 = dx * factor_q10,
		.y_q10 = dy * factor_q10,
	};

	return accelerated_q10;
}
