#ifndef ERGOTYPE_PLATFORM_SPI_H
#define ERGOTYPE_PLATFORM_SPI_H

#include <stddef.h>
#include <stdint.h>

int platform_spi_init(uint8_t bus, int sck, int mosi, int miso, uint32_t frequency);
int platform_spi_write(uint8_t bus, const uint8_t *data, size_t len);
int platform_spi_read(uint8_t bus, uint8_t tx_fill, uint8_t *data, size_t len);

#endif
