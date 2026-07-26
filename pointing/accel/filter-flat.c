#include "filter-flat.h"

#include <assert.h>

#define FLAT_MIN_FACTOR_Q10 ((int32_t)5)

void
pointer_accelerator_flat_init(struct pointer_accelerator_flat *accel_filter)
{
	accel_filter->factor_q10 = Q10_ONE;
	accel_filter->speed_adjustment_q10 = 0;
}

void
accelerator_set_speed_flat(struct pointer_accelerator_flat *accel_filter,
			   int32_t speed_adjustment_q10)
{
	assert(speed_adjustment_q10 >= -Q10_ONE &&
	       speed_adjustment_q10 <= Q10_ONE);

	accel_filter->factor_q10 = Q10_ONE + speed_adjustment_q10;
	if (accel_filter->factor_q10 < FLAT_MIN_FACTOR_Q10)
		accel_filter->factor_q10 = FLAT_MIN_FACTOR_Q10;
	accel_filter->speed_adjustment_q10 = speed_adjustment_q10;
}

struct coords_q10
accelerator_filter_flat(const struct pointer_accelerator_flat *accel_filter,
			int32_t dx,
			int32_t dy)
{
	struct coords_q10 accelerated_q10 = {
		.x_q10 = dx * accel_filter->factor_q10,
		.y_q10 = dy * accel_filter->factor_q10,
	};

	return accelerated_q10;
}
