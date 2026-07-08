#include "filter.h"

#include "FreeRTOS.h"
#include "task.h"

void
filter_init(struct filter_state *filter,
	    const struct filter_params *params,
	    uint16_t cpi)
{
	filter->cpi = cpi;
	filter->residue = (struct coords_residue){ 0, 0 };

	switch (params->profile) {
	case FILTER_PROFILE_ADAPTIVE:
		pointer_accelerator_init(&filter->accelerator.adaptive,
					 cpi,
					 params->adaptive_velocity_averaging);
		pointer_accelerator_set_speed(&filter->accelerator.adaptive,
					      params->speed_q10);
		pointer_accelerator_restart(&filter->accelerator.adaptive,
					    (uint32_t)xTaskGetTickCount() *
						    portTICK_PERIOD_MS);
		break;
	case FILTER_PROFILE_CUSTOM:
		custom_accelerator_init(&filter->accelerator.custom);
		custom_accelerator_set_points(&filter->accelerator.custom,
					      CUSTOM_FALLBACK,
					      params->custom_step_q10,
					      params->custom_npoints,
					      params->custom_points_q10);
		break;
	case FILTER_PROFILE_FLAT:
		pointer_accelerator_flat_init(&filter->accelerator.flat);
		accelerator_set_speed_flat(&filter->accelerator.flat,
					   params->speed_q10);
		break;
	}
}

void
filter_process(struct filter_state *filter,
	       const struct filter_params *params,
	       int32_t *dx,
	       int32_t *dy)
{
	uint32_t time_ms = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
	struct coords_q10 accelerated_q10;

	switch (params->profile) {
	case FILTER_PROFILE_ADAPTIVE:
		if (filter->cpi >= DEFAULT_MOUSE_DPI)
			accelerated_q10 = accelerator_filter_linear(&filter->accelerator.adaptive,
								   *dx,
								   *dy,
								   time_ms);
		else
			accelerated_q10 = accelerator_filter_linear_low_dpi(&filter->accelerator.adaptive,
									   *dx,
									   *dy,
									   time_ms);
		break;
	case FILTER_PROFILE_CUSTOM:
		accelerated_q10 = accelerator_filter_custom_fallback(&filter->accelerator.custom,
								     *dx,
								     *dy,
								     time_ms);
		break;
	case FILTER_PROFILE_FLAT:
		accelerated_q10 = accelerator_filter_flat(&filter->accelerator.flat,
							  *dx,
							  *dy);
		break;
	}

	coords_q10_to_int(accelerated_q10, &filter->residue, dx, dy);
}
