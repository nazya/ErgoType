#include "filter-direction-private.h"

static const int32_t cordic_atan_q10[] = {
	1024, 605, 319, 162, 81, 41, 20, 10,
	5,    3,   1,   1,
};

int32_t
direction_angle_from_x_cordic_q10(uint32_t ax_q10, uint32_t ay_q10)
{
	if (ax_q10 == 0)
		return 2 * Q10_ONE;

	int32_t x = (int32_t)ax_q10;
	int32_t y = (int32_t)ay_q10;
	int32_t angle_q10 = 0;

	for (unsigned i = 0; i < sizeof(cordic_atan_q10) /
				 sizeof(cordic_atan_q10[0]);
	     i++) {
		int32_t x_shift = x >> i;
		int32_t y_shift = y >> i;

		if (y > 0) {
			x += y_shift;
			y -= x_shift;
			angle_q10 += cordic_atan_q10[i];
		} else {
			x -= y_shift;
			y += x_shift;
			angle_q10 -= cordic_atan_q10[i];
		}
	}

	if (angle_q10 < 0)
		angle_q10 = 0;
	if (angle_q10 > 2 * Q10_ONE)
		angle_q10 = 2 * Q10_ONE;

	return angle_q10;
}

uint32_t
direction_get_cordic(int32_t x_q10, int32_t y_q10)
{
	const int32_t threshold = 2 * Q10_ONE;

	if ((int32_t)abs32(x_q10) < threshold &&
	    (int32_t)abs32(y_q10) < threshold)
		return direction_small(x_q10, y_q10);

	uint32_t ax_q10 = abs32(x_q10);
	uint32_t ay_q10 = abs32(y_q10);
	int32_t angle_from_x_q10 =
		direction_angle_from_x_cordic_q10(ax_q10, ay_q10);
	int32_t r_q10;

	if (x_q10 >= 0 && y_q10 < 0) {
		r_q10 = 2 * Q10_ONE - angle_from_x_q10;
	} else if (x_q10 >= 0 && y_q10 >= 0) {
		r_q10 = 2 * Q10_ONE + angle_from_x_q10;
	} else if (x_q10 < 0 && y_q10 >= 0) {
		r_q10 = 6 * Q10_ONE - angle_from_x_q10;
	} else {
		r_q10 = 6 * Q10_ONE + angle_from_x_q10;
		if (r_q10 >= 8 * Q10_ONE)
			r_q10 -= 8 * Q10_ONE;
	}

	return direction_bits_from_r_q10(r_q10);
}
