/*
 * Copyright (c) 2020 a1xd
 * SPDX-License-Identifier: MIT
 *
 * Adapted from Raw Accel's Synchronous profile:
 * https://github.com/RawAccelOfficial/rawaccel/blob/master/common/accel-synchronous.hpp
 */

#include "filter-synchronous.h"

#include <math.h>

#define SYNCHRONOUS_DEFAULT_INTERVAL_MS 1.0f
#define SYNCHRONOUS_MIN_INTERVAL_MS 1.0f
#define SYNCHRONOUS_MAX_INTERVAL_MS 100.0f

void
synchronous_accel_init(struct synchronous_accel *accel,
		       uint16_t cpi,
		       int32_t sync_speed,
		       int32_t motivity,
		       int32_t gamma,
		       int32_t smooth,
		       int32_t scale)
{
	float motivity_f = (float)motivity / scale;
	float smooth_f = (float)smooth / scale;

	accel->cpi_scale = 1000.0f / cpi;
	accel->log_motivity = logf(motivity_f);
	accel->gamma_constant =
		((float)gamma / scale) / accel->log_motivity;
	accel->sync_speed = (float)sync_speed / scale;
	accel->log_sync_speed = logf(accel->sync_speed);
	accel->sharpness = smooth == 0 ? 16.0f : 0.5f / smooth_f;
	accel->reciprocal_sharpness = 1.0f / accel->sharpness;
	accel->minimum_sensitivity = 1.0f / motivity_f;
	accel->maximum_sensitivity = motivity_f;
	accel->last_time_ms = 0;
}

static float
synchronous_sensitivity(const struct synchronous_accel *accel, float speed)
{
	float log_difference = logf(speed) - accel->log_sync_speed;

	if (accel->sharpness >= 16.0f) {
		float log_space = accel->gamma_constant * log_difference;

		if (log_space < -1.0f)
			return accel->minimum_sensitivity;
		if (log_space > 1.0f)
			return accel->maximum_sensitivity;
		return expf(log_space * accel->log_motivity);
	}

	if (speed == accel->sync_speed)
		return 1.0f;

	float log_space = fabsf(accel->gamma_constant * log_difference);
	float exponent =
		powf(tanhf(powf(log_space, accel->sharpness)),
		     accel->reciprocal_sharpness);
	if (log_difference < 0.0f)
		exponent = -exponent;

	return expf(exponent * accel->log_motivity);
}

struct coords
synchronous_accel_filter(struct synchronous_accel *accel,
			 int32_t dx,
			 int32_t dy,
			 uint32_t time_ms)
{
	if (dx == 0 && dy == 0)
		return (struct coords){ 0.0f, 0.0f };

	float delta_time_ms = SYNCHRONOUS_DEFAULT_INTERVAL_MS;
	if (accel->last_time_ms)
		delta_time_ms = time_ms - accel->last_time_ms;
	accel->last_time_ms = time_ms;

	if (delta_time_ms < SYNCHRONOUS_MIN_INTERVAL_MS)
		delta_time_ms = SYNCHRONOUS_MIN_INTERVAL_MS;
	if (delta_time_ms > SYNCHRONOUS_MAX_INTERVAL_MS)
		delta_time_ms = SYNCHRONOUS_MAX_INTERVAL_MS;

	float x = dx;
	float y = dy;
	float distance = sqrtf(x * x + y * y);
	float speed = distance / delta_time_ms * accel->cpi_scale;
	float factor = synchronous_sensitivity(accel, speed);

	return (struct coords) {
		.x = x * factor,
		.y = y * factor,
	};
}
