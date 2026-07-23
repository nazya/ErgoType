#ifndef ERGOTYPE_PLATFORM_I2C_H
#define ERGOTYPE_PLATFORM_I2C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct platform_i2c platform_i2c_t;

extern platform_i2c_t platform_i2c0;
extern platform_i2c_t platform_i2c1;

uint platform_i2c_init(platform_i2c_t *i2c, uint baud);
int platform_i2c_write_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t len,
    bool nostop);
int platform_i2c_write_read_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t dst_len);

#endif
