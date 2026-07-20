#include "filter-flat.h"

#include <assert.h>

#define FLAT_MIN_FACTOR (5.0f / 1024.0f)

void
pointer_accelerator_flat_init(struct pointer_accelerator_flat *accel_filter)
{
	accel_filter->factor = 1.0f;
	accel_filter->speed_adjustment = 0.0f;
}

void
accelerator_set_speed_flat(struct pointer_accelerator_flat *accel_filter,
			   int32_t speed_adjustment,
			   int32_t scale)
{
	float speed = (float)speed_adjustment / (float)scale;

	assert(speed >= -1.0f && speed <= 1.0f);

	accel_filter->factor = 1.0f + speed;
	if (accel_filter->factor < FLAT_MIN_FACTOR)
		accel_filter->factor = FLAT_MIN_FACTOR;
	accel_filter->speed_adjustment = speed;
}

struct coords
accelerator_filter_flat(const struct pointer_accelerator_flat *accel_filter,
			int32_t dx,
			int32_t dy)
{
	struct coords accelerated = {
		.x = (float)dx * accel_filter->factor,
		.y = (float)dy * accel_filter->factor,
	};

	return accelerated;
}
