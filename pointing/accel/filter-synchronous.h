/*
 * Copyright (c) 2020 a1xd
 * SPDX-License-Identifier: MIT
 *
 * Adapted from Raw Accel's Synchronous profile:
 * https://github.com/RawAccelOfficial/rawaccel/blob/master/common/accel-synchronous.hpp
 */

#ifndef FILTER_SYNCHRONOUS_H
#define FILTER_SYNCHRONOUS_H

#include <stdint.h>

#include "filter-adaptive.h"

struct synchronous_accel {
	float cpi_scale;
	float log_motivity;
	float gamma_constant;
	float log_sync_speed;
	float sync_speed;
	float sharpness;
	float reciprocal_sharpness;
	float minimum_sensitivity;
	float maximum_sensitivity;
	uint32_t last_time_ms;
};

void
synchronous_accel_init(struct synchronous_accel *accel,
		       uint16_t cpi,
		       int32_t sync_speed,
		       int32_t motivity,
		       int32_t gamma,
		       int32_t smooth,
		       int32_t scale);

struct coords
synchronous_accel_filter(struct synchronous_accel *accel,
			 int32_t dx,
			 int32_t dy,
			 uint32_t time_ms);

#endif
