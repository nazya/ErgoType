#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "jconfig.h"

bool pmw3389_init(const pmw33xx_cfg_t *cfg);
void pmw3389_get_deltas(const pmw33xx_cfg_t *cfg, int16_t *dx, int16_t *dy);
void pmw3389_set_cpi(const pmw33xx_cfg_t *cfg);
