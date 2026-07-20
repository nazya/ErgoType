#include "filter-custom.h"
#include "filter-math.h"

#define MOTION_TIMEOUT_MS 1000u
#define FIRST_MOTION_TIME_INTERVAL_MS 7u

// Upstream calculates the acceleration factor as speed_out / speed_in. The previous
// Q10 port used speed_out * time / distance and made an identity curve non-identity.
// static inline int32_t
// q10_div(int32_t a_q10, int32_t b_q10)
// {
// 	return (a_q10 << Q10_SHIFT) / b_q10;
// }

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

static float
custom_accel_function_calculate_speed(struct custom_accel_function *accel_function,
				      int32_t dx,
				      int32_t dy,
				      uint32_t time_ms,
				      float *distance_out,
				      uint32_t *delta_time_out)
{
	uint32_t delta_time_ms =
		time_ms > accel_function->last_time_ms ?
			time_ms - accel_function->last_time_ms :
			accel_function->last_delta_time_ms;

	if (delta_time_ms > MOTION_TIMEOUT_MS)
		delta_time_ms = FIRST_MOTION_TIME_INTERVAL_MS;

	float distance = distance_float((float)(dx * accel_function->scale),
					(float)(dy * accel_function->scale));

	accel_function->last_time_ms = time_ms;
	accel_function->last_delta_time_ms = delta_time_ms;
	*distance_out = distance;
	*delta_time_out = delta_time_ms;

	return distance / (float)delta_time_ms;
}

static float
custom_accel_function_profile(const struct custom_accel_function *accel_function,
			      float speed_in,
			      uint32_t delta_time_ms,
			      float distance)
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
	return (float)slope / (float)accel_function->step +
	       (float)intercept * (float)delta_time_ms / (float)distance;
}

struct coords
custom_accel_function_filter(struct custom_accel_function *accel_function,
			     int32_t dx,
			     int32_t dy,
			     uint32_t time_ms)
{
	struct coords accelerated = { 0.0f, 0.0f };
	float distance;
	uint32_t delta_time_ms;
	float speed;
	float factor;

	if (dx == 0 && dy == 0)
		return accelerated;

	speed = custom_accel_function_calculate_speed(accel_function,
						     dx,
						     dy,
						     time_ms,
						     &distance,
						     &delta_time_ms);
	if (distance == 0.0f)
		return (struct coords){ 0.0f, 0.0f };

	factor = custom_accel_function_profile(accel_function,
					       speed,
					       delta_time_ms,
					       distance);

	accelerated = (struct coords) {
		.x = (float)dx * factor,
		.y = (float)dy * factor,
	};

	return accelerated;
}
