/*
 * Copyright Inria
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from libpointing's Darwin 16 fourth-position transfer table:
 * https://github.com/INRIA/libpointing/blob/6895505b620a74e23a008b6b75018ff562e6ace1/pointing-echomouse/darwin-16/f4.dat
 */

#ifndef FILTER_LIBPOINTING_H
#define FILTER_LIBPOINTING_H

#include <stdint.h>

#include "filter-adaptive.h"

struct libpointing_accel {
	float cpi_scale;
	int32_t previous_dx;
	int32_t previous_dy;
};

void
libpointing_accel_init(struct libpointing_accel *accel, uint16_t cpi);

struct coords
libpointing_accel_filter(struct libpointing_accel *accel,
			 struct coords_residue *residue,
			 int32_t dx,
			 int32_t dy);

#endif
