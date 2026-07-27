/*
 * SPDX-License-Identifier: APSL-2.0
 *
 * Adapted from Apple IOHIDFamily's acceleration algorithm and curve data:
 * https://github.com/apple-oss-distributions/IOHIDFamily/tree/777ccd9698845aadf711e32d843c8c9b777431d9
 */

#include "filter-apple.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

#define APPLE_FIXED_ONE 65536.0f
#define APPLE_FRAME_RATE 67.0f
#define APPLE_CURSOR_SCALE (96.0f / APPLE_FRAME_RATE)

struct apple_curve {
	int32_t index;
	int32_t gain_linear;
	int32_t gain_parabolic;
	int32_t gain_cubic;
	int32_t gain_quartic;
	int32_t tangent_linear;
	int32_t tangent_root;
};

static const struct apple_curve apple_curves[] = {
	{ 0, 65536, 0, 0, 0, 524288, 0 },
	{ 8192, 60293, 26214, 5243, 0, 537395, 1245184 },
	{ 32768, 60948, 36045, 6554, 0, 543949, 1179648 },
	{ 45056, 61604, 46531, 7864, 0, 550502, 1114112 },
	{ 57344, 62259, 57672, 9830, 0, 557056, 1048576 },
	{ 65536, 62915, 69468, 11796, 0, 563610, 983040 },
	{ 98304, 63570, 81920, 14418, 0, 570163, 917504 },
	{ 131072, 64225, 95027, 17695, 0, 576717, 851968 },
	{ 163840, 64881, 108790, 21627, 0, 583270, 786432 },
	{ 196608, 65536, 123208, 26214, 0, 589824, 786432 },
};

void
apple_accel_init(struct apple_accel *accel,
		 uint16_t cpi,
		 int32_t speed,
		 int32_t scale)
{
	float index = (float)speed / scale;
	size_t lower = 0;
	size_t upper;

	while (lower + 1 < sizeof(apple_curves) / sizeof(apple_curves[0]) &&
	       index >= apple_curves[lower + 1].index / APPLE_FIXED_ONE)
		lower++;

	upper = lower + 1 < sizeof(apple_curves) / sizeof(apple_curves[0]) ?
			lower + 1 :
			lower;

	float ratio = 0.0f;
	if (upper != lower) {
		float low_index = apple_curves[lower].index / APPLE_FIXED_ONE;
		float high_index = apple_curves[upper].index / APPLE_FIXED_ONE;
		ratio = (index - low_index) / (high_index - low_index);
	}

#define INTERPOLATE(field)                                                     \
	((apple_curves[lower].field +                                          \
	  (apple_curves[upper].field - apple_curves[lower].field) * ratio) /   \
	 APPLE_FIXED_ONE)

	accel->cpi = cpi;
	accel->gain_linear = INTERPOLATE(gain_linear);
	accel->gain_parabolic = INTERPOLATE(gain_parabolic);
	accel->gain_cubic = INTERPOLATE(gain_cubic);
	accel->gain_quartic = INTERPOLATE(gain_quartic);
	float tangent_linear = INTERPOLATE(tangent_linear);
	float tangent_root = INTERPOLATE(tangent_root);

#undef INTERPOLATE

	accel->tangent[0] = FLT_MAX;
	accel->tangent[1] = FLT_MAX;

	if (tangent_linear != 0.0f) {
		float x = tangent_linear;
		float parabolic = accel->gain_parabolic * x;
		float cubic = accel->gain_cubic * x;
		float quartic = accel->gain_quartic * x;
		float y = accel->gain_linear * x +
			  parabolic * parabolic +
			  cubic * cubic * cubic +
			  quartic * quartic * quartic * quartic;

		accel->m[0] =
			accel->gain_linear +
			2.0f * x * accel->gain_parabolic *
				accel->gain_parabolic +
			3.0f * x * x * accel->gain_cubic *
				accel->gain_cubic * accel->gain_cubic +
			4.0f * x * x * x * accel->gain_quartic *
				accel->gain_quartic * accel->gain_quartic *
				accel->gain_quartic;
		accel->b[0] = y - accel->m[0] * x;
		accel->tangent[0] = x;

		if (tangent_root != 0.0f) {
			x = tangent_root;
			y = accel->m[0] * x + accel->b[0];
			accel->m[1] = 2.0f * y * accel->m[0];
			accel->b[1] = y * y - accel->m[1] * x;
			accel->tangent[1] = x;
		}
	}
}

struct coords
apple_accel_filter(const struct apple_accel *accel,
		   int32_t dx,
		   int32_t dy)
{
	if (dx == 0 && dy == 0)
		return (struct coords){ 0.0f, 0.0f };

	float x = dx;
	float y = dy;
	float velocity = floorf(sqrtf(x * x + y * y));
	float standardized = velocity / (accel->cpi / APPLE_FRAME_RATE);
	float speed;

	if (standardized <= accel->tangent[0]) {
		float parabolic = accel->gain_parabolic * standardized;
		float cubic = accel->gain_cubic * standardized;
		float quartic = accel->gain_quartic * standardized;
		speed = accel->gain_linear * standardized +
			parabolic * parabolic +
			cubic * cubic * cubic +
			quartic * quartic * quartic * quartic;
	} else if (standardized <= accel->tangent[1]) {
		speed = accel->m[0] * standardized + accel->b[0];
	} else {
		speed = sqrtf(accel->m[1] * standardized + accel->b[1]);
	}

	float factor = speed * APPLE_CURSOR_SCALE / velocity;

	return (struct coords) {
		.x = x * factor,
		.y = y * factor,
	};
}
