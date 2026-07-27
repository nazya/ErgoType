#include "filter.h"

#include "FreeRTOS.h"
#include "task.h"

void
filter_init(struct filter_state *filter,
	    const accel_profile_cfg_t *filter_cfg,
	    uint16_t cpi)
{
	filter->profile = filter_cfg->profile;
	filter->residue = (struct coords_residue){ 0, 0 };

	switch (filter->profile) {
	case ACCEL_PROFILE_NONE:
		break;
	case ACCEL_PROFILE_ADAPTIVE:
		filter->accelerator.adaptive =
			pvPortMalloc(sizeof(*filter->accelerator.adaptive));
		configASSERT(filter->accelerator.adaptive);
		pointer_accelerator_init(filter->accelerator.adaptive,
					 cpi,
					 filter_cfg->adaptive_velocity_averaging);
		pointer_accelerator_set_speed(filter->accelerator.adaptive,
					      filter_cfg->speed,
					      filter_cfg->scale);
		pointer_accelerator_restart(filter->accelerator.adaptive,
					    (uint32_t)xTaskGetTickCount() *
						    portTICK_PERIOD_MS);
		break;
	case ACCEL_PROFILE_CUSTOM:
		custom_accel_function_init(&filter->accelerator.custom,
					   filter_cfg->custom_step,
					   filter_cfg->nr_points,
					   filter_cfg->custom_points,
					   filter_cfg->scale);
		break;
	case ACCEL_PROFILE_FLAT:
		pointer_accelerator_flat_init(&filter->accelerator.flat);
		accelerator_set_speed_flat(&filter->accelerator.flat,
					   filter_cfg->speed,
					   filter_cfg->scale);
		break;
	case ACCEL_PROFILE_MACCEL:
		maccel_init(&filter->accelerator.maccel, cpi);
		break;
	case ACCEL_PROFILE_SYNCHRONOUS:
		synchronous_accel_init(&filter->accelerator.synchronous,
				       cpi,
				       filter_cfg->sync_speed,
				       filter_cfg->motivity,
				       filter_cfg->gamma,
				       filter_cfg->smooth,
				       filter_cfg->scale);
		break;
	}
}

void
filter_process(struct filter_state *filter,
	       int32_t *dx,
	       int32_t *dy)
{
	uint32_t time_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
	struct coords accelerated;

	switch (filter->profile) {
	case ACCEL_PROFILE_NONE:
		return;
	case ACCEL_PROFILE_ADAPTIVE:
		if (filter->accelerator.adaptive->dpi >= DEFAULT_MOUSE_DPI)
			accelerated = accelerator_filter_linear(filter->accelerator.adaptive,
								*dx,
								*dy,
								time_ms);
		else
			accelerated = accelerator_filter_low_dpi(filter->accelerator.adaptive,
								 *dx,
								 *dy,
								 time_ms);
		break;
	case ACCEL_PROFILE_CUSTOM:
		accelerated = custom_accel_function_filter(&filter->accelerator.custom,
							   *dx,
							   *dy,
							   time_ms);
		break;
	case ACCEL_PROFILE_FLAT:
		accelerated = accelerator_filter_flat(&filter->accelerator.flat,
						      *dx,
						      *dy);
		break;
	case ACCEL_PROFILE_MACCEL:
		accelerated = maccel_filter(&filter->accelerator.maccel,
					    &filter->residue,
					    *dx,
					    *dy,
					    time_ms);
		break;
	case ACCEL_PROFILE_SYNCHRONOUS:
		accelerated =
			synchronous_accel_filter(&filter->accelerator.synchronous,
						 *dx,
						 *dy,
						 time_ms);
		break;
	}

	coords_to_int(accelerated, &filter->residue, dx, dy);
}
