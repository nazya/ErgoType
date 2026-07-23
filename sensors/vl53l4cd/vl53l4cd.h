#ifndef ERGOTYPE_SENSORS_VL53L4CD_H
#define ERGOTYPE_SENSORS_VL53L4CD_H

#include <stdbool.h>

/*
 * Build-time sensor setup. The I2C pins and baud rate still come from the
 * i2c0/i2c1 object selected below in config.json; these constants describe
 * the sensor attached to that bus. SDA and SCL require pull-ups, which are
 * already present on most breakout modules.
 */
/* Platform I2C controller: 0 selects i2c0, 1 selects i2c1. */
#define VL53L4CD_I2C_INDEX 1u
/* 7-bit address expected by platform_i2c; ST's 8-bit address notation is 0x52. */
#define VL53L4CD_I2C_ADDRESS 0x29u
/* Active-low hardware shutdown GPIO, or -1 when the module keeps XSHUT high itself. */
#define VL53L4CD_XSHUT_PIN (-1)
/*
 * Laser-on time per measurement. ST permits 10..200 ms: 10 ms gives the
 * lowest latency (up to 100 Hz); a larger value improves accuracy and range.
 */
#define VL53L4CD_TIMING_BUDGET_MS 10u
/*
 * Time between measurements. Zero selects continuous ranging. A nonzero
 * value selects lower-power autonomous ranging and must exceed the timing
 * budget (maximum documented value: 5000 ms).
 */
#define VL53L4CD_INTER_MEASUREMENT_MS 0u
/*
 * Tune both distance thresholds on the assembled device. Cover glass and
 * mounting can require ST offset/crosstalk calibration, which is not included
 * in this minimal basic-ranging driver.
 */
/* Enter the pressed state at or below 20 mm, chosen for a finger about 1-2 cm away. */
#define VL53L4CD_PRESS_DISTANCE_MM 20u
/* Release at or above 30 mm; the 10 mm gap is hysteresis against threshold chatter. */
#define VL53L4CD_RELEASE_DISTANCE_MM 30u

/* Boots, configures, and starts continuous/autonomous ranging. */
bool vl53l4cd_init(void);
/* Returns true only when a new proximity press or release was written to pressed. */
bool vl53l4cd_poll(bool *pressed);

#endif
