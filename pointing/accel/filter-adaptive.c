#include "filter-adaptive-private.h"
#include "filter-math.h"

#include <assert.h>
#include <string.h>

#define DEFAULT_THRESHOLD_Q10 ((int32_t)410) /* 0.4 */
#define MINIMUM_THRESHOLD_Q10 ((int32_t)205) /* 0.2 */
#define DEFAULT_ACCELERATION_Q10 ((int32_t)(2 * Q10_ONE))
#define DEFAULT_INCLINE_Q10 ((int32_t)1126)   /* 1.1 */
#define QUARTER_Q10 ((int32_t)(Q10_ONE / 4))
#define ONE_AND_HALF_Q10 ((int32_t)(Q10_ONE + Q10_ONE / 2))
#define THREE_QUARTERS_Q10 ((int32_t)(Q10_ONE * 3 / 4))
#define MAX_VELOCITY_DIFF_Q10 ((int32_t)Q10_ONE)
#define MOTION_TIMEOUT_MS 1000u

enum directions {
	N = 1u << 0,
	NE = 1u << 1,
	E = 1u << 2,
	SE = 1u << 3,
	S = 1u << 4,
	SW = 1u << 5,
	W = 1u << 6,
	NW = 1u << 7,
	UNDEFINED_DIRECTION = 0xffu,
};

static const int32_t cordic_atan_q10[] = {
	1024, 605, 319, 162, 81, 41, 20, 10,
	5,    3,   1,   1,
};

static int32_t
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

static uint32_t
xy_get_direction(int32_t x_q10, int32_t y_q10)
{
	const int32_t threshold = 2 * Q10_ONE;
	uint32_t dir = UNDEFINED_DIRECTION;

	if ((int32_t)abs32(x_q10) < threshold &&
	    (int32_t)abs32(y_q10) < threshold) {
		if (x_q10 > 0 && y_q10 > 0)
			dir = S | SE | E;
		else if (x_q10 > 0 && y_q10 < 0)
			dir = N | NE | E;
		else if (x_q10 < 0 && y_q10 > 0)
			dir = S | SW | W;
		else if (x_q10 < 0 && y_q10 < 0)
			dir = N | NW | W;
		else if (x_q10 > 0)
			dir = NE | E | SE;
		else if (x_q10 < 0)
			dir = NW | W | SW;
		else if (y_q10 > 0)
			dir = SE | S | SW;
		else if (y_q10 < 0)
			dir = NE | N | NW;
	} else {
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

		const int32_t q10_0_1 = 102;
		const int32_t q10_0_9 = 922;
		unsigned d1 =
			(unsigned)((r_q10 + q10_0_9) >> Q10_SHIFT) % 8;
		unsigned d2 =
			(unsigned)((r_q10 + q10_0_1) >> Q10_SHIFT) % 8;

		dir = (1u << d1) | (1u << d2);
	}

	return dir;
}

static inline struct pointer_tracker *
trackers_by_offset(struct pointer_trackers *trackers, unsigned offset)
{
	unsigned idx = (trackers->cur_tracker + trackers->ntrackers - offset) %
		       trackers->ntrackers;
	return &trackers->trackers[idx];
}

int32_t
q10_div(int32_t a_q10, int32_t b_q10)
{
	return (a_q10 << Q10_SHIFT) / b_q10;
}

static void
trackers_init(struct pointer_trackers *trackers, int ntrackers)
{
	trackers->ntrackers = ntrackers;
	trackers->cur_tracker = 0;
}

static void
trackers_reset(struct pointer_trackers *trackers, uint32_t time_ms)
{
	for (int offset = 1; offset < trackers->ntrackers; offset++) {
		struct pointer_tracker *tracker =
			trackers_by_offset(trackers, (unsigned)offset);

		tracker->time_ms = 0;
		tracker->dir = 0;
		tracker->delta.x_q10 = 0;
		tracker->delta.y_q10 = 0;
	}

	struct pointer_tracker *tracker = trackers_by_offset(trackers, 0);
	tracker->time_ms = time_ms;
	tracker->dir = UNDEFINED_DIRECTION;
}

void
trackers_feed(struct pointer_trackers *trackers,
	      struct coords_q10 unaccelerated,
	      uint32_t time_ms)
{
	for (int i = 0; i < trackers->ntrackers; i++) {
		trackers->trackers[i].delta.x_q10 += unaccelerated.x_q10;
		trackers->trackers[i].delta.y_q10 += unaccelerated.y_q10;
	}

	trackers->cur_tracker =
		(trackers->cur_tracker + 1) % trackers->ntrackers;
	trackers->trackers[trackers->cur_tracker].delta.x_q10 = 0;
	trackers->trackers[trackers->cur_tracker].delta.y_q10 = 0;
	trackers->trackers[trackers->cur_tracker].time_ms = time_ms;
	trackers->trackers[trackers->cur_tracker].dir =
		xy_get_direction(unaccelerated.x_q10, unaccelerated.y_q10);
}

static inline int32_t
calculate_trackers_velocity(const struct pointer_tracker *tracker,
			    uint32_t time_ms)
{
	uint32_t dt_ms = time_ms - tracker->time_ms;

	if (dt_ms == 0)
		dt_ms = 1;

	return (int32_t)(distance_scaled(tracker->delta.x_q10,
					 tracker->delta.y_q10) / dt_ms);
}

static inline int32_t
trackers_velocity_after_timeout(const struct pointer_tracker *tracker)
{
	return calculate_trackers_velocity(
		tracker,
		tracker->time_ms + MOTION_TIMEOUT_MS);
}

int32_t
trackers_velocity(struct pointer_trackers *trackers, uint32_t time_ms)
{
	int32_t result_q10 = 0;
	int32_t initial_velocity_q10 = 0;
	uint32_t dir;

	dir = trackers_by_offset(trackers, 0)->dir;

	for (unsigned offset = 1;
	     offset < (unsigned)trackers->ntrackers;
	     offset++) {
		const struct pointer_tracker *tracker =
			trackers_by_offset(trackers, offset);

		if (tracker->time_ms > time_ms)
			break;

		uint32_t dt_ms = time_ms - tracker->time_ms;
		if (dt_ms > MOTION_TIMEOUT_MS) {
			if (offset == 1)
				result_q10 =
					trackers_velocity_after_timeout(tracker);
			break;
		}

		int32_t velocity_q10 =
			calculate_trackers_velocity(tracker, time_ms);

		dir &= tracker->dir;
		if (dir == 0) {
			if (offset == 1)
				result_q10 = velocity_q10;
			break;
		}

		if (initial_velocity_q10 == 0 || offset <= 2) {
			result_q10 = initial_velocity_q10 = velocity_q10;
		} else {
			int32_t velocity_diff_q10 =
				initial_velocity_q10 > velocity_q10
					? initial_velocity_q10 - velocity_q10
					: velocity_q10 - initial_velocity_q10;
			if (velocity_diff_q10 > MAX_VELOCITY_DIFF_Q10)
				break;

			result_q10 = velocity_q10;
		}
	}

	return result_q10;
}

int32_t
calculate_acceleration_simpsons(
	const struct pointer_accelerator *accel,
	int32_t (*profile)(const struct pointer_accelerator *accel,
			   int32_t speed_q10),
	int32_t velocity_q10,
	int32_t last_velocity_q10)
{
	int32_t factor_q10;

	factor_q10 = profile(accel, velocity_q10);
	factor_q10 += profile(accel, last_velocity_q10);
	factor_q10 += 4 *
		      profile(accel, (velocity_q10 + last_velocity_q10) / 2);

	return (int32_t)(factor_q10 / 6);
}

struct coords_q10
normalize_for_dpi(int32_t dx, int32_t dy, int dpi)
{
	int32_t x = dx * DEFAULT_MOUSE_DPI;
	int32_t y = dy * DEFAULT_MOUSE_DPI;
	int32_t x_whole = x / dpi;
	int32_t y_whole = y / dpi;
	int32_t x_remainder = x % dpi;
	int32_t y_remainder = y % dpi;
	struct coords_q10 normalized_q10 = {
		.x_q10 = x_whole * Q10_ONE +
			   (x_remainder * Q10_ONE) / dpi,
		.y_q10 = y_whole * Q10_ONE +
			   (y_remainder * Q10_ONE) / dpi,
	};

	return normalized_q10;
}

void
pointer_accelerator_init(struct pointer_accelerator *accel,
			 int dpi,
			 bool use_velocity_averaging)
{
	memset(accel, 0, sizeof(*accel));
	accel->dpi = dpi;
	trackers_init(&accel->trackers, use_velocity_averaging ? 16 : 2);
	accel->threshold_q10 = DEFAULT_THRESHOLD_Q10;
	accel->max_accel_q10 = DEFAULT_ACCELERATION_Q10;
	accel->incline_q10 = DEFAULT_INCLINE_Q10;
}

void
pointer_accelerator_set_speed(struct pointer_accelerator *accel,
			      int32_t speed_adjustment_q10)
{
	assert(speed_adjustment_q10 >= -Q10_ONE &&
	       speed_adjustment_q10 <= Q10_ONE);

	accel->threshold_q10 =
		DEFAULT_THRESHOLD_Q10 -
		q10_mul(QUARTER_Q10, speed_adjustment_q10);
	if (accel->threshold_q10 < MINIMUM_THRESHOLD_Q10)
		accel->threshold_q10 = MINIMUM_THRESHOLD_Q10;

	accel->max_accel_q10 =
		DEFAULT_ACCELERATION_Q10 +
		q10_mul(ONE_AND_HALF_Q10, speed_adjustment_q10);
	accel->incline_q10 =
		DEFAULT_INCLINE_Q10 +
		q10_mul(THREE_QUARTERS_Q10, speed_adjustment_q10);
	accel->speed_adjustment_q10 = speed_adjustment_q10;
}

void
pointer_accelerator_set_dpi(struct pointer_accelerator *accel,
			    int dpi,
			    uint32_t time_ms)
{
	accel->dpi = dpi;
	accel->last_velocity_q10 = 0;
	pointer_accelerator_restart(accel, time_ms);
}

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms)
{
	trackers_reset(&accel->trackers, time_ms);
}

void
coords_q10_to_int(struct coords_q10 accelerated_q10,
		  struct coords_residue *residue,
		  int32_t *dx,
		  int32_t *dy)
{
	int32_t x_q10 = accelerated_q10.x_q10 + residue->x_q10;
	int32_t y_q10 = accelerated_q10.y_q10 + residue->y_q10;
	int32_t integer_x = x_q10 >= 0 ? (int32_t)(x_q10 >> Q10_SHIFT)
				       : -(int32_t)((-x_q10) >> Q10_SHIFT);
	int32_t integer_y = y_q10 >= 0 ? (int32_t)(y_q10 >> Q10_SHIFT)
				       : -(int32_t)((-y_q10) >> Q10_SHIFT);

	residue->x_q10 = x_q10 - integer_x * Q10_ONE;
	residue->y_q10 = y_q10 - integer_y * Q10_ONE;

	*dx = integer_x;
	*dy = integer_y;
}
