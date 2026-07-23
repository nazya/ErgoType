#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include "platform/gpio.h"
#include "platform/i2c.h"
#include "platform/spi.h"

struct platform_spi {
    spi_host_device_t host;
    spi_device_handle_t dev;
    uint baud;
    uint data_bits;
    uint8_t mode;
};

struct platform_i2c {
    i2c_port_num_t port;
    uint baud;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    int address;
};

platform_spi_t platform_spi0 = { .host = SPI2_HOST, .baud = 1000000, .data_bits = 8 };
platform_spi_t platform_spi1 = { .host = SPI3_HOST, .baud = 1000000, .data_bits = 8 };

platform_i2c_t platform_i2c0 = { .port = I2C_NUM_0, .baud = 100000, .address = -1 };
platform_i2c_t platform_i2c1 = { .port = I2C_NUM_1, .baud = 100000, .address = -1 };

static int spi_pins[3] = { -1, -1, -1 };
static uint8_t spi_pin_count;
static int i2c_pins[2] = { -1, -1 };
static uint8_t i2c_pin_count;
static platform_gpio_irq_callback_t gpio_irq_callback;

void platform_gpio_init(uint gpio)
{
    gpio_reset_pin((gpio_num_t)gpio);
}

void platform_gpio_set_dir(uint gpio, bool out)
{
    (void)gpio_set_direction((gpio_num_t)gpio, out ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
}

void platform_gpio_pull_up(uint gpio)
{
    (void)gpio_pullup_en((gpio_num_t)gpio);
    (void)gpio_pulldown_dis((gpio_num_t)gpio);
}

void platform_gpio_pull_down(uint gpio)
{
    (void)gpio_pulldown_en((gpio_num_t)gpio);
    (void)gpio_pullup_dis((gpio_num_t)gpio);
}

void platform_gpio_disable_pulls(uint gpio)
{
    (void)gpio_pullup_dis((gpio_num_t)gpio);
    (void)gpio_pulldown_dis((gpio_num_t)gpio);
}

void platform_gpio_set_function(uint gpio, int fn)
{
    if (fn == PLATFORM_GPIO_FUNC_SPI) {
        spi_pins[spi_pin_count++] = (int)gpio;
    } else if (fn == PLATFORM_GPIO_FUNC_I2C) {
        i2c_pins[i2c_pin_count++] = (int)gpio;
    }
}

static void gpio_isr_trampoline(void *arg)
{
    const uint gpio = (uint)(uintptr_t)arg;
    if (gpio_irq_callback)
        gpio_irq_callback(gpio, PLATFORM_GPIO_IRQ_EDGE_FALL);
}

void platform_gpio_set_irq_enabled(uint gpio, uint32_t events, bool enabled)
{
    (void)events;
    if (enabled) {
        (void)gpio_set_intr_type((gpio_num_t)gpio, GPIO_INTR_NEGEDGE);
        (void)gpio_install_isr_service(0);
        (void)gpio_isr_handler_add((gpio_num_t)gpio, gpio_isr_trampoline, (void *)(uintptr_t)gpio);
    } else {
        (void)gpio_intr_disable((gpio_num_t)gpio);
        (void)gpio_isr_handler_remove((gpio_num_t)gpio);
    }
}

void platform_gpio_set_irq_enabled_with_callback(
    uint gpio,
    uint32_t events,
    bool enabled,
    platform_gpio_irq_callback_t callback)
{
    gpio_irq_callback = callback;
    platform_gpio_set_irq_enabled(gpio, events, enabled);
}

uint platform_spi_init(platform_spi_t *spi, uint baud)
{
    spi->baud = baud;
    spi_bus_config_t buscfg = {
        .mosi_io_num = spi_pins[1],
        .miso_io_num = spi_pins[2],
        .sclk_io_num = spi_pins[0],
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    (void)spi_bus_initialize(spi->host, &buscfg, SPI_DMA_CH_AUTO);
    spi_pin_count = 0;
    return baud;
}

void platform_spi_set_format(
    platform_spi_t *spi,
    uint data_bits,
    platform_spi_cpol_t cpol,
    platform_spi_cpha_t cpha,
    platform_spi_order_t order)
{
    (void)order;
    const uint8_t mode = (uint8_t)(
        ((cpol == PLATFORM_SPI_CPOL_1) ? 2 : 0) |
        ((cpha == PLATFORM_SPI_CPHA_1) ? 1 : 0));
    if (spi->dev && spi->data_bits == data_bits && spi->mode == mode)
        return;
    if (spi->dev) {
        (void)spi_bus_remove_device(spi->dev);
        spi->dev = NULL;
    }
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)spi->baud,
        .mode = mode,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    if (spi_bus_add_device(spi->host, &devcfg, &spi->dev) == ESP_OK) {
        spi->data_bits = data_bits;
        spi->mode = mode;
    }
}

int platform_spi_write_blocking(platform_spi_t *spi, const uint8_t *src, size_t len)
{
    if (!spi->dev)
        return -1;
    spi_transaction_t trans = {0};
    trans.length = len * 8u;
    trans.tx_buffer = src;
    return spi_device_polling_transmit(spi->dev, &trans) == ESP_OK ? (int)len : -1;
}

int platform_spi_read_blocking(
    platform_spi_t *spi,
    uint8_t repeated_tx_data,
    uint8_t *dst,
    size_t len)
{
    if (!spi->dev)
        return -1;
    uint8_t tx[len];
    memset(tx, repeated_tx_data, sizeof(tx));
    spi_transaction_t trans = {0};
    trans.length = len * 8u;
    trans.rxlength = len * 8u;
    trans.tx_buffer = tx;
    trans.rx_buffer = dst;
    return spi_device_polling_transmit(spi->dev, &trans) == ESP_OK ? (int)len : -1;
}

uint platform_i2c_init(platform_i2c_t *i2c, uint baud)
{
    i2c->baud = baud;
    i2c_master_bus_config_t conf = {
        .i2c_port = i2c->port,
        .sda_io_num = i2c_pins[0],
        .scl_io_num = i2c_pins[1],
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    (void)i2c_new_master_bus(&conf, &i2c->bus);
    i2c_pin_count = 0;
    return baud;
}

static i2c_master_dev_handle_t platform_i2c_device(platform_i2c_t *i2c, uint8_t addr)
{
    if (!i2c->bus)
        return NULL;

    if (i2c->address != addr) {
        if (i2c->device) {
            (void)i2c_master_bus_rm_device(i2c->device);
            i2c->device = NULL;
        }
        i2c_device_config_t conf = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addr,
            .scl_speed_hz = i2c->baud,
        };
        if (i2c_master_bus_add_device(i2c->bus, &conf, &i2c->device) != ESP_OK)
            return NULL;
        i2c->address = addr;
    }

    return i2c->device;
}

int platform_i2c_write_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t len,
    bool nostop)
{
    (void)nostop;
    i2c_master_dev_handle_t device = platform_i2c_device(i2c, addr);
    if (!device)
        return -1;
    return i2c_master_transmit(device, src, len, 100) == ESP_OK ? (int)len : -1;
}

int platform_i2c_write_read_blocking(
    platform_i2c_t *i2c,
    uint8_t addr,
    const uint8_t *src,
    size_t src_len,
    uint8_t *dst,
    size_t dst_len)
{
    i2c_master_dev_handle_t device = platform_i2c_device(i2c, addr);
    if (!device)
        return -1;
    return i2c_master_transmit_receive(
               device, src, src_len, dst, dst_len, 100) == ESP_OK
        ? (int)dst_len
        : -1;
}
