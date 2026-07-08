#ifndef FILTER_FLAT_H
#define FILTER_FLAT_H

#include <stdbool.h>
#include <stdint.h>

#include "filter-adaptive.h"

struct pointer_accelerator_flat {
	int32_t factor_q10;
	int32_t speed_adjustment_q10;
};

void
pointer_accelerator_flat_init(struct pointer_accelerator_flat *accel_filter);

void
accelerator_set_speed_flat(struct pointer_accelerator_flat *accel_filter,
			   int32_t speed_adjustment_q10);

struct coords_q10
accelerator_filter_flat(const struct pointer_accelerator_flat *accel_filter,
			int32_t dx,
			int32_t dy);

struct coords_q10
accelerator_filter_scroll_flat(const struct pointer_accelerator_flat *accel_filter,
			       int32_t dx,
			       int32_t dy,
			       bool is_wheel);

#endif
