#pragma once

#include <stdint.h>

#include "filter-adaptive.h"
#include "filter-custom.h"
#include "filter-flat.h"
#include "filter-maccel.h"
#include "filter-synchronous.h"
#include "jconfig.h"

struct filter_state {
	uint8_t profile;
	struct coords_residue residue;
	union {
		struct pointer_accelerator_flat flat;
		struct pointer_accelerator *adaptive;
		struct custom_accel_function custom;
		struct maccel maccel;
		struct synchronous_accel synchronous;
	} accelerator;
};

void
filter_init(struct filter_state *filter,
	    const accel_profile_cfg_t *filter_cfg,
	    uint16_t cpi);

void
filter_process(struct filter_state *filter,
	       int32_t *dx,
	       int32_t *dy);
