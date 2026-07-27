/*
 * Copyright 2024 burkfers (@burkfers)
 * Copyright 2024 Wimads (@wimads)
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from:
 * https://github.com/burkfers/qmk_userspace_features/blob/2d761d0a01299efcd75372d403ce6a3043059e82/maccel/maccel.c
 */

#include "filter-maccel.h"

#include <math.h>

#define MACCEL_TAKEOFF 2.0f
#define MACCEL_GROWTH_RATE 0.25f
#define MACCEL_OFFSET 2.2f
#define MACCEL_LIMIT 0.2f
#define MACCEL_UPPER_LIMIT 1.0f
#define MACCEL_DEFAULT_INTERVAL_MS 1u
#define MACCEL_RESIDUE_TIMEOUT_MS 200u

void
maccel_init(struct maccel *accel, uint16_t cpi)
{
	accel->cpi_scale = 1000.0f / cpi;
	accel->last_time_ms = 0;
}

struct coords
maccel_filter(struct maccel *accel,
	      struct coords_residue *residue,
	      int32_t dx,
	      int32_t dy,
	      uint32_t time_ms)
{
	if (dx == 0 && dy == 0)
		return (struct coords){ 0.0f, 0.0f };

	uint32_t delta_time_ms = accel->last_time_ms ?
					 time_ms - accel->last_time_ms :
					 MACCEL_DEFAULT_INTERVAL_MS;
	accel->last_time_ms = time_ms;
	if (delta_time_ms == 0)
		delta_time_ms = 1;

	if (delta_time_ms > MACCEL_RESIDUE_TIMEOUT_MS) {
		residue->x = 0.0f;
		residue->y = 0.0f;
	}
	if ((dx > 0 && residue->x < 0.0f) ||
	    (dx < 0 && residue->x > 0.0f))
		residue->x = 0.0f;
	if ((dy > 0 && residue->y < 0.0f) ||
	    (dy < 0 && residue->y > 0.0f))
		residue->y = 0.0f;

	float x = dx;
	float y = dy;
	float distance = sqrtf(x * x + y * y);
	float velocity = accel->cpi_scale * distance / delta_time_ms;
	float factor = MACCEL_UPPER_LIMIT -
		       (MACCEL_UPPER_LIMIT - MACCEL_LIMIT) /
			       powf(1.0f + expf(MACCEL_TAKEOFF *
					       (velocity - MACCEL_OFFSET)),
				    MACCEL_GROWTH_RATE / MACCEL_TAKEOFF);

	return (struct coords) {
		.x = x * factor,
		.y = y * factor,
	};
}
