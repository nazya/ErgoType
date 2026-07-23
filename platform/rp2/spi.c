#include "hardware/spi.h"
#include "platform/spi.h"

struct platform_spi {
    spi_inst_t *hw;
};

platform_spi_t platform_spi0 = { .hw = spi0 };
platform_spi_t platform_spi1 = { .hw = spi1 };

uint platform_spi_init(platform_spi_t *spi, uint baud)
{
    return spi_init(spi->hw, baud);
}

void platform_spi_set_format(
    platform_spi_t *spi,
    uint data_bits,
    platform_spi_cpol_t cpol,
    platform_spi_cpha_t cpha,
    platform_spi_order_t order)
{
    spi_set_format(
        spi->hw,
        data_bits,
        (spi_cpol_t)cpol,
        (spi_cpha_t)cpha,
        (spi_order_t)order);
}

int platform_spi_write_blocking(platform_spi_t *spi, const uint8_t *src, size_t len)
{
    return spi_write_blocking(spi->hw, src, len);
}

int platform_spi_read_blocking(
    platform_spi_t *spi,
    uint8_t repeated_tx_data,
    uint8_t *dst,
    size_t len)
{
    return spi_read_blocking(spi->hw, repeated_tx_data, dst, len);
}
