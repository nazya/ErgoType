#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hardware/spi.h"

typedef struct {
    spi_inst_t *spi;
    int8_t cs;
    uint32_t baud;
    uint8_t mode; // 0..3
} pmw3360_t;

bool pmw3360_init(pmw3360_t *dev);
void pmw3360_get_deltas(pmw3360_t *dev, int16_t *dx, int16_t *dy);
void pmw3360_set_cpi(pmw3360_t *dev, uint16_t cpi);
