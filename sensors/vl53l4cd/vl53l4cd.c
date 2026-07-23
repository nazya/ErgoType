/**
 ******************************************************************************
 * Copyright (c) 2021 STMicroelectronics.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in
 * licenses/LICENSE-BSD-STMicroelectronics.txt are met.
 ******************************************************************************
 */

#include "sensors/vl53l4cd/vl53l4cd.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "platform/gpio.h"
#include "platform/i2c.h"
#include "platform/time.h"

/*
 * Register addresses and command sequences below come from ST's VL53L4CD
 * Ultra Lite Driver (ULD). They are sensor internals, not tuning parameters.
 * References: ST user manual UM2931 and ULD source package STSW-IMG026.
 */
#define REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND 0x0008u /* VHV calibration timeout. */
#define REG_GPIO_HV_MUX_CTRL 0x0030u /* GPIO1 interrupt polarity is stored in bit 4. */
#define REG_GPIO_TIO_HV_STATUS 0x0031u /* GPIO1/data-ready state is mirrored in bit 0. */
#define REG_RANGE_CONFIG_A 0x005Eu /* Encoded timeout for internal ranging phase A. */
#define REG_RANGE_CONFIG_B 0x0061u /* Encoded timeout for internal ranging phase B. */
#define REG_INTERMEASUREMENT_MS 0x006Cu /* PLL-scaled period; zero means continuous mode. */
#define REG_SYSTEM_INTERRUPT_CLEAR 0x0086u /* Write 1 to acknowledge a completed sample. */
#define REG_SYSTEM_START 0x0087u /* Start, mode selection, and immediate-abort commands. */
#define REG_RESULT_RANGE_STATUS 0x0089u /* First register in the packed result block. */
#define REG_RESULT_OSC_CALIBRATE_VAL 0x00DEu /* PLL calibration for period conversion. */
#define REG_FIRMWARE_SYSTEM_STATUS 0x00E5u /* Value 3 means firmware boot completed. */
#define REG_IDENTIFICATION_MODEL_ID 0x010Fu /* Two-byte silicon model identifier. */
#define MODEL_ID 0xEBAAu /* Model identifier returned by a VL53L4CD. */
#define BOOT_TIMEOUT_MS 1000u /* ST ULD limit for boot and first-data waits. */

#if VL53L4CD_I2C_INDEX == 0
static platform_i2c_t *const vl53l4cd_i2c = &platform_i2c0;
#elif VL53L4CD_I2C_INDEX == 1
static platform_i2c_t *const vl53l4cd_i2c = &platform_i2c1;
#else
#error "VL53L4CD_I2C_INDEX must be 0 or 1"
#endif

static bool proximity_pressed;

/*
 * Opaque ST ULD default configuration for registers 0x002D through 0x0087.
 * It establishes the sensor's internal ranging defaults and a new-sample
 * interrupt. Most bytes are explicitly documented by ST as not user
 * modifiable; tune behavior through the constants in vl53l4cd.h instead.
 */
static const uint8_t default_configuration[] = {
    0x12, 0x00, 0x00, 0x11, 0x02, 0x00, 0x02, 0x08,
    0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x0B, 0x00, 0x00, 0x02, 0x14, 0x21,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC8,
    0x00, 0x00, 0x38, 0xFF, 0x01, 0x00, 0x08, 0x00,
    0x00, 0x01, 0xCC, 0x07, 0x01, 0xF1, 0x05, 0x00,
    0xA0, 0x00, 0x80, 0x08, 0x38, 0x00, 0x00, 0x00,
    0x00, 0x0F, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x07, 0x05, 0x06, 0x06, 0x00,
    0x00, 0x02, 0xC7, 0xFF, 0x9B, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00,
};

static bool read_multi(uint16_t reg, uint8_t *data, size_t size)
{
    /* VL53L4CD uses a 16-bit big-endian register address and a repeated-start read. */
    const uint8_t address[] = { (uint8_t)(reg >> 8), (uint8_t)reg };
    return platform_i2c_write_read_blocking(
               vl53l4cd_i2c, VL53L4CD_I2C_ADDRESS,
               address, sizeof(address), data, size) == (int)size;
}

static bool write_multi(uint16_t reg, const uint8_t *data, size_t size)
{
    /* Two address bytes plus at most one 32-bit value are written by this driver. */
    uint8_t tx[6];
    tx[0] = (uint8_t)(reg >> 8);
    tx[1] = (uint8_t)reg;
    memcpy(&tx[2], data, size);
    return platform_i2c_write_blocking(
               vl53l4cd_i2c, VL53L4CD_I2C_ADDRESS,
               tx, size + 2u, false) == (int)(size + 2u);
}

static bool read_byte(uint16_t reg, uint8_t *value)
{
    return read_multi(reg, value, 1u);
}

static bool read_word(uint16_t reg, uint16_t *value)
{
    /* Sensor register values, like register addresses, are transmitted MSB first. */
    uint8_t data[2];
    if (!read_multi(reg, data, sizeof(data)))
        return false;
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return true;
}

static bool read_dword(uint16_t reg, uint32_t *value)
{
    uint8_t data[4];
    if (!read_multi(reg, data, sizeof(data)))
        return false;
    *value = ((uint32_t)data[0] << 24) |
             ((uint32_t)data[1] << 16) |
             ((uint32_t)data[2] << 8) |
             data[3];
    return true;
}

static bool write_byte(uint16_t reg, uint8_t value)
{
    return write_multi(reg, &value, 1u);
}

static bool write_word(uint16_t reg, uint16_t value)
{
    const uint8_t data[] = { (uint8_t)(value >> 8), (uint8_t)value };
    return write_multi(reg, data, sizeof(data));
}

static bool write_dword(uint16_t reg, uint32_t value)
{
    const uint8_t data[] = {
        (uint8_t)(value >> 24), (uint8_t)(value >> 16),
        (uint8_t)(value >> 8), (uint8_t)value,
    };
    return write_multi(reg, data, sizeof(data));
}

static bool check_data_ready(bool *ready)
{
    /* Poll the sensor's GPIO1 mirror, so the physical interrupt pin need not be wired. */
    uint8_t mux;
    uint8_t status;
    if (!read_byte(REG_GPIO_HV_MUX_CTRL, &mux) ||
        !read_byte(REG_GPIO_TIO_HV_STATUS, &status))
        return false;
    const uint8_t polarity = (mux & 0x10u) ? 0u : 1u;
    *ready = (status & 1u) == polarity;
    return true;
}

static bool wait_data_ready(void)
{
    /* Blocking waits are confined to initialization; normal keyscan polling never waits. */
    for (uint16_t i = 0; i < BOOT_TIMEOUT_MS; ++i) {
        bool ready;
        if (!check_data_ready(&ready))
            return false;
        if (ready)
            return true;
        platform_sleep_ms(1u);
    }
    return false;
}

static bool clear_interrupt(void)
{
    /* Acknowledging the current sample lets the ranging engine produce the next one. */
    return write_byte(REG_SYSTEM_INTERRUPT_CLEAR, 0x01u);
}

static bool stop_ranging(void)
{
    /* 0x80 is the newer ULD "abort now" command, not the older deferred 0x00 stop. */
    return write_byte(REG_SYSTEM_START, 0x80u);
}

static bool set_range_timing(void)
{
    /* Register 0x0006 holds the per-device oscillator value used by ST's timeout formula. */
    uint16_t osc_frequency;
    if (!read_word(0x0006u, &osc_frequency) || osc_frequency == 0u)
        return false;

    uint32_t timing_budget_us = VL53L4CD_TIMING_BUDGET_MS * 1000u;
    /* Convert wall-clock microseconds to the sensor's fixed-point macro-period units. */
    const uint32_t macro_period_us =
        (2304u * (0x40000000u / osc_frequency)) >> 6;

    if (VL53L4CD_INTER_MEASUREMENT_MS == 0u) {
        /* Continuous mode spends one 2.5 ms overhead inside each timing budget. */
        if (!write_dword(REG_INTERMEASUREMENT_MS, 0u))
            return false;
        timing_budget_us -= 2500u;
    } else {
        /* Autonomous mode stores the period in PLL ticks and splits its budget in two. */
        uint16_t clock_pll;
        if (!read_word(REG_RESULT_OSC_CALIBRATE_VAL, &clock_pll))
            return false;
        clock_pll &= 0x03FFu;
        const uint32_t inter_measurement =
            (VL53L4CD_INTER_MEASUREMENT_MS * (uint32_t)clock_pll * 1055u + 500u) / 1000u;
        if (!write_dword(REG_INTERMEASUREMENT_MS, inter_measurement))
            return false;
        timing_budget_us = (timing_budget_us - 4300u) / 2u;
    }

    /* ST encodes each phase timeout as an 8-bit exponent and 8-bit mantissa. */
    timing_budget_us <<= 12;
    uint16_t ms_byte = 0;
    uint32_t tmp = macro_period_us * 16u;
    uint32_t ls_byte =
        ((timing_budget_us + ((tmp >> 6) >> 1)) / (tmp >> 6)) - 1u;
    while (ls_byte & 0xFFFFFF00u) {
        ls_byte >>= 1;
        ++ms_byte;
    }
    const uint16_t range_config_a =
        (uint16_t)((ms_byte << 8) | (ls_byte & 0xFFu));

    /* Phase B uses a different internal macro-period multiplier prescribed by the ULD. */
    ms_byte = 0;
    tmp = macro_period_us * 12u;
    ls_byte = ((timing_budget_us + ((tmp >> 6) >> 1)) / (tmp >> 6)) - 1u;
    while (ls_byte & 0xFFFFFF00u) {
        ls_byte >>= 1;
        ++ms_byte;
    }
    const uint16_t range_config_b =
        (uint16_t)((ms_byte << 8) | (ls_byte & 0xFFu));

    return write_word(REG_RANGE_CONFIG_A, range_config_a) &&
           write_word(REG_RANGE_CONFIG_B, range_config_b);
}

static bool sensor_init(void)
{
    /* The embedded firmware reports 0x03 after its power-on boot has completed. */
    for (uint16_t i = 0; i < BOOT_TIMEOUT_MS; ++i) {
        uint8_t status;
        if (!read_byte(REG_FIRMWARE_SYSTEM_STATUS, &status))
            return false;
        if (status == 0x03u)
            break;
        if (i + 1u == BOOT_TIMEOUT_MS)
            return false;
        platform_sleep_ms(1u);
    }

    /* Load ST's contiguous 0x002D..0x0087 configuration exactly as supplied. */
    for (size_t i = 0; i < sizeof(default_configuration); ++i) {
        if (!write_byte((uint16_t)(0x002Du + i), default_configuration[i]))
            return false;
    }

    /*
     * Run the one-shot VHV calibration, consume its result, then abort ranging.
     * The final three writes are ST's post-calibration tuning sequence;
     * registers 0x000B and 0x0024 have no public application-level meaning.
     */
    if (!write_byte(REG_SYSTEM_START, 0x40u) ||
        !wait_data_ready() ||
        !clear_interrupt() ||
        !stop_ranging() ||
        !write_byte(REG_VHV_CONFIG_TIMEOUT_MACROP_LOOP_BOUND, 0x09u) ||
        !write_byte(0x000Bu, 0x00u) ||
        !write_word(0x0024u, 0x0500u))
        return false;

    return set_range_timing();
}

static bool start_ranging(void)
{
    uint32_t inter_measurement;
    if (!read_dword(REG_INTERMEASUREMENT_MS, &inter_measurement))
        return false;
    /* 0x21 selects continuous mode; 0x40 selects autonomous low-power mode. */
    const uint8_t start = inter_measurement == 0u ? 0x21u : 0x40u;
    /* The first ready indication synchronizes the host and is cleared before normal polling. */
    return write_byte(REG_SYSTEM_START, start) &&
           wait_data_ready() &&
           clear_interrupt();
}

static bool get_result(uint16_t *distance_mm, bool *valid)
{
    /* Translate the firmware's internal status code to the public ULD status values. */
    static const uint8_t status_map[24] = {
        255, 255, 255, 5, 2, 4, 1, 7, 3, 0, 255, 255,
        9, 13, 255, 255, 255, 255, 10, 6, 255, 255, 11, 12,
    };
    /* One burst covers range status, SPAD count, and the final distance field. */
    uint8_t data[15];
    if (!read_multi(REG_RESULT_RANGE_STATUS, data, sizeof(data)))
        return false;

    const uint8_t raw_status = data[0] & 0x1Fu;
    const uint8_t range_status = raw_status < sizeof(status_map)
        ? status_map[raw_status]
        : 255u;
    /* A zero enabled-SPAD count cannot represent a usable optical measurement. */
    const uint16_t number_of_spad =
        (uint16_t)(((uint16_t)data[3] << 8) | data[4]) / 256u;
    *distance_mm = (uint16_t)(((uint16_t)data[13] << 8) | data[14]);
    *valid = range_status == 0u && number_of_spad != 0u;
    return true;
}

bool vl53l4cd_init(void)
{
#if VL53L4CD_XSHUT_PIN >= 0
    /* XSHUT is active low: pulse it low to force a known hardware-reset state. */
    platform_gpio_init(VL53L4CD_XSHUT_PIN);
    platform_gpio_set_dir(VL53L4CD_XSHUT_PIN, PLATFORM_GPIO_OUT);
    platform_gpio_put(VL53L4CD_XSHUT_PIN, 0);
    platform_sleep_ms(10u);
    platform_gpio_put(VL53L4CD_XSHUT_PIN, 1);
    platform_sleep_ms(10u);
#endif

    /* Reject another I2C device at 0x29 before sending the vendor init sequence. */
    uint16_t model_id;
    if (!read_word(REG_IDENTIFICATION_MODEL_ID, &model_id) ||
        model_id != MODEL_ID ||
        !sensor_init() ||
        !start_ranging())
        return false;

    proximity_pressed = false;
    return true;
}

bool vl53l4cd_poll(bool *pressed)
{
    /* No ready sample is a normal no-event result; this function does not wait. */
    bool ready;
    if (!check_data_ready(&ready) || !ready)
        return false;
    if (!clear_interrupt())
        return false;

    /* Match the ST ranging example: acknowledge the sample, then read its latched result. */
    bool valid;
    uint16_t distance_mm;
    if (!get_result(&distance_mm, &valid))
        return false;

    /*
     * Use a lower press threshold and a higher release threshold as
     * hysteresis against chatter. An invalid optical sample releases.
     */
    const bool next_pressed = valid &&
        (proximity_pressed
            ? distance_mm < VL53L4CD_RELEASE_DISTANCE_MM
            : distance_mm <= VL53L4CD_PRESS_DISTANCE_MM);
    /* Keyscan needs edges, not another identical event for every 10 ms sample. */
    if (next_pressed == proximity_pressed)
        return false;

    proximity_pressed = next_pressed;
    *pressed = next_pressed;
    return true;
}
