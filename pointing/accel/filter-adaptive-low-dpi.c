#include "filter-adaptive-private.h"

static float
pointer_accel_profile_linear_low_dpi(const struct pointer_accelerator *accel,
				     float speed)
{
	float dpi_factor = (float)accel->dpi / (float)DEFAULT_MOUSE_DPI;
	float max_accel = accel->max_accel / dpi_factor;
	float threshold = accel->threshold * dpi_factor;
	float factor;

	if (speed < 0.07f) {
		factor = 10.0f * speed + 0.3f;
	} else if (speed < threshold) {
		factor = 1.0f;
	} else {
		float speed_delta = speed - threshold;

		// The direct Q10 product overflows before the capped result does.
		// factor_q10 = q10_mul(accel->incline_q10,
		//                         speed_delta_q10) + Q10_ONE;
		factor = accel->incline * speed_delta + 1.0f;
	}

	if (factor > max_accel)
		factor = max_accel;

	return factor;
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
					       pointer_accel_profile_linear_low_dpi,
					       velocity,
					       accel->last_velocity);

	accel->last_velocity = velocity;

	return factor;
}

struct coords
accelerator_filter_low_dpi(struct pointer_accelerator *accel,
			   int32_t dx,
			   int32_t dy,
			   uint32_t time_ms)
{
	struct coords unaccelerated = {
		.x = (float)dx,
		.y = (float)dy,
	};
	float factor =
		calculate_acceleration_factor(accel, unaccelerated, time_ms);

	// The Q10 scale in unaccelerated_q10 cancels q10_mul's scale division.
	// struct coords_q10 accelerated_q10 = {
	// 	.x_q10 = q10_mul(unaccelerated_q10.x_q10, factor_q10),
	// 	.y_q10 = q10_mul(unaccelerated_q10.y_q10, factor_q10),
	// };
	struct coords accelerated = {
		.x = (float)dx * factor,
		.y = (float)dy * factor,
	};

	return accelerated;
}
