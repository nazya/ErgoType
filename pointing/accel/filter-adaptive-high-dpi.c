#include "filter-adaptive-private.h"

static int32_t
pointer_accel_profile_linear(const struct pointer_accelerator *accel,
			     int32_t speed_q10)
{
	int32_t factor_q10;

	if (speed_q10 < 72) {
		factor_q10 =
			q10_mul(10 * Q10_ONE, speed_q10) + 307;
	} else if (speed_q10 < accel->threshold_q10) {
		factor_q10 = Q10_ONE;
	} else {
		int32_t speed_delta_q10 = speed_q10 - accel->threshold_q10;

		// The direct Q10 product overflows before the capped result does.
		// factor_q10 = q10_mul(accel->incline_q10,
		//                         speed_delta_q10) + Q10_ONE;
		factor_q10 =
			(speed_delta_q10 / Q10_ONE) * accel->incline_q10 +
			(((speed_delta_q10 % Q10_ONE) * accel->incline_q10) >>
			 Q10_SHIFT) +
			Q10_ONE;
	}

	if (factor_q10 > accel->max_accel_q10)
		factor_q10 = accel->max_accel_q10;

	return factor_q10;
}

static int32_t
apply_factor_for_dpi(int32_t delta, int32_t factor_q10, int dpi)
{
	int32_t scaled_q10 = delta * factor_q10;

	return (scaled_q10 / dpi) * DEFAULT_MOUSE_DPI +
	       ((scaled_q10 % dpi) * DEFAULT_MOUSE_DPI) / dpi;
}

static inline int32_t
calculate_acceleration_factor(struct pointer_accelerator *accel,
			      struct coords_q10 unaccelerated_q10,
			      uint32_t time_ms)
{
	trackers_feed(&accel->trackers, unaccelerated_q10, time_ms);
	int32_t velocity_q10 = trackers_velocity(&accel->trackers, time_ms);
	int32_t factor_q10 =
		calculate_acceleration_simpsons(accel,
					       pointer_accel_profile_linear,
					       velocity_q10,
					       accel->last_velocity_q10);

	accel->last_velocity_q10 = velocity_q10;

	return factor_q10;
}

struct coords_q10
accelerator_filter_linear(struct pointer_accelerator *accel,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms)
{
	struct coords_q10 normalized_q10 =
		normalize_for_dpi(dx, dy, accel->dpi);
	int32_t factor_q10 =
		calculate_acceleration_factor(accel, normalized_q10, time_ms);

	// Applying the factor after Q10 normalization overflows the intermediate.
	// struct coords_q10 accelerated_q10 = {
	// 	.x_q10 = q10_mul(normalized_q10.x_q10, factor_q10),
	// 	.y_q10 = q10_mul(normalized_q10.y_q10, factor_q10),
	// };
	struct coords_q10 accelerated_q10 = {
		.x_q10 = apply_factor_for_dpi(dx, factor_q10, accel->dpi),
		.y_q10 = apply_factor_for_dpi(dy, factor_q10, accel->dpi),
	};

	return accelerated_q10;
}
