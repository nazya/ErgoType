#include "filter-adaptive-private.h"

static float
pointer_accel_profile_linear(const struct pointer_accelerator *accel,
			     float speed)
{
	float factor;

	if (speed < 0.07f) {
		factor = 10.0f * speed + 0.3f;
	} else if (speed < accel->threshold) {
		factor = 1.0f;
	} else {
		float speed_delta = speed - accel->threshold;

		// The direct Q10 product overflows before the capped result does.
		// factor_q10 = q10_mul(accel->incline_q10,
		//                         speed_delta_q10) + Q10_ONE;
		factor = accel->incline * speed_delta + 1.0f;
	}

	if (factor > accel->max_accel)
		factor = accel->max_accel;

	return factor;
}

static float
apply_factor_for_dpi(int32_t delta, float factor, int dpi)
{
	return (float)delta * factor * (float)DEFAULT_MOUSE_DPI / (float)dpi;
}

static inline float
calculate_acceleration_factor(struct pointer_accelerator *accel,
			      struct coords unaccelerated,
			      uint32_t time_ms)
{
	trackers_feed(&accel->trackers, unaccelerated, time_ms);
	float velocity = trackers_velocity(&accel->trackers, time_ms);
	float factor =
		calculate_acceleration_simpsons(accel,
					       pointer_accel_profile_linear,
					       velocity,
					       accel->last_velocity);

	accel->last_velocity = velocity;

	return factor;
}

struct coords
accelerator_filter_linear(struct pointer_accelerator *accel,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms)
{
	struct coords normalized =
		normalize_for_dpi(dx, dy, accel->dpi);
	float factor =
		calculate_acceleration_factor(accel, normalized, time_ms);

	// Applying the factor after Q10 normalization overflows the intermediate.
	// struct coords_q10 accelerated_q10 = {
	// 	.x_q10 = q10_mul(normalized_q10.x_q10, factor_q10),
	// 	.y_q10 = q10_mul(normalized_q10.y_q10, factor_q10),
	// };
	struct coords accelerated = {
		.x = apply_factor_for_dpi(dx, factor, accel->dpi),
		.y = apply_factor_for_dpi(dy, factor, accel->dpi),
	};

	return accelerated;
}
