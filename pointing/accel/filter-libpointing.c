/*
 * Copyright Inria
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Adapted from libpointing's Darwin 16 fourth-position transfer table:
 * https://github.com/INRIA/libpointing/blob/6895505b620a74e23a008b6b75018ff562e6ace1/pointing-echomouse/darwin-16/f4.dat
 */

#include "filter-libpointing.h"

#include <math.h>

static const float darwin_16_f4[] = {
	0.0f, 0.246224f, 0.540104f, 0.873698f, 1.24701f, 1.66003f,
	2.11673f, 2.61712f, 3.15723f, 3.74102f, 4.36452f, 5.02773f,
	5.73861f, 6.48919f, 7.27949f, 8.11348f, 8.99115f, 9.90853f,
	10.8696f, 11.8743f, 12.9228f, 14.0109f, 15.1428f, 16.3143f,
	17.5335f, 18.7924f, 20.0951f, 21.4413f, 22.8313f, 24.265f,
	25.7423f, 27.2594f, 28.3516f, 29.9297f, 31.5508f, 33.2188f,
	34.9258f, 36.6758f, 38.4727f, 40.3086f, 42.1914f, 44.1172f,
	46.082f, 48.0938f, 50.1484f, 52.2461f, 54.3906f, 56.5742f,
	58.8047f, 61.0781f, 63.3945f, 65.7422f, 68.0859f, 70.4336f,
	72.7773f, 75.125f, 77.4688f, 79.8164f, 82.1641f, 84.5078f,
	86.8555f, 89.1992f, 91.5469f, 93.8906f, 96.2383f, 98.5859f,
	100.93f, 103.277f, 105.621f, 107.969f, 110.312f, 112.66f,
	115.004f, 117.352f, 119.699f, 122.043f, 124.391f, 126.734f,
	129.082f, 131.426f, 133.773f, 136.121f, 138.465f, 140.812f,
	143.156f, 145.504f, 147.848f, 150.195f, 152.539f, 154.887f,
	157.234f, 159.578f, 161.926f, 164.27f, 166.617f, 168.961f,
	171.309f, 173.656f, 176.0f, 178.348f, 180.691f, 183.039f,
	185.379f, 187.695f, 189.984f, 192.246f, 194.48f, 196.691f,
	198.875f, 201.035f, 203.172f, 205.289f, 207.383f, 209.457f,
	211.512f, 213.543f, 215.559f, 217.551f, 219.531f, 221.488f,
	223.434f, 225.355f, 227.266f, 229.16f, 231.039f, 232.902f,
	234.75f, 236.582f,
};

static float
gain_at(uint32_t index)
{
	return index == 0 ? 0.0f : darwin_16_f4[index] / index;
}

static float
gain_from_table(float index)
{
	uint32_t last = sizeof(darwin_16_f4) / sizeof(darwin_16_f4[0]) - 1;

	if (index >= last)
		return gain_at(last);

	uint32_t lower = (uint32_t)floorf(index);
	uint32_t upper = lower + 1;
	float fraction = index - lower;

	return gain_at(lower) +
	       (gain_at(upper) - gain_at(lower)) * fraction;
}

void
libpointing_accel_init(struct libpointing_accel *accel, uint16_t cpi)
{
	accel->cpi_scale = 400.0f / cpi;
	accel->previous_dx = 0;
	accel->previous_dy = 0;
}

struct coords
libpointing_accel_filter(struct libpointing_accel *accel,
			 struct coords_residue *residue,
			 int32_t dx,
			 int32_t dy)
{
	if (dx != 0) {
		if ((dx > 0 && accel->previous_dx < 0) ||
		    (dx < 0 && accel->previous_dx > 0))
			residue->x = 0.0f;
		accel->previous_dx = dx;
	}
	if (dy != 0) {
		if ((dy > 0 && accel->previous_dy < 0) ||
		    (dy < 0 && accel->previous_dy > 0))
			residue->y = 0.0f;
		accel->previous_dy = dy;
	}
	if (dx == 0 && dy == 0)
		return (struct coords){ 0.0f, 0.0f };

	float x = dx;
	float y = dy;
	float index = floorf(sqrtf(x * x + y * y)) * accel->cpi_scale;
	float gain = gain_from_table(index);

	return (struct coords) {
		.x = x * gain,
		.y = y * gain,
	};
}
