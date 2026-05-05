// derived from https://github.com/mrjohnk/PMW3360DM-T2QU

#include "pmw3360.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "srom.h"

// Registers
#define Product_ID  0x00
#define Revision_ID 0x01
#define Motion  0x02
#define Delta_X_L 0x03
#define Delta_X_H 0x04
#define Delta_Y_L 0x05
#define Delta_Y_H 0x06
#define SQUAL 0x07
#define Raw_Data_Sum  0x08
#define Maximum_Raw_data  0x09
#define Minimum_Raw_data  0x0A
#define Shutter_Lower 0x0B
#define Shutter_Upper 0x0C
#define Control 0x0D
#define Config1 0x0F
#define Config2 0x10
#define Angle_Tune  0x11
#define Frame_Capture 0x12
#define SROM_Enable 0x13
#define Run_Downshift 0x14
#define Rest1_Rate_Lower  0x15
#define Rest1_Rate_Upper  0x16
#define Rest1_Downshift 0x17
#define Rest2_Rate_Lower  0x18
#define Rest2_Rate_Upper  0x19
#define Rest2_Downshift 0x1A
#define Rest3_Rate_Lower  0x1B
#define Rest3_Rate_Upper  0x1C
#define Observation 0x24
#define Data_Out_Lower  0x25
#define Data_Out_Upper  0x26
#define Raw_Data_Dump 0x29
#define SROM_ID 0x2A
#define Min_SQ_Run  0x2B
#define Raw_Data_Threshold  0x2C
#define Config5 0x2F
#define Power_Up_Reset  0x3A
#define Shutdown  0x3B
#define Inverse_Product_ID  0x3F
#define LiftCutoff_Tune3  0x41
#define Angle_Snap  0x42
#define LiftCutoff_Tune1  0x4A
#define Motion_Burst  0x50
#define LiftCutoff_Tune_Timeout 0x58
#define LiftCutoff_Tune_Min_Length  0x5A
#define SROM_Load_Burst 0x62
#define Lift_Config 0x63
#define Raw_Data_Burst  0x64
#define LiftCutoff_Tune2  0x65

static inline void pmw3360_spi_prepare(const pmw3360_t *dev)
{
    spi_set_baudrate(dev->spi, (uint)dev->baud);
    spi_cpol_t cpol = (dev->mode & 2u) ? SPI_CPOL_1 : SPI_CPOL_0;
    spi_cpha_t cpha = (dev->mode & 1u) ? SPI_CPHA_1 : SPI_CPHA_0;
    spi_set_format(dev->spi, 8, cpol, cpha, SPI_MSB_FIRST);
}

static void cs_select(const pmw3360_t *dev) {
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)dev->cs, 0);        // Active low
    asm volatile ("nop \n nop \n nop");
}

static void cs_deselect(const pmw3360_t *dev) {
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)dev->cs, 1);
    asm volatile ("nop \n nop \n nop");
}

static uint8_t read_register(pmw3360_t *dev, uint8_t reg_addr) {
    pmw3360_spi_prepare(dev);
    cs_select(dev);

    // send adress of the register, with MSBit = 0 to indicate it's a read
    uint8_t x = reg_addr & 0x7f;
    spi_write_blocking(dev->spi, &x, 1);
    sleep_us(100);              // tSRAD
    // read data
    uint8_t data;
    spi_read_blocking(dev->spi, 0, &data, 1);

    sleep_us(1);                // tSCLK-NCS for read operation is 120ns
    cs_deselect(dev);
    sleep_us(19);               // tSRW/tSRR (=20us) minus tSCLK-NCS

    return data;
}

static void write_register(pmw3360_t *dev, uint8_t reg_addr, uint8_t data) {
    pmw3360_spi_prepare(dev);
    cs_select(dev);

    // send adress of the register, with MSBit = 1 to indicate it's a write
    uint8_t x = reg_addr | 0x80;
    spi_write_blocking(dev->spi, &x, 1);
    // send data
    spi_write_blocking(dev->spi, &data, 1);

    sleep_us(20);               // tSCLK-NCS for write operation
    cs_deselect(dev);
    sleep_us(100);              // tSWW/tSWR (=120us) minus tSCLK-NCS. Could be shortened, but is looks like a safe lower bound 
}

static void upload_firmware(pmw3360_t *dev) {
    // send the firmware to the chip, cf p.18 of the datasheet

    // Write 0 to Rest_En bit of Config2 register to disable Rest mode.
    write_register(dev, Config2, 0x20);

    // write 0x1d in SROM_enable reg for initializing
    write_register(dev, SROM_Enable, 0x1d);

    // wait for more than one frame period
    sleep_ms(10);               // assume that the frame rate is as low as 100fps... even if it should never be that low

    // write 0x18 to SROM_enable to start SROM download
    write_register(dev, SROM_Enable, 0x18);

    // write the SROM file (=firmware data) 
    pmw3360_spi_prepare(dev);
    cs_select(dev);
    uint8_t data = SROM_Load_Burst | 0x80; // write burst destination adress
    spi_write_blocking(dev->spi, &data, 1);
    sleep_us(15);

    // send all bytes of the firmware
    for (int i = 0; i < firmware_length; i++) {
        spi_write_blocking(dev->spi, &(firmware_data[i]), 1);
        sleep_us(15);
    }

    // Read the SROM_ID register to verify the ID before any other register reads or writes.
    read_register(dev, SROM_ID);

    // Write 0x00 to Config2 register for wired mouse or 0x20 for wireless mouse design.
    write_register(dev, Config2, 0x00);

    // set initial CPI resolution
    write_register(dev, Config1, 0x15);

    // write_register(Angle_Tune, 90);
    write_register(dev, Angle_Snap, 0xC0);

    cs_deselect(dev);
}

static void perform_startup(pmw3360_t *dev) {
    cs_deselect(dev);              // ensure that the serial port is reset
    cs_select(dev);                // ensure that the serial port is reset
    cs_deselect(dev);              // ensure that the serial port is reset
    write_register(dev, Power_Up_Reset, 0x5a);       // force reset
    sleep_ms(50);               // wait for it to reboot
    // read registers 0x02 to 0x06 (and discard the data)
    read_register(dev, Motion);
    read_register(dev, Delta_X_L);
    read_register(dev, Delta_X_H);
    read_register(dev, Delta_Y_L);
    read_register(dev, Delta_Y_H);
    // upload the firmware
    upload_firmware(dev);
    sleep_ms(10);
}

void pmw3360_set_cpi(pmw3360_t *dev, uint16_t cpi) {
    if (!dev)
        return;
    uint8_t cpival = (uint8_t)((cpi / 100u) - 1u);
    write_register(dev, Config1, cpival);
}

void pmw3360_get_deltas(pmw3360_t *dev, int16_t *dx, int16_t *dy) {
    if (!dev)
        return;
    // write 0x01 to Motion register and read from it to freeze the motion values and make them available
    write_register(dev, Motion, 0x01);
    read_register(dev, Motion);

    uint8_t dx_l = read_register(dev, Delta_X_L);
    uint8_t dx_h = read_register(dev, Delta_X_H);
    uint8_t dy_l = read_register(dev, Delta_Y_L);
    uint8_t dy_h = read_register(dev, Delta_Y_H);

    *dx = (int16_t)((dx_h << 8) | dx_l);
    *dy = (int16_t)((dy_h << 8) | dy_l);
}

bool pmw3360_init(pmw3360_t *dev) {
    if (!dev || !dev->spi || dev->cs < 0)
        return false;

    gpio_init((uint)dev->cs);
    gpio_set_dir((uint)dev->cs, GPIO_OUT);
    gpio_put((uint)dev->cs, 1);

    perform_startup(dev);
    return true;
}
