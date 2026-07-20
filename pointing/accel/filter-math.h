#ifndef FILTER_MATH_H
#define FILTER_MATH_H

#include <math.h>
#include <stdint.h>

static inline uint32_t
abs32(int32_t value)
{
	return value >= 0 ? (uint32_t)value : (uint32_t)(-value);
}

static inline float
absf32(float value)
{
	return value >= 0.0f ? value : -value;
}

/*
static uint32_t
isqrt_u32(uint32_t value)
{
	uint32_t bit = 1u << 30;
	uint32_t result = 0;

	while (bit > value)
		bit >>= 2;

	while (bit != 0) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}

		bit >>= 2;
	}

	return result;
}
*/

static inline uint32_t
distance_scaled(int32_t x_scaled, int32_t y_scaled)
{
	uint32_t ax = abs32(x_scaled);
	uint32_t ay = abs32(y_scaled);
	uint32_t max = ax > ay ? ax : ay;
	uint32_t min = ax > ay ? ay : ax;

	// Original exact calculation overflows after Q10 scaling.
	// return isqrt_u32(ax * ax + ay * ay);

	// More accurate shift/add approximation, maximum error is about 4%.
	// return max - (max >> 5) - (max >> 7) +
	//        (min >> 2) + (min >> 3) + (min >> 6) + (min >> 7);

	return max + (min >> 2) + (min >> 3);
}

static inline float
distance_float(float x, float y)
{
	return sqrtf(x * x + y * y);
}

#endif
