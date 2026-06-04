/******************************************************************************
 * ADS1220_WE Library - Ported to Zephyr RTOS
 *
 * Original Arduino library by Wolfgang (Wolle) Ewald
 * Ported to Zephyr RTOS for ESP32-S3
 *
 * This is a library for the 24 bit, 4 channel ADS1220 A/D Converter
 ******************************************************************************/

#ifndef ADS1220_WE_H_
#define ADS1220_WE_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

/* ADS1220 MUX Configuration */
typedef enum {
  ADS1220_MUX_0_1 = 0x00, // default
  ADS1220_MUX_0_2 = 0x10,
  ADS1220_MUX_0_3 = 0x20,
  ADS1220_MUX_1_2 = 0x30,
  ADS1220_MUX_1_3 = 0x40,
  ADS1220_MUX_2_3 = 0x50,
  ADS1220_MUX_1_0 = 0x60,
  ADS1220_MUX_3_2 = 0x70,
  ADS1220_MUX_0_AVSS = 0x80,
  ADS1220_MUX_1_AVSS = 0x90,
  ADS1220_MUX_2_AVSS = 0xA0,
  ADS1220_MUX_3_AVSS = 0xB0,
  ADS1220_MUX_REFPX_REFNX_4 = 0xC0,
  ADS1220_MUX_AVDD_M_AVSS_4 = 0xD0,
  ADS1220_MUX_AVDD_P_AVSS_2 = 0xE0
} ads1220_mux_t;

typedef enum {
  ADS1220_GAIN_1 = 0x00, // default
  ADS1220_GAIN_2 = 0x02,
  ADS1220_GAIN_4 = 0x04,
  ADS1220_GAIN_8 = 0x06,
  ADS1220_GAIN_16 = 0x08,
  ADS1220_GAIN_32 = 0x0A,
  ADS1220_GAIN_64 = 0x0C,
  ADS1220_GAIN_128 = 0x0E
} ads1220_gain_t;

typedef enum {
  ADS1220_DR_LVL_0 =
      0x00, // default: 20 SPS (normal), 5 SPS (duty), 40 SPS (turbo)
  ADS1220_DR_LVL_1 = 0x20, // 45 SPS (normal), 11.25 SPS (duty), 90 SPS (turbo)
  ADS1220_DR_LVL_2 = 0x40, // 90 SPS (normal), 22.5 SPS (duty), 180 SPS (turbo)
  ADS1220_DR_LVL_3 = 0x60, // 175 SPS (normal), 44 SPS (duty), 350 SPS (turbo)
  ADS1220_DR_LVL_4 = 0x80, // 330 SPS (normal), 82.5 SPS (duty), 660 SPS (turbo)
  ADS1220_DR_LVL_5 = 0xA0, // 600 SPS (normal), 150 SPS (duty), 1200 SPS (turbo)
  ADS1220_DR_LVL_6 = 0xC0 // 1000 SPS (normal), 250 SPS (duty), 2000 SPS (turbo)
} ads1220_data_rate_t;

typedef enum {
  ADS1220_NORMAL_MODE = 0x00, // default
  ADS1220_DUTY_CYCLE_MODE = 0x08,
  ADS1220_TURBO_MODE = 0x10
} ads1220_op_mode_t;

typedef enum {
  ADS1220_SINGLE_SHOT = 0x00, // default
  ADS1220_CONTINUOUS = 0x04
} ads1220_conv_mode_t;

typedef enum {
  ADS1220_VREF_INT = 0x00,         // default - Internal 2.048V
  ADS1220_VREF_REFP0_REFN0 = 0x40, // External reference REFP0/REFN0
  ADS1220_VREF_REFP1_REFN1 = 0x80, // External reference REFP1/REFN1 (AIN0/AIN3)
  ADS1220_VREF_AVDD_AVSS = 0xC0    // Supply voltage AVDD/AVSS
} ads1220_vref_t;

typedef enum {
  ADS1220_NONE = 0x00,      // default - No 50Hz/60Hz rejection
  ADS1220_50HZ_60HZ = 0x10, // Both 50Hz and 60Hz rejection
  ADS1220_50HZ = 0x20,      // 50Hz rejection only
  ADS1220_60HZ = 0x30       // 60Hz rejection only
} ads1220_fir_t;

typedef enum {
  ADS1220_ALWAYS_OPEN = 0x00, // default - Switch always open
  ADS1220_SWITCH = 0x08       // Switch control
} ads1220_psw_t;

typedef enum {
  ADS1220_IDAC_OFF = 0x00, // default
  ADS1220_IDAC_10_MU_A = 0x01,
  ADS1220_IDAC_50_MU_A = 0x02,
  ADS1220_IDAC_100_MU_A = 0x03,
  ADS1220_IDAC_250_MU_A = 0x04,
  ADS1220_IDAC_500_MU_A = 0x05,
  ADS1220_IDAC_1000_MU_A = 0x06,
  ADS1220_IDAC_1500_MU_A = 0x07
} ads1220_idac_current_t;

typedef enum {
  ADS1220_IDAC_NONE = 0x00, // default
  ADS1220_IDAC_AIN0_REFP1 = 0x01,
  ADS1220_IDAC_AIN1 = 0x02,
  ADS1220_IDAC_AIN2 = 0x03,
  ADS1220_IDAC_AIN3_REFN1 = 0x04,
  ADS1220_IDAC_REFP0 = 0x05,
  ADS1220_IDAC_REFN0 = 0x06,
} ads1220_idac_routing_t;

typedef enum {
  ADS1220_DRDY = 0x00,     // default - Use DRDY pin
  ADS1220_DOUT_DRDY = 0x02 // Use DOUT/DRDY pin
} ads1220_drdy_mode_t;

/* ADS1220 Device Structure */
typedef struct {
  const struct spi_dt_spec *spi;
  int drdy_pin;
  float vref;
  uint8_t gain;
  bool ref_measurement;
  bool do_not_bypass_pga_if_possible;
  bool non_blocking_mode;
  ads1220_conv_mode_t conv_mode;
} ads1220_dev_t;

/* ADS1220 Commands */
#define ADS1220_RESET 0x06
#define ADS1220_START 0x08 // START/SYNC command for continuous conversion
#define ADS1220_PWRDOWN 0x02
#define ADS1220_RDATA 0x10
#define ADS1220_WREG 0x40 // Write register
#define ADS1220_RREG 0x20 // Read register

/* ADS1220 Registers */
#define ADS1220_CONF_REG_0 0x00
#define ADS1220_CONF_REG_1 0x01
#define ADS1220_CONF_REG_2 0x02
#define ADS1220_CONF_REG_3 0x03

/* ADS1220 Constants */
#define ADS1220_RANGE 8388607.0 // 2^23 - 1 as float

/* Device Configuration */
#define SPI_ADS1220_NODE DT_NODELABEL(spi2)
#define ADS1220_DEVICE_NODE DT_NODELABEL(ads1220_adc)
#define ADS1220_CS_GPIO_NODE DT_NODELABEL(gpio0)
#define ADS1220_CS_PIN 10
#define ADS1220_DRDY_PIN -1 // Optional DRDY pin

/* Function Declarations */

/* Initialization and Control */
int ads1220_init(ads1220_dev_t *dev);
void ads1220_start(ads1220_dev_t *dev);
void ads1220_reset(ads1220_dev_t *dev);
void ads1220_power_down(ads1220_dev_t *dev);

/* Configuration Register 0 settings */
void ads1220_set_compare_channels(ads1220_dev_t *dev, ads1220_mux_t mux);
void ads1220_set_gain(ads1220_dev_t *dev, ads1220_gain_t gain);
uint8_t ads1220_get_gain_factor(ads1220_dev_t *dev);
void ads1220_bypass_pga(ads1220_dev_t *dev, bool bypass);
bool ads1220_is_pga_bypassed(ads1220_dev_t *dev);

/* Configuration Register 1 settings */
void ads1220_set_data_rate(ads1220_dev_t *dev, ads1220_data_rate_t rate);
void ads1220_set_operating_mode(ads1220_dev_t *dev, ads1220_op_mode_t mode);
void ads1220_set_conversion_mode(ads1220_dev_t *dev, ads1220_conv_mode_t mode);
void ads1220_enable_temperature_sensor(ads1220_dev_t *dev, bool enable);
void ads1220_enable_burnout_current_sources(ads1220_dev_t *dev, bool enable);

/* Configuration Register 2 settings */
void ads1220_set_vref_source(ads1220_dev_t *dev, ads1220_vref_t vref_source);
void ads1220_set_fir_filter(ads1220_dev_t *dev, ads1220_fir_t fir);
void ads1220_set_low_side_power_switch(ads1220_dev_t *dev, ads1220_psw_t psw);
void ads1220_set_idac_current(ads1220_dev_t *dev,
                              ads1220_idac_current_t current);

/* Configuration Register 3 settings */
void ads1220_set_idac1_routing(ads1220_dev_t *dev,
                               ads1220_idac_routing_t route);
void ads1220_set_idac2_routing(ads1220_dev_t *dev,
                               ads1220_idac_routing_t route);
void ads1220_set_drdy_mode(ads1220_dev_t *dev, ads1220_drdy_mode_t mode);

/* Other settings */
void ads1220_set_spi_clock_speed(ads1220_dev_t *dev, uint32_t clock);
void ads1220_set_vref_value_v(ads1220_dev_t *dev, float ref_val);
float ads1220_get_vref_v(ads1220_dev_t *dev);
void ads1220_set_avdd_avss_as_vref_and_calibrate(ads1220_dev_t *dev);
void ads1220_set_refp0_refn0_as_vref_and_calibrate(ads1220_dev_t *dev);
void ads1220_set_refp1_refn1_as_vref_and_calibrate(ads1220_dev_t *dev);
void ads1220_set_int_vref(ads1220_dev_t *dev);
void ads1220_set_non_blocking_mode(ads1220_dev_t *dev, bool non_blocking);
bool ads1220_get_non_blocking_mode(ads1220_dev_t *dev);

/* Results */
float ads1220_get_voltage_mv(ads1220_dev_t *dev);
float ads1220_get_voltage_muv(ads1220_dev_t *dev);
int32_t ads1220_get_raw_data(ads1220_dev_t *dev);
float ads1220_get_temperature(ads1220_dev_t *dev);

/* Low-level functions */
int32_t ads1220_get_data(ads1220_dev_t *dev);
uint32_t ads1220_read_result(ads1220_dev_t *dev);
uint8_t ads1220_read_register(ads1220_dev_t *dev, uint8_t reg);
void ads1220_write_register(ads1220_dev_t *dev, uint8_t reg, uint8_t val);
void ads1220_command(ads1220_dev_t *dev, uint8_t cmd);
void ads1220_set_cs_pin(ads1220_dev_t *dev, uint8_t state);

#endif /* ADS1220_WE_H_ */
