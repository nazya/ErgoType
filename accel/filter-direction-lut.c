#include "filter-direction-private.h"

uint32_t
direction_get_lut(int32_t x_q10, int32_t y_q10)
{
	return direction_get_cordic(x_q10, y_q10);
}
