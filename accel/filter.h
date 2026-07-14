#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "filter-adaptive.h"
#include "filter-custom.h"
#include "filter-flat.h"

#define FILTER_Q10(num, den) ((int32_t)((((num) * Q10_ONE) + ((den) / 2)) / (den)))

enum filter_profile {
	FILTER_PROFILE_FLAT = 0,
	FILTER_PROFILE_ADAPTIVE,
	FILTER_PROFILE_CUSTOM,
};

struct filter_params {
	enum filter_profile profile;
	int32_t speed_q10;
	int32_t custom_step_q10;
	const int32_t *custom_points_q10;
	size_t custom_npoints;
	bool adaptive_velocity_averaging;
};

struct filter_state {
	uint16_t cpi;
	struct coords_residue residue;
	union {
		struct pointer_accelerator_flat flat;
		struct pointer_accelerator adaptive;
		struct custom_accelerator custom;
	} accelerator;
};

void
filter_init(struct filter_state *filter,
	    const struct filter_params *params,
	    uint16_t cpi);

void
filter_process(struct filter_state *filter,
	       const struct filter_params *params,
	       uint32_t time_ms,
	       int32_t *dx,
	       int32_t *dy);
