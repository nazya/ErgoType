// derived from https://github.com/mrjohnk/PMW3360DM-T2QU

#include "pmw3360.h"

#ifdef ERGOTYPE_ZEPHYR
#include "platform/gpio.h"
#include "platform/spi.h"
#include "platform/time.h"
#else
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#endif

#include "srom.h"

#ifdef ERGOTYPE_ZEPHYR
#define asm __asm__
#endif

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
#define Motion_Burst_Size 6
#define Motion_Burst_Delta_X 2
#define Motion_Burst_Delta_Y 4
#define LiftCutoff_Tune_Timeout 0x58
#define LiftCutoff_Tune_Min_Length  0x5A
#define SROM_Load_Burst 0x62
#define Lift_Config 0x63
#define Raw_Data_Burst  0x64
#define LiftCutoff_Tune2  0x65

#ifdef ERGOTYPE_ZEPHYR
#define PMW3360_SPI_WRITE(cfg, data, len) platform_spi_write((uint8_t)(cfg)->spi_idx, data, len)
#define PMW3360_SPI_READ(cfg, data, len) platform_spi_read((uint8_t)(cfg)->spi_idx, 0, data, len)
#define PMW3360_DELAY_US(us) platform_sleep_us(us)
#define PMW3360_DELAY_MS(ms) platform_sleep_ms(ms)

static inline void pmw3360_spi_prepare(const pmw33xx_cfg_t *cfg)
{
    (void)cfg;
}
#else
#define PMW3360_SPI_WRITE(cfg, data, len) spi_write_blocking(pmw3360_spi(cfg), data, len)
#define PMW3360_SPI_READ(cfg, data, len) spi_read_blocking(pmw3360_spi(cfg), 0, data, len)
#define PMW3360_DELAY_US(us) busy_wait_us_32(us)
#define PMW3360_DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))

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
#endif

static void cs_select(const pmw33xx_cfg_t *cfg) {
    asm volatile ("nop \n nop \n nop");
#ifdef ERGOTYPE_ZEPHYR
    platform_gpio_write(cfg->cs, false);
#else
    gpio_put((uint)cfg->cs, 0);        // Active low
#endif
    asm volatile ("nop \n nop \n nop");
}

static void cs_deselect(const pmw33xx_cfg_t *cfg) {
    asm volatile ("nop \n nop \n nop");
#ifdef ERGOTYPE_ZEPHYR
    platform_gpio_write(cfg->cs, true);
#else
    gpio_put((uint)cfg->cs, 1);
#endif
    asm volatile ("nop \n nop \n nop");
}

static uint8_t read_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr) {
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);

    // send adress of the register, with MSBit = 0 to indicate it's a read
    uint8_t x = reg_addr & 0x7f;
    PMW3360_SPI_WRITE(cfg, &x, 1);
    PMW3360_DELAY_US(100);              // tSRAD
    // read data
    uint8_t data;
    PMW3360_SPI_READ(cfg, &data, 1);

    PMW3360_DELAY_US(1);                // tSCLK-NCS for read operation is 120ns
    cs_deselect(cfg);
    PMW3360_DELAY_US(19);               // tSRW/tSRR (=20us) minus tSCLK-NCS

    return data;
}

static void write_register(const pmw33xx_cfg_t *cfg, uint8_t reg_addr, uint8_t data) {
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);

    // send adress of the register, with MSBit = 1 to indicate it's a write
    uint8_t x = reg_addr | 0x80;
    PMW3360_SPI_WRITE(cfg, &x, 1);
    // send data
    PMW3360_SPI_WRITE(cfg, &data, 1);

    PMW3360_DELAY_US(20);               // tSCLK-NCS for write operation
    cs_deselect(cfg);
    PMW3360_DELAY_US(100);              // tSWW/tSWR (=120us) minus tSCLK-NCS. Could be shortened, but is looks like a safe lower bound 
}

static void upload_firmware(const pmw33xx_cfg_t *cfg) {
    // send the firmware to the chip, cf p.18 of the datasheet

    // Write 0 to Rest_En bit of Config2 register to disable Rest mode.
    write_register(cfg, Config2, 0x20);

    // write 0x1d in SROM_enable reg for initializing
    write_register(cfg, SROM_Enable, 0x1d);

    // wait for more than one frame period
    PMW3360_DELAY_MS(10); // assume that the frame rate is as low as 100fps... even if it should never be that low

    // write 0x18 to SROM_enable to start SROM download
    write_register(cfg, SROM_Enable, 0x18);

    // write the SROM file (=firmware data) 
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);
    uint8_t data = SROM_Load_Burst | 0x80; // write burst destination adress
    PMW3360_SPI_WRITE(cfg, &data, 1);
    PMW3360_DELAY_US(15);

    // send all bytes of the firmware
    for (int i = 0; i < firmware_length; i++) {
        PMW3360_SPI_WRITE(cfg, &(firmware_data[i]), 1);
        PMW3360_DELAY_US(15);
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
    PMW3360_DELAY_MS(50); // wait for it to reboot
    // read registers 0x02 to 0x06 (and discard the data)
    read_register(cfg, Motion);
    read_register(cfg, Delta_X_L);
    read_register(cfg, Delta_X_H);
    read_register(cfg, Delta_Y_L);
    read_register(cfg, Delta_Y_H);
    // upload the firmware
    upload_firmware(cfg);
    PMW3360_DELAY_MS(10);
}

void pmw3360_set_cpi(const pmw33xx_cfg_t *cfg) {
    uint8_t cpival = (uint8_t)((cfg->cpi / 100u) - 1u);
    write_register(cfg, Config1, cpival);
    write_register(cfg, Motion_Burst, 0x00);
}

void pmw3360_get_deltas(const pmw33xx_cfg_t *cfg, int16_t *dx, int16_t *dy) {
    pmw3360_spi_prepare(cfg);
    cs_select(cfg);

    uint8_t address = Motion_Burst;
    PMW3360_SPI_WRITE(cfg, &address, 1);
    PMW3360_DELAY_US(35);

    uint8_t data[Motion_Burst_Size];
    PMW3360_SPI_READ(cfg, data, sizeof(data));

    PMW3360_DELAY_US(1);
    cs_deselect(cfg);
    PMW3360_DELAY_US(1);

    *dx = (int16_t)(((uint16_t)data[Motion_Burst_Delta_X + 1] << 8) |
                    data[Motion_Burst_Delta_X]);
    *dy = (int16_t)(((uint16_t)data[Motion_Burst_Delta_Y + 1] << 8) |
                    data[Motion_Burst_Delta_Y]);
}

bool pmw3360_init(const pmw33xx_cfg_t *cfg) {
#ifdef ERGOTYPE_ZEPHYR
    platform_gpio_output(cfg->cs, true);
#else
    gpio_init((uint)cfg->cs);
    gpio_set_dir((uint)cfg->cs, GPIO_OUT);
    gpio_put((uint)cfg->cs, 1);
#endif

    perform_startup(cfg);
    return true;
}
