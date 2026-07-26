#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "jconfig.h"

bool pmw3360_init(const pmw33xx_cfg_t *cfg);
void pmw3360_get_deltas(const pmw33xx_cfg_t *cfg, int16_t *dx, int16_t *dy);
void pmw3360_set_cpi(const pmw33xx_cfg_t *cfg);
