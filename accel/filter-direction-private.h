#ifndef FILTER_DIRECTION_PRIVATE_H
#define FILTER_DIRECTION_PRIVATE_H

#include "filter-adaptive.h"

enum direction_bits {
	DIR_N = 1u << 0,
	DIR_NE = 1u << 1,
	DIR_E = 1u << 2,
	DIR_SE = 1u << 3,
	DIR_S = 1u << 4,
	DIR_SW = 1u << 5,
	DIR_W = 1u << 6,
	DIR_NW = 1u << 7,
	DIR_UNDEFINED = 0xffu,
};

static inline uint32_t
abs32(int32_t value)
{
	return value < 0 ? (uint32_t)-value : (uint32_t)value;
}

static inline uint32_t
direction_small(int32_t x_q10, int32_t y_q10)
{
	if (x_q10 > 0 && y_q10 > 0)
		return DIR_S | DIR_SE | DIR_E;
	if (x_q10 > 0 && y_q10 < 0)
		return DIR_N | DIR_NE | DIR_E;
	if (x_q10 < 0 && y_q10 > 0)
		return DIR_S | DIR_SW | DIR_W;
	if (x_q10 < 0 && y_q10 < 0)
		return DIR_N | DIR_NW | DIR_W;
	if (x_q10 > 0)
		return DIR_NE | DIR_E | DIR_SE;
	if (x_q10 < 0)
		return DIR_NW | DIR_W | DIR_SW;
	if (y_q10 > 0)
		return DIR_SE | DIR_S | DIR_SW;
	if (y_q10 < 0)
		return DIR_NE | DIR_N | DIR_NW;

	return DIR_UNDEFINED;
}

static inline uint32_t
direction_bits_from_r_q10(int32_t r_q10)
{
	const int32_t q10_0_1 = 102;
	const int32_t q10_0_9 = 922;
	unsigned d1 = (unsigned)((r_q10 + q10_0_9) >> Q10_SHIFT) % 8;
	unsigned d2 = (unsigned)((r_q10 + q10_0_1) >> Q10_SHIFT) % 8;

	return (1u << d1) | (1u << d2);
}

int32_t
direction_angle_from_x_cordic_q10(uint32_t ax_q10, uint32_t ay_q10);

uint32_t
direction_get_lut(int32_t x_q10, int32_t y_q10);

uint32_t
direction_get_cordic(int32_t x_q10, int32_t y_q10);

#endif
