#include "platform/spi.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <hal/nrf_gpio.h>
#include <nrfx_spim.h>

#include "jconfig.h"

static nrfx_spim_t spim[MAX_SPI] = {
    NRFX_SPIM_INSTANCE(NRF_SPIM2),
    NRFX_SPIM_INSTANCE(NRF_SPIM3),
};

static bool spim_initialized[MAX_SPI];

static uint32_t nrf_pin(int pin)
{
    if (pin < 32)
        return (uint32_t)pin;

    return NRF_GPIO_PIN_MAP(1, (uint32_t)(pin - 32));
}

static nrf_spim_frequency_t spi_frequency(uint32_t frequency)
{
    if (frequency >= 8000000u)
        return NRF_SPIM_FREQ_8M;
    if (frequency >= 4000000u)
        return NRF_SPIM_FREQ_4M;
    if (frequency >= 2000000u)
        return NRF_SPIM_FREQ_2M;
    if (frequency >= 1000000u)
        return NRF_SPIM_FREQ_1M;
    if (frequency >= 500000u)
        return NRF_SPIM_FREQ_500K;
    if (frequency >= 250000u)
        return NRF_SPIM_FREQ_250K;

    return NRF_SPIM_FREQ_125K;
}

int platform_spi_init(uint8_t bus, int sck, int mosi, int miso, uint32_t frequency)
{
    if (bus >= MAX_SPI)
        return -EINVAL;

    if (spim_initialized[bus]) {
        nrfx_spim_uninit(&spim[bus]);
        spim_initialized[bus] = false;
    }

    nrfx_spim_config_t config = NRFX_SPIM_DEFAULT_CONFIG(
        nrf_pin(sck),
        nrf_pin(mosi),
        nrf_pin(miso),
        NRF_SPIM_PIN_NOT_CONNECTED
    );
    config.frequency = spi_frequency(frequency);
    config.mode = NRF_SPIM_MODE_3;
    config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;
    config.orc = 0;

    int rc = nrfx_spim_init(&spim[bus], &config, NULL, NULL);
    if (!rc)
        spim_initialized[bus] = true;

    return rc;
}

int platform_spi_write(uint8_t bus, const uint8_t *data, size_t len)
{
    if (bus >= MAX_SPI)
        return -EINVAL;

    uint8_t tx_data[len];
    memcpy(tx_data, data, len);

    nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_TX(tx_data, len);
    return nrfx_spim_xfer(&spim[bus], &xfer, 0);
}

int platform_spi_read(uint8_t bus, uint8_t tx_fill, uint8_t *data, size_t len)
{
    (void)tx_fill;

    if (bus >= MAX_SPI)
        return -EINVAL;

    nrfx_spim_xfer_desc_t xfer = NRFX_SPIM_XFER_RX(data, len);
    return nrfx_spim_xfer(&spim[bus], &xfer, 0);
}
