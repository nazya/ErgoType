// derived from https://github.com/mrjohnk/PMW3389DM

#include "pmw3389.h"

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
#define RawData_Sum  0x08
#define Maximum_RawData  0x09
#define Minimum_RawData  0x0A
#define Shutter_Lower 0x0B
#define Shutter_Upper 0x0C
#define Ripple_Control 0x0D
#define Resolution_L 0x0E
#define Resolution_H 0x0F
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
#define RawData_Threshold  0x2C
#define Control2 0x2D
#define Config5_L 0x2E
#define Config5_H 0x2F
#define Power_Up_Reset  0x3A
#define Shutdown  0x3B
#define Inverse_Product_ID  0x3F
#define LiftCutoff_Cal3  0x41
#define Angle_Snap  0x42
#define LiftCutoff_Cal1  0x4A
#define Motion_Burst  0x50
#define LiftCutoff_Cal_Timeout 0x71
#define LiftCutoff_Cal_Min_Length  0x72
#define PWM_Period_Cnt  0x73
#define PWM_Width_Cnt  0x74
#define SROM_Load_Burst 0x62
#define Lift_Config 0x63
#define RawData_Burst  0x64
#define LiftCutoff_Cal2  0x65

#define PMW3389_PRODUCT_ID_EXPECTED 0x47u

// Timing (PMW3389 datasheet)
#define PMW3389_TSRAD_US 160u
#define PMW3389_TSRW_US 20u
#define PMW3389_TSCLK_NCS_WRITE_US 35u
#define PMW3389_TSWW_TSWR_US 180u

static spi_inst_t *const spi_by_idx[MAX_SPI] = { spi0, spi1 };

static inline spi_inst_t *pmw3389_spi(const pmw33xx_cfg_t *cfg)
{
    return spi_by_idx[(uint8_t)cfg->spi_idx];
}

static inline void pmw3389_spi_prepare(const pmw33xx_cfg_t *cfg)
{
    spi_inst_t *spi = pmw3389_spi(cfg);
    // PMW3389 uses SPI mode 3 (CPOL=1, CPHA=1).
    spi_set_format(spi, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
}

static void cs_select(const pmw33xx_cfg_t *cfg)
{
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)cfg->cs, 0); // active low
    asm volatile ("nop \n nop \n nop");
}

static void cs_deselect(const pmw33xx_cfg_t *cfg)
{
    asm volatile ("nop \n nop \n nop");
    gpio_put((uint)cfg->cs, 1);
    asm volatile ("nop \n nop \n nop");
}

static uint8_t read_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr)
{
    spi_inst_t *spi = pmw3389_spi(cfg);
    pmw3389_spi_prepare(cfg);
    cs_select(cfg);

    uint8_t x = reg_addr & 0x7f; // read
    spi_write_blocking(spi, &x, 1);
    sleep_us(PMW3389_TSRAD_US);

    uint8_t data;
    spi_read_blocking(spi, 0, &data, 1);

    sleep_us(1);
    cs_deselect(cfg);
    sleep_us(PMW3389_TSRW_US - 1u);

    return data;
}

static void write_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr, uint8_t data)
{
    spi_inst_t *spi = pmw3389_spi(cfg);
    pmw3389_spi_prepare(cfg);
    cs_select(cfg);

    uint8_t x = reg_addr | 0x80; // write
    spi_write_blocking(spi, &x, 1);
    spi_write_blocking(spi, &data, 1);

    sleep_us(PMW3389_TSCLK_NCS_WRITE_US);
    cs_deselect(cfg);
    sleep_us(PMW3389_TSWW_TSWR_US);
}

static void upload_firmware(const pmw33xx_cfg_t *cfg)
{
    // Write 0 to Rest_En bit of Config2 register to disable Rest mode.
    write_register(cfg, Config2, 0x20);

    // write 0x1d in SROM_enable reg for initializing
    write_register(cfg, SROM_Enable, 0x1d);

    // wait for more than one frame period
    sleep_ms(10);

    // write 0x18 to SROM_enable to start SROM download
    write_register(cfg, SROM_Enable, 0x18);

    // write the SROM file (=firmware data)
    spi_inst_t *spi = pmw3389_spi(cfg);
    pmw3389_spi_prepare(cfg);
    cs_select(cfg);
    uint8_t data = SROM_Load_Burst | 0x80;
    spi_write_blocking(spi, &data, 1);
    sleep_us(15);

    for (int i = 0; i < (int)pmw3389_firmware_length; i++) {
        spi_write_blocking(spi, &(pmw3389_firmware_data[i]), 1);
        sleep_us(15);
    }

    // Read the SROM_ID register to verify the ID before any other register reads or writes.
    read_register(cfg, SROM_ID);

    // Write 0x00 to Config2 register for wired mouse or 0x20 for wireless mouse design.
    write_register(cfg, Config2, 0x00);

    cs_deselect(cfg);
}

static void perform_startup(const pmw33xx_cfg_t *cfg)
{
    cs_deselect(cfg); // ensure that the serial port is reset
    cs_select(cfg);
    cs_deselect(cfg);
    write_register(cfg, Power_Up_Reset, 0x5a);
    sleep_ms(50);

    read_register(cfg, Motion);
    read_register(cfg, Delta_X_L);
    read_register(cfg, Delta_X_H);
    read_register(cfg, Delta_Y_L);
    read_register(cfg, Delta_Y_H);

    upload_firmware(cfg);
    sleep_ms(10);
}

void pmw3389_set_cpi(const pmw33xx_cfg_t *cfg)
{
    uint16_t cpi = cfg->cpi;
    if (cpi < 50)
        cpi = 50;
    uint16_t cpival = (uint16_t)(cpi / 50u);
    if (cpival == 0)
        cpival = 1;
    cpival -= 1u;

    // Sets upper byte first for more consistent setting of cpi
    write_register(cfg, Resolution_H, (uint8_t)((cpival >> 8) & 0xFFu));
    write_register(cfg, Resolution_L, (uint8_t)(cpival & 0xFFu));
}

void pmw3389_get_deltas(const pmw33xx_cfg_t *cfg, int16_t *dx, int16_t *dy)
{
    write_register(cfg, Motion, 0x01);
    read_register(cfg, Motion);

    uint8_t dx_l = read_register(cfg, Delta_X_L);
    uint8_t dx_h = read_register(cfg, Delta_X_H);
    uint8_t dy_l = read_register(cfg, Delta_Y_L);
    uint8_t dy_h = read_register(cfg, Delta_Y_H);

    *dx = (int16_t)((dx_h << 8) | dx_l);
    *dy = (int16_t)((dy_h << 8) | dy_l);
}

bool pmw3389_init(const pmw33xx_cfg_t *cfg)
{
    gpio_init((uint)cfg->cs);
    gpio_set_dir((uint)cfg->cs, GPIO_OUT);
    gpio_put((uint)cfg->cs, 1);

    perform_startup(cfg);

    uint8_t pid = read_register(cfg, Product_ID);
    return pid == (uint8_t)PMW3389_PRODUCT_ID_EXPECTED;
}
