/*
 * SPDX-License-Identifier: APSL-2.0
 *
 * Adapted from Apple IOHIDFamily's acceleration algorithm and curve data:
 * https://github.com/apple-oss-distributions/IOHIDFamily/tree/777ccd9698845aadf711e32d843c8c9b777431d9
 */

#ifndef FILTER_APPLE_H
#define FILTER_APPLE_H

#include <stdint.h>

#include "filter-adaptive.h"

struct apple_accel {
	float cpi;
	float gain_linear;
	float gain_parabolic;
	float gain_cubic;
	float gain_quartic;
	float tangent[2];
	float m[2];
	float b[2];
};

void
apple_accel_init(struct apple_accel *accel,
		 uint16_t cpi,
		 int32_t speed,
		 int32_t scale);

struct coords
apple_accel_filter(const struct apple_accel *accel,
		   int32_t dx,
		   int32_t dy);

#endif
