/*
 * Copyright 2024 burkfers (@burkfers)
 * Copyright 2024 Wimads (@wimads)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from:
 * https://github.com/burkfers/qmk_userspace_features/blob/2d761d0a01299efcd75372d403ce6a3043059e82/maccel/maccel.h
 */

#ifndef FILTER_MACCEL_H
#define FILTER_MACCEL_H

#include <stdint.h>

#include "filter-adaptive.h"

struct maccel {
	float cpi_scale;
	uint32_t last_time_ms;
};

void
maccel_init(struct maccel *accel, uint16_t cpi);

struct coords
maccel_filter(struct maccel *accel,
	      struct coords_residue *residue,
	      int32_t dx,
	      int32_t dy,
	      uint32_t time_ms);

#endif
