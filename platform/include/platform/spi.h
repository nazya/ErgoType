#ifndef ERGOTYPE_PLATFORM_SPI_H
#define ERGOTYPE_PLATFORM_SPI_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct platform_spi platform_spi_t;

typedef enum {
    PLATFORM_SPI_CPOL_0 = 0,
    PLATFORM_SPI_CPOL_1 = 1,
} platform_spi_cpol_t;

typedef enum {
    PLATFORM_SPI_CPHA_0 = 0,
    PLATFORM_SPI_CPHA_1 = 1,
} platform_spi_cpha_t;

typedef enum {
    PLATFORM_SPI_MSB_FIRST = 0,
} platform_spi_order_t;

extern platform_spi_t platform_spi0;
extern platform_spi_t platform_spi1;

uint platform_spi_init(platform_spi_t *spi, uint baud);
void platform_spi_set_format(
    platform_spi_t *spi,
    uint data_bits,
    platform_spi_cpol_t cpol,
    platform_spi_cpha_t cpha,
    platform_spi_order_t order);
int platform_spi_write_blocking(platform_spi_t *spi, const uint8_t *src, size_t len);
int platform_spi_read_blocking(
    platform_spi_t *spi,
    uint8_t repeated_tx_data,
    uint8_t *dst,
    size_t len);

#endif
