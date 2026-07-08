#include "filter-adaptive-private.h"

static int32_t
pointer_accel_profile_linear_low_dpi(const struct pointer_accelerator *accel,
				     int32_t speed_q10)
{
	int32_t dpi_factor_q10 =
		(accel->dpi * Q10_ONE) / DEFAULT_MOUSE_DPI;
	int32_t max_accel_q10 = q10_div(accel->max_accel_q10, dpi_factor_q10);
	int32_t threshold_q10 = q10_mul(accel->threshold_q10, dpi_factor_q10);
	int32_t factor_q10;

	if (speed_q10 < 72) {
		factor_q10 =
			q10_mul(10 * Q10_ONE, speed_q10) + 307;
	} else if (speed_q10 < threshold_q10) {
		factor_q10 = Q10_ONE;
	} else {
		factor_q10 = q10_mul(accel->incline_q10,
					   speed_q10 - threshold_q10) +
			     Q10_ONE;
	}

	if (factor_q10 > max_accel_q10)
		factor_q10 = max_accel_q10;

	return factor_q10;
}

struct coords_q10
accelerator_filter_linear_low_dpi(struct pointer_accelerator *accel,
				  int32_t dx,
				  int32_t dy,
				  uint32_t time_ms)
{
	struct coords_q10 unaccelerated_q10 = {
		.x_q10 = dx * Q10_ONE,
		.y_q10 = dy * Q10_ONE,
	};
	int32_t velocity_q10 =
		trackers_velocity(accel, unaccelerated_q10, time_ms);
	int32_t factor_q10 =
		calculate_acceleration_simpsons(pointer_accel_profile_linear_low_dpi,
					       accel,
					       velocity_q10,
					       accel->last_velocity_q10);

	accel->last_velocity_q10 = velocity_q10;

	struct coords_q10 accelerated_q10 = {
		.x_q10 = q10_mul(unaccelerated_q10.x_q10, factor_q10),
		.y_q10 = q10_mul(unaccelerated_q10.y_q10, factor_q10),
	};

	return accelerated_q10;
}
