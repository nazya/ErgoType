// derived from https://github.com/mrjohnk/PMW3360DM-T2QU

#include "pmw3360.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"

#include "srom.h"

// NOTE: We intentionally use `busy_wait_us_32()` (spin) instead of pico-sdk `sleep_us()`.
// `sleep_us()` may use WFE-based sleep; with PMW33xx enabled this caused flaky USB enumeration
// on some setups (device sometimes fails to enumerate unless reset several times).

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

static spi_inst_t *const spi_by_idx[MAX_SPI] = { spi0, spi1 };

static inline spi_inst_t *pmw3360_spi(const pmw33xx_cfg_t *cfg)
{
    return spi_by_idx[(uint8_t)cfg->spi_idx];
}

static inline void pmw3360_spi_prepare(const pmw33xx_cfg_t *cfg)
{
    spi_inst_t *spi = pmw3360_spi(cfg);
    // PMW3360 uses SPI mode 3 (CPOL=1, CPHA=1).
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

static void cs_select(const pmw33xx_cfg_t *cfg) {
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)cfg->cs, 0);        // Active low
    asm volatile ("nop \n nop \n nop");
}

static void cs_deselect(const pmw33xx_cfg_t *cfg) {
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)cfg->cs, 1);
    asm volatile ("nop \n nop \n nop");
}

static uint8_t read_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr) {
    spi_inst_t *spi = pmw3360_spi(cfg);
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);

    // send adress of the register, with MSBit = 0 to indicate it's a read
    uint8_t x = reg_addr & 0x7f;
    spi_write_blocking(spi, &x, 1);
    busy_wait_us_32(100);              // tSRAD
    // read data
    uint8_t data;
    spi_read_blocking(spi, 0, &data, 1);

    busy_wait_us_32(1);                // tSCLK-NCS for read operation is 120ns
    cs_deselect(cfg);
    busy_wait_us_32(19);               // tSRW/tSRR (=20us) minus tSCLK-NCS

    return data;
}

static void write_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr, uint8_t data) {
    spi_inst_t *spi = pmw3360_spi(cfg);
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);

    // send adress of the register, with MSBit = 1 to indicate it's a write
    uint8_t x = reg_addr | 0x80;
    spi_write_blocking(spi, &x, 1);
    // send data
    spi_write_blocking(spi, &data, 1);

    busy_wait_us_32(20);               // tSCLK-NCS for write operation
    cs_deselect(cfg);
    busy_wait_us_32(100);              // tSWW/tSWR (=120us) minus tSCLK-NCS. Could be shortened, but is looks like a safe lower bound 
}

static void upload_firmware(const pmw33xx_cfg_t *cfg) {
    // send the firmware to the chip, cf p.18 of the datasheet

    // Write 0 to Rest_En bit of Config2 register to disable Rest mode.
    write_register(cfg, Config2, 0x20);

    // write 0x1d in SROM_enable reg for initializing
    write_register(cfg, SROM_Enable, 0x1d);

    // wait for more than one frame period
    vTaskDelay(pdMS_TO_TICKS(10)); // assume that the frame rate is as low as 100fps... even if it should never be that low

    // write 0x18 to SROM_enable to start SROM download
    write_register(cfg, SROM_Enable, 0x18);

    // write the SROM file (=firmware data) 
    spi_inst_t *spi = pmw3360_spi(cfg);
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);
    uint8_t data = SROM_Load_Burst | 0x80; // write burst destination adress
    spi_write_blocking(spi, &data, 1);
    busy_wait_us_32(15);

    // send all bytes of the firmware
    for (int i = 0; i < firmware_length; i++) {
        spi_write_blocking(spi, &(firmware_data[i]), 1);
        busy_wait_us_32(15);
    }

    // Read the SROM_ID register to verify the ID before any other register reads or writes.
    read_register(cfg, SROM_ID);

    // Write 0x00 to Config2 register for wired mouse or 0x20 for wireless mouse design.
    write_register(cfg, Config2, 0x00);

    // set initial CPI resolution
    write_register(cfg, Config1, 0x15);

    // write_register(Angle_Tune, 90);
    write_register(cfg, Angle_Snap, 0xC0);

    cs_deselect(cfg);
}

static void perform_startup(const pmw33xx_cfg_t *cfg) {
    cs_deselect(cfg);              // ensure that the serial port is reset
    cs_select(cfg);                // ensure that the serial port is reset
    cs_deselect(cfg);              // ensure that the serial port is reset
    write_register(cfg, Power_Up_Reset, 0x5a);       // force reset
    vTaskDelay(pdMS_TO_TICKS(50)); // wait for it to reboot
    // read registers 0x02 to 0x06 (and discard the data)
    read_register(cfg, Motion);
    read_register(cfg, Delta_X_L);
    read_register(cfg, Delta_X_H);
    read_register(cfg, Delta_Y_L);
    read_register(cfg, Delta_Y_H);
    // upload the firmware
    upload_firmware(cfg);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void pmw3360_set_cpi(const pmw33xx_cfg_t *cfg) {
    uint16_t cpi = cfg->cpi;
    uint8_t cpival = (uint8_t)((cpi / 100u) - 1u);
    write_register(cfg, Config1, cpival);
}

void pmw3360_get_deltas(const pmw33xx_cfg_t *cfg, int16_t *dx, int16_t *dy) {
    // write 0x01 to Motion register and read from it to freeze the motion values and make them available
    write_register(cfg, Motion, 0x01);
    read_register(cfg, Motion);

    uint8_t dx_l = read_register(cfg, Delta_X_L);
    uint8_t dx_h = read_register(cfg, Delta_X_H);
    uint8_t dy_l = read_register(cfg, Delta_Y_L);
    uint8_t dy_h = read_register(cfg, Delta_Y_H);

    *dx = (int16_t)((dx_h << 8) | dx_l);
    *dy = (int16_t)((dy_h << 8) | dy_l);
}

bool pmw3360_init(const pmw33xx_cfg_t *cfg) {
    gpio_init((uint)cfg->cs);
    gpio_set_dir((uint)cfg->cs, GPIO_OUT);
    gpio_put((uint)cfg->cs, 1);

    perform_startup(cfg);
    return true;
}
