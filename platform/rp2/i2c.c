#include "hardware/i2c.h"
#include "platform/i2c.h"

struct platform_i2c {
    i2c_inst_t *hw;
};

platform_i2c_t platform_i2c0 = { .hw = i2c0 };
platform_i2c_t platform_i2c1 = { .hw = i2c1 };

uint platform_i2c_init(platform_i2c_t *i2c, uint baud)
{
    return i2c_init(i2c->hw, baud);
}

int platform_i2c_write_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t len,
    bool nostop)
{
    return i2c_write_blocking(i2c->hw, addr, src, len, nostop);
}

int platform_i2c_write_read_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t dst_len)
{
    if (i2c_write_blocking(i2c->hw, addr, src, src_len, true) != (int)src_len)
        return -1;
    return i2c_read_blocking(i2c->hw, addr, dst, dst_len, false);
}
