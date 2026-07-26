#ifndef ERGOTYPE_PLATFORM_I2C_H
#define ERGOTYPE_PLATFORM_I2C_H

#include <stddef.h>
#include <stdint.h>

int platform_i2c_init(uint8_t bus, int sda, int scl, uint32_t frequency);
int platform_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len);

#endif
