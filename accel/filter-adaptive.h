#ifndef FILTER_ADAPTIVE_H
#define FILTER_ADAPTIVE_H

#include <stdbool.h>
#include <stdint.h>

#define Q10_SHIFT 10
#define Q10_ONE ((int32_t)(1u << Q10_SHIFT))
#define DEFAULT_MOUSE_DPI 1000
#define MAX_TRACKERS 16

enum direction_mode {
	DIRECTION_MODE_LUT = 0,
	DIRECTION_MODE_CORDIC = 1,
};

struct coords_q10 {
	int32_t x_q10;
	int32_t y_q10;
};

struct pointer_tracker {
	struct coords_q10 delta;
	uint32_t time_ms;
	uint32_t dir;
};

struct pointer_accelerator {
	int dpi;
	int ntrackers;
	unsigned cur_tracker;

	int32_t last_velocity_q10;
	int32_t threshold_q10;
	int32_t max_accel_q10;
	int32_t incline_q10;
	int32_t speed_adjustment_q10;
	enum direction_mode direction_mode;

	struct pointer_tracker trackers[MAX_TRACKERS];
};

struct coords_residue {
	int32_t x_q10;
	int32_t y_q10;
};

void
pointer_accelerator_init(struct pointer_accelerator *accel,
			 int dpi,
			 bool use_velocity_averaging);

void
pointer_accelerator_set_speed(struct pointer_accelerator *accel,
			      int32_t speed_adjustment_q10);

void
pointer_accelerator_set_dpi(struct pointer_accelerator *accel,
			    int dpi,
			    uint32_t time_ms);

void
pointer_accelerator_set_direction_mode(struct pointer_accelerator *accel,
				       enum direction_mode direction_mode);

void
pointer_accelerator_restart(struct pointer_accelerator *accel, uint32_t time_ms);

struct coords_q10
accelerator_filter_linear(struct pointer_accelerator *accel,
			  int32_t dx,
			  int32_t dy,
			  uint32_t time_ms);

struct coords_q10
accelerator_filter_linear_low_dpi(struct pointer_accelerator *accel,
				  int32_t dx,
				  int32_t dy,
				  uint32_t time_ms);

void
coords_q10_to_int(struct coords_q10 accelerated_q10,
		  struct coords_residue *residue,
		  int32_t *dx,
		  int32_t *dy);

#endif
