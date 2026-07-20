#include "filter-adaptive-private.h"
#include "filter-math.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#define DEFAULT_THRESHOLD 0.4f
#define MINIMUM_THRESHOLD 0.2f
#define DEFAULT_ACCELERATION 2.0f
#define DEFAULT_INCLINE 1.1f
#define MAX_VELOCITY_DIFF 1.0f
#define FILTER_PI 3.14159265358979323846f
#define FILTER_1_PI 0.31830988618379067154f
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

static uint32_t
xy_get_direction(float x, float y)
{
	uint32_t dir = UNDEFINED_DIRECTION;

	if (absf32(x) < 2.0f && absf32(y) < 2.0f) {
		if (x > 0.0f && y > 0.0f)
			dir = S | SE | E;
		else if (x > 0.0f && y < 0.0f)
			dir = N | NE | E;
		else if (x < 0.0f && y > 0.0f)
			dir = S | SW | W;
		else if (x < 0.0f && y < 0.0f)
			dir = N | NW | W;
		else if (x > 0.0f)
			dir = NE | E | SE;
		else if (x < 0.0f)
			dir = NW | W | SW;
		else if (y > 0.0f)
			dir = SE | S | SW;
		else if (y < 0.0f)
			dir = NE | N | NW;
	} else {
		float r = atan2f(y, x);
		r = fmodf(r + 2.5f * FILTER_PI, 2.0f * FILTER_PI);
		r *= 4.0f * FILTER_1_PI;
		unsigned d1 = (unsigned)((int)(r + 0.9f)) % 8;
		unsigned d2 = (unsigned)((int)(r + 0.1f)) % 8;

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
		tracker->delta.x = 0.0f;
		tracker->delta.y = 0.0f;
	}

	struct pointer_tracker *tracker = trackers_by_offset(trackers, 0);
	tracker->time_ms = time_ms;
	tracker->dir = UNDEFINED_DIRECTION;
}

void
trackers_feed(struct pointer_trackers *trackers,
	      struct coords unaccelerated,
	      uint32_t time_ms)
{
	for (int i = 0; i < trackers->ntrackers; i++) {
		trackers->trackers[i].delta.x += unaccelerated.x;
		trackers->trackers[i].delta.y += unaccelerated.y;
	}

	trackers->cur_tracker =
		(trackers->cur_tracker + 1) % trackers->ntrackers;
	trackers->trackers[trackers->cur_tracker].delta.x = 0.0f;
	trackers->trackers[trackers->cur_tracker].delta.y = 0.0f;
	trackers->trackers[trackers->cur_tracker].time_ms = time_ms;
	trackers->trackers[trackers->cur_tracker].dir =
		xy_get_direction(unaccelerated.x, unaccelerated.y);
}

static inline float
calculate_trackers_velocity(const struct pointer_tracker *tracker,
			    uint32_t time_ms)
{
	uint32_t dt_ms = time_ms - tracker->time_ms;

	if (dt_ms == 0)
		dt_ms = 1;

	return distance_float(tracker->delta.x, tracker->delta.y) / (float)dt_ms;
}

static inline float
trackers_velocity_after_timeout(const struct pointer_tracker *tracker)
{
	return calculate_trackers_velocity(
		tracker,
		tracker->time_ms + MOTION_TIMEOUT_MS);
}

float
trackers_velocity(struct pointer_trackers *trackers, uint32_t time_ms)
{
	float result = 0.0f;
	float initial_velocity = 0.0f;
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
				result =
					trackers_velocity_after_timeout(tracker);
			break;
		}

		float velocity =
			calculate_trackers_velocity(tracker, time_ms);

		dir &= tracker->dir;
		if (dir == 0) {
			if (offset == 1)
				result = velocity;
			break;
		}

		if (initial_velocity == 0.0f || offset <= 2) {
			result = initial_velocity = velocity;
		} else {
			float velocity_diff =
				initial_velocity > velocity
					? initial_velocity - velocity
					: velocity - initial_velocity;
			if (velocity_diff > MAX_VELOCITY_DIFF)
				break;

			result = velocity;
		}
	}

	return result;
}

float
calculate_acceleration_simpsons(
	const struct pointer_accelerator *accel,
	float (*profile)(const struct pointer_accelerator *accel,
			 float speed),
	float velocity,
	float last_velocity)
{
	float factor;

	factor = profile(accel, velocity);
	factor += profile(accel, last_velocity);
	factor += 4.0f * profile(accel, (velocity + last_velocity) / 2.0f);

	return factor / 6.0f;
}

struct coords
normalize_for_dpi(int32_t dx, int32_t dy, int dpi)
{
	struct coords normalized = {
		.x = (float)dx * (float)DEFAULT_MOUSE_DPI / (float)dpi,
		.y = (float)dy * (float)DEFAULT_MOUSE_DPI / (float)dpi,
	};

	return normalized;
}

void
pointer_accelerator_init(struct pointer_accelerator *accel,
			 int dpi,
			 bool use_velocity_averaging)
{
	memset(accel, 0, sizeof(*accel));
	accel->dpi = dpi;
	trackers_init(&accel->trackers, use_velocity_averaging ? 16 : 2);
	accel->threshold = DEFAULT_THRESHOLD;
	accel->max_accel = DEFAULT_ACCELERATION;
	accel->incline = DEFAULT_INCLINE;
}

void
pointer_accelerator_set_speed(struct pointer_accelerator *accel,
			      int32_t speed_adjustment,
			      int32_t scale)
{
	float speed = (float)speed_adjustment / (float)scale;

	assert(speed >= -1.0f && speed <= 1.0f);

	accel->threshold = DEFAULT_THRESHOLD - 0.25f * speed;
	if (accel->threshold < MINIMUM_THRESHOLD)
		accel->threshold = MINIMUM_THRESHOLD;

	accel->max_accel = DEFAULT_ACCELERATION + speed * 1.5f;
	accel->incline = DEFAULT_INCLINE + speed * 0.75f;
	accel->speed_adjustment = speed;
}

void
pointer_accelerator_set_dpi(struct pointer_accelerator *accel,
			    int dpi,
			    uint32_t time_ms)
{
	accel->dpi = dpi;
	accel->last_velocity = 0.0f;
	pointer_accelerator_restart(accel, time_ms);
}

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms)
{
	trackers_reset(&accel->trackers, time_ms);
}

void
coords_to_int(struct coords accelerated,
	      struct coords_residue *residue,
	      int32_t *dx,
	      int32_t *dy)
{
	float x = accelerated.x + residue->x;
	float y = accelerated.y + residue->y;
	int32_t integer_x = (int32_t)x;
	int32_t integer_y = (int32_t)y;

	residue->x = x - (float)integer_x;
	residue->y = y - (float)integer_y;

	*dx = integer_x;
	*dy = integer_y;
}
