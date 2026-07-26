#include "platform/i2c.h"

#include <errno.h>
#include <stdbool.h>

#include <hal/nrf_gpio.h>
#include <nrfx_twim.h>

#include "jconfig.h"

static nrfx_twim_t twim[MAX_I2C] = {
    NRFX_TWIM_INSTANCE(NRF_TWIM0),
    NRFX_TWIM_INSTANCE(NRF_TWIM1),
};

static bool twim_initialized[MAX_I2C];

static uint32_t nrf_pin(int pin)
{
    if (pin < 32)
        return (uint32_t)pin;

    return NRF_GPIO_PIN_MAP(1, (uint32_t)(pin - 32));
}

static nrf_twim_frequency_t i2c_frequency(uint32_t frequency)
{
    if (frequency >= 400000u)
        return NRF_TWIM_FREQ_400K;
    if (frequency >= 250000u)
        return NRF_TWIM_FREQ_250K;

    return NRF_TWIM_FREQ_100K;
}

int platform_i2c_init(uint8_t bus, int sda, int scl, uint32_t frequency)
{
    if (bus >= MAX_I2C)
        return -EINVAL;

    if (twim_initialized[bus]) {
        nrfx_twim_uninit(&twim[bus]);
        twim_initialized[bus] = false;
    }

    nrfx_twim_config_t config = NRFX_TWIM_DEFAULT_CONFIG(nrf_pin(scl), nrf_pin(sda));
    config.frequency = i2c_frequency(frequency);

    int rc = nrfx_twim_init(&twim[bus], &config, NULL, NULL);
    if (!rc)
        twim_initialized[bus] = true;

    return rc;
}

int platform_i2c_write(uint8_t bus, uint8_t addr, const uint8_t *data, size_t len)
{
    if (bus >= MAX_I2C)
        return -EINVAL;

    nrfx_twim_xfer_desc_t xfer = NRFX_TWIM_XFER_DESC_TX(addr, (uint8_t *)data, len);
    return nrfx_twim_xfer(&twim[bus], &xfer, 0);
}
