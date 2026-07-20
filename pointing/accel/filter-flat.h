#ifndef FILTER_FLAT_H
#define FILTER_FLAT_H

#include <stdint.h>

#include "filter-adaptive.h"

struct pointer_accelerator_flat {
	float factor;
	float speed_adjustment;
};

void
pointer_accelerator_flat_init(struct pointer_accelerator_flat *accel_filter);

void
accelerator_set_speed_flat(struct pointer_accelerator_flat *accel_filter,
			   int32_t speed_adjustment,
			   int32_t scale);

struct coords
accelerator_filter_flat(const struct pointer_accelerator_flat *accel_filter,
			int32_t dx,
			int32_t dy);

#endif
