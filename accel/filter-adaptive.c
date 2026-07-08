#include "filter-adaptive-private.h"
#include "filter-direction-private.h"

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

static inline struct pointer_tracker *
trackers_by_offset(struct pointer_accelerator *accel, unsigned offset)
{
	unsigned idx = (accel->cur_tracker + accel->ntrackers - offset) %
		       accel->ntrackers;
	return &accel->trackers[idx];
}

int32_t
q10_mul(int32_t a_q10, int32_t b_q10)
{
	return (a_q10 * b_q10) >> Q10_SHIFT;
}

int32_t
q10_div(int32_t a_q10, int32_t b_q10)
{
	return (a_q10 << Q10_SHIFT) / b_q10;
}

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

static uint32_t
distance_q10(int32_t x_q10, int32_t y_q10)
{
	uint32_t ax = abs32(x_q10);
	uint32_t ay = abs32(y_q10);

	return isqrt_u32(ax * ax + ay * ay);
}

static void
trackers_feed(struct pointer_accelerator *accel,
	      struct coords_q10 unaccelerated,
	      uint32_t time_ms)
{
	for (int i = 0; i < accel->ntrackers; i++) {
		accel->trackers[i].delta.x_q10 += unaccelerated.x_q10;
		accel->trackers[i].delta.y_q10 += unaccelerated.y_q10;
	}

	accel->cur_tracker = (accel->cur_tracker + 1) % accel->ntrackers;
	accel->trackers[accel->cur_tracker].delta.x_q10 = 0;
	accel->trackers[accel->cur_tracker].delta.y_q10 = 0;
	accel->trackers[accel->cur_tracker].time_ms = time_ms;
	accel->trackers[accel->cur_tracker].dir = accel->direction_mode ==
							  DIRECTION_MODE_CORDIC
						  ? direction_get_cordic(
								unaccelerated.x_q10,
								unaccelerated.y_q10)
						  : direction_get_lut(
								unaccelerated.x_q10,
								unaccelerated.y_q10);
}

static inline int32_t
tracker_velocity(const struct pointer_tracker *tracker, uint32_t time_ms)
{
	uint32_t dt_ms = time_ms - tracker->time_ms;

	if (dt_ms == 0)
		dt_ms = 1;

	return (int32_t)(distance_q10(tracker->delta.x_q10,
				      tracker->delta.y_q10) / dt_ms);
}

static inline int32_t
tracker_velocity_after_timeout(const struct pointer_tracker *tracker)
{
	return tracker_velocity(
		tracker,
		tracker->time_ms + MOTION_TIMEOUT_MS);
}

int32_t
trackers_velocity(struct pointer_accelerator *accel,
		  struct coords_q10 unaccelerated,
		  uint32_t time_ms)
{
	int32_t result_q10 = 0;
	int32_t initial_velocity_q10 = 0;
	uint32_t dir;

	trackers_feed(accel, unaccelerated, time_ms);

	dir = trackers_by_offset(accel, 0)->dir;

	for (unsigned offset = 1; offset < (unsigned)accel->ntrackers; offset++) {
		const struct pointer_tracker *tracker =
			trackers_by_offset(accel, offset);

		if (tracker->time_ms > time_ms)
			break;

		uint32_t dt_ms = time_ms - tracker->time_ms;
		if (dt_ms > MOTION_TIMEOUT_MS) {
			if (offset == 1)
				result_q10 =
					tracker_velocity_after_timeout(tracker);
			break;
		}

		int32_t velocity_q10 =
			tracker_velocity(tracker, time_ms);

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
	int32_t (*profile)(const struct pointer_accelerator *accel,
			   int32_t speed_q10),
	const struct pointer_accelerator *accel,
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
	struct coords_q10 normalized_q10 = {
		.x_q10 = (dx * DEFAULT_MOUSE_DPI * Q10_ONE) / dpi,
		.y_q10 = (dy * DEFAULT_MOUSE_DPI * Q10_ONE) / dpi,
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
	accel->ntrackers = use_velocity_averaging ? 16 : 2;
	accel->threshold_q10 = DEFAULT_THRESHOLD_Q10;
	accel->max_accel_q10 = DEFAULT_ACCELERATION_Q10;
	accel->incline_q10 = DEFAULT_INCLINE_Q10;
	accel->direction_mode = DIRECTION_MODE_LUT;
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
pointer_accelerator_set_direction_mode(struct pointer_accelerator *accel,
				       enum direction_mode direction_mode)
{
	accel->direction_mode = direction_mode;
}

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms)
{
	for (int offset = 1; offset < accel->ntrackers; offset++) {
		struct pointer_tracker *tracker =
			trackers_by_offset(accel, (unsigned)offset);

		tracker->time_ms = 0;
		tracker->dir = 0;
		tracker->delta.x_q10 = 0;
		tracker->delta.y_q10 = 0;
	}

	struct pointer_tracker *tracker = trackers_by_offset(accel, 0);
	tracker->time_ms = time_ms;
	tracker->dir = DIR_UNDEFINED;
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

	residue->x_q10 = x_q10 - (integer_x << Q10_SHIFT);
	residue->y_q10 = y_q10 - (integer_y << Q10_SHIFT);

	*dx = integer_x;
	*dy = integer_y;
}
