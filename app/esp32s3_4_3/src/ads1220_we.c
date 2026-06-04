/*****************************************
 * ADS1220_WE Library - Ported to Zephyr RTOS
 *
 * Original Arduino library by Wolfgang (Wolle) Ewald
 * Ported to Zephyr RTOS for ESP32-S3
 *
 * This is a library for the 24 bit, 4 channel ADS1220 A/D Converter
 *******************************************/

#include "ads1220_we.h"
#include "zephyr/drivers/spi.h"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(ads1220_we, CONFIG_LOG_DEFAULT_LEVEL);

/* DT 노드 라벨은 dts와 맞춰줘 */
#define ADS1220_NODE DT_NODELABEL(ads1220_adc)

/* Define SPI device tree specification for ADS1220 */
static const struct spi_dt_spec ads1220_spi_spec = SPI_DT_SPEC_GET(
    ADS1220_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);

/* Initialize ADS1220 device */
int ads1220_init(ads1220_dev_t *dev) {
  if (!dev) {
    return -EINVAL;
  }

  // Initialize default values
  dev->vref = 2.048f;
  dev->gain = 1;
  dev->ref_measurement = false;
  dev->conv_mode = ADS1220_SINGLE_SHOT;
  dev->non_blocking_mode = false;
  dev->do_not_bypass_pga_if_possible = false;

  // Initialize SPI device tree specification (modern Zephyr 4.2 way)
  dev->spi = &ads1220_spi_spec;

  if (!device_is_ready(dev->spi->bus)) {
    LOG_ERR("SPI bus not ready");
    return -ENODEV;
  }
  if (!device_is_ready(dev->spi->config.cs.gpio.port)) {
    LOG_ERR("SPI CS GPIO not ready");
    return -ENODEV;
  }

  LOG_INF("SPI initialized using device tree specification");

  // Initialize ADS1220
  ads1220_reset(dev);
  ads1220_start(dev);

  // Test connection by bypassing PGA and reading register
  ads1220_bypass_pga(dev, true);
  uint8_t ctrl_val = ads1220_read_register(dev, ADS1220_CONF_REG_0);
  ads1220_bypass_pga(dev, false);

  if (ctrl_val == 1) {
    LOG_INF("ADS1220 detected and initialized successfully");
    return 0;
  } else {
    LOG_ERR("ADS1220 not detected! Expected 0x01, got 0x%02X", ctrl_val);
    return -ENODEV;
  }
}

/* Start conversion */
void ads1220_start(ads1220_dev_t *dev) { ads1220_command(dev, ADS1220_START); }

/* Reset ADS1220 */
void ads1220_reset(ads1220_dev_t *dev) {
  ads1220_command(dev, ADS1220_RESET);
  k_msleep(1); // Wait for reset
}

/* Power down */
void ads1220_power_down(ads1220_dev_t *dev) {
  ads1220_command(dev, ADS1220_PWRDOWN);
}

/* Configuration Register 0 settings */
void ads1220_set_compare_channels(ads1220_dev_t *dev, ads1220_mux_t mux) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_0);
  reg_val &= 0x0F; // Clear MUX bits
  reg_val |= (uint8_t)mux;
  ads1220_write_register(dev, ADS1220_CONF_REG_0, reg_val);

  // Handle special cases for single-ended measurements
  if (mux >= ADS1220_MUX_0_AVSS && mux <= ADS1220_MUX_3_AVSS) {
    if (!dev->do_not_bypass_pga_if_possible) {
      // For single-ended, bypass PGA if gain <= 4
      uint8_t current_gain = ads1220_get_gain_factor(dev);
      if (current_gain <= 4) {
        ads1220_bypass_pga(dev, true);
      }
    }
  }
}

void ads1220_set_gain(ads1220_dev_t *dev, ads1220_gain_t gain) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_0);
  reg_val &= 0xF1; // Clear gain bits
  reg_val |= (uint8_t)gain;
  ads1220_write_register(dev, ADS1220_CONF_REG_0, reg_val);

  // Update gain factor
  dev->gain = 1 << ((gain >> 1) & 0x07);
}

uint8_t ads1220_get_gain_factor(ads1220_dev_t *dev) { return dev->gain; }

void ads1220_bypass_pga(ads1220_dev_t *dev, bool bypass) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_0);
  if (bypass) {
    reg_val |= 0x01; // Set PGA_BYPASS bit
  } else {
    reg_val &= 0xFE; // Clear PGA_BYPASS bit
  }
  ads1220_write_register(dev, ADS1220_CONF_REG_0, reg_val);
}

bool ads1220_is_pga_bypassed(ads1220_dev_t *dev) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_0);
  return (reg_val & 0x01) != 0;
}

/* Configuration Register 1 settings */
void ads1220_set_data_rate(ads1220_dev_t *dev, ads1220_data_rate_t rate) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_1);
  reg_val &= 0x1F; // Clear data rate bits
  reg_val |= (uint8_t)rate;
  ads1220_write_register(dev, ADS1220_CONF_REG_1, reg_val);
}

void ads1220_set_operating_mode(ads1220_dev_t *dev, ads1220_op_mode_t mode) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_1);
  reg_val &= 0xE7; // Clear operating mode bits
  reg_val |= (uint8_t)mode;
  ads1220_write_register(dev, ADS1220_CONF_REG_1, reg_val);
}

void ads1220_set_conversion_mode(ads1220_dev_t *dev, ads1220_conv_mode_t mode) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_1);
  if (mode == ADS1220_CONTINUOUS) {
    reg_val |= 0x04; // Set CC bit
  } else {
    reg_val &= 0xFB; // Clear CC bit
  }
  ads1220_write_register(dev, ADS1220_CONF_REG_1, reg_val);
  dev->conv_mode = mode;
}

void ads1220_enable_temperature_sensor(ads1220_dev_t *dev, bool enable) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_1);
  if (enable) {
    reg_val |= 0x02; // Set TS bit
  } else {
    reg_val &= 0xFD; // Clear TS bit
  }
  ads1220_write_register(dev, ADS1220_CONF_REG_1, reg_val);
}

void ads1220_enable_burnout_current_sources(ads1220_dev_t *dev, bool enable) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_1);
  if (enable) {
    reg_val |= 0x01; // Set BCS bit
  } else {
    reg_val &= 0xFE; // Clear BCS bit
  }
  ads1220_write_register(dev, ADS1220_CONF_REG_1, reg_val);
}

/* Configuration Register 2 settings */
void ads1220_set_vref_source(ads1220_dev_t *dev, ads1220_vref_t vref_source) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_2);
  reg_val &= 0x3F; // Clear VREF bits
  reg_val |= (uint8_t)vref_source;
  ads1220_write_register(dev, ADS1220_CONF_REG_2, reg_val);
}

void ads1220_set_fir_filter(ads1220_dev_t *dev, ads1220_fir_t fir) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_2);
  reg_val &= 0xCF; // Clear FIR bits
  reg_val |= (uint8_t)fir;
  ads1220_write_register(dev, ADS1220_CONF_REG_2, reg_val);
}

void ads1220_set_low_side_power_switch(ads1220_dev_t *dev, ads1220_psw_t psw) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_2);
  reg_val &= 0xF7; // Clear PSW bit
  reg_val |= (uint8_t)psw;
  ads1220_write_register(dev, ADS1220_CONF_REG_2, reg_val);
}

void ads1220_set_idac_current(ads1220_dev_t *dev,
                              ads1220_idac_current_t current) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_2);
  reg_val &= 0xF8; // Clear IDAC bits
  reg_val |= (uint8_t)current;
  ads1220_write_register(dev, ADS1220_CONF_REG_2, reg_val);
}

/* Configuration Register 3 settings */
void ads1220_set_idac1_routing(ads1220_dev_t *dev,
                               ads1220_idac_routing_t route) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_3);
  reg_val &= 0x1F; // Clear IDAC1 routing bits
  reg_val |= ((uint8_t)route << 5);
  ads1220_write_register(dev, ADS1220_CONF_REG_3, reg_val);
}

void ads1220_set_idac2_routing(ads1220_dev_t *dev,
                               ads1220_idac_routing_t route) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_3);
  reg_val &= 0xE3; // Clear IDAC2 routing bits
  reg_val |= ((uint8_t)route << 2);
  ads1220_write_register(dev, ADS1220_CONF_REG_3, reg_val);
}

void ads1220_set_drdy_mode(ads1220_dev_t *dev, ads1220_drdy_mode_t mode) {
  uint8_t reg_val = ads1220_read_register(dev, ADS1220_CONF_REG_3);
  reg_val &= 0xFD; // Clear DRDY mode bit
  reg_val |= (uint8_t)mode;
  ads1220_write_register(dev, ADS1220_CONF_REG_3, reg_val);
}

/* Other settings */
void ads1220_set_spi_clock_speed(ads1220_dev_t *dev, uint32_t clock) {
  // Note: In modern Zephyr 4.2 with spi_dt_spec, SPI frequency is set in device
  // tree Dynamic frequency changes would require recreating the spi_dt_spec
  LOG_WRN("SPI frequency is configured in device tree overlay, runtime changes "
          "not supported");
  ARG_UNUSED(dev);
  ARG_UNUSED(clock);
}

void ads1220_set_vref_value_v(ads1220_dev_t *dev, float ref_val) {
  dev->vref = ref_val;
}

float ads1220_get_vref_v(ads1220_dev_t *dev) { return dev->vref; }

void ads1220_set_avdd_avss_as_vref_and_calibrate(ads1220_dev_t *dev) {
  ads1220_set_vref_source(dev, ADS1220_VREF_AVDD_AVSS);
  ads1220_set_compare_channels(dev, ADS1220_MUX_AVDD_P_AVSS_2);
  dev->ref_measurement = true;

  // Measure supply voltage
  int32_t raw_data = ads1220_get_raw_data(dev);
  float measured_vref = (float)raw_data * 2.048f / ADS1220_RANGE * 2.0f;

  dev->vref = measured_vref;
  dev->ref_measurement = false;
  LOG_INF("Calibrated AVDD/AVSS reference: %.3fV", (double)dev->vref);
}

void ads1220_set_refp0_refn0_as_vref_and_calibrate(ads1220_dev_t *dev) {
  ads1220_set_vref_source(dev, ADS1220_VREF_REFP0_REFN0);
  ads1220_set_compare_channels(dev, ADS1220_MUX_REFPX_REFNX_4);
  dev->ref_measurement = true;

  // Measure reference voltage
  int32_t raw_data = ads1220_get_raw_data(dev);
  float measured_vref = (float)raw_data * 2.048f / ADS1220_RANGE * 4.0f;

  dev->vref = measured_vref;
  dev->ref_measurement = false;
  LOG_INF("Calibrated REFP0/REFN0 reference: %.3fV", (double)dev->vref);
}

void ads1220_set_refp1_refn1_as_vref_and_calibrate(ads1220_dev_t *dev) {
  ads1220_set_vref_source(dev, ADS1220_VREF_REFP1_REFN1);
  ads1220_set_compare_channels(dev, ADS1220_MUX_REFPX_REFNX_4);
  dev->ref_measurement = true;

  // Measure reference voltage
  int32_t raw_data = ads1220_get_raw_data(dev);
  float measured_vref = (float)raw_data * 2.048f / ADS1220_RANGE * 4.0f;

  dev->vref = measured_vref;
  dev->ref_measurement = false;
  LOG_INF("Calibrated REFP1/REFN1 reference: %.3fV", (double)dev->vref);
}

void ads1220_set_int_vref(ads1220_dev_t *dev) {
  ads1220_set_vref_source(dev, ADS1220_VREF_INT);
  dev->vref = 2.048f;
}

void ads1220_set_non_blocking_mode(ads1220_dev_t *dev, bool non_blocking) {
  dev->non_blocking_mode = non_blocking;
}

bool ads1220_get_non_blocking_mode(ads1220_dev_t *dev) {
  return dev->non_blocking_mode;
}

/* Results */
float ads1220_get_voltage_mv(ads1220_dev_t *dev) {
  int32_t raw_data = ads1220_get_raw_data(dev);
  if (dev->ref_measurement) {
    return (float)raw_data * 2.048f / ADS1220_RANGE * 1000.0f;
  } else {
    return (float)raw_data * dev->vref / ADS1220_RANGE * 1000.0f /
           (float)dev->gain;
  }
}

float ads1220_get_voltage_muv(ads1220_dev_t *dev) {
  return ads1220_get_voltage_mv(dev) * 1000.0f;
}

int32_t ads1220_get_raw_data(ads1220_dev_t *dev) {
  return ads1220_get_data(dev);
}

float ads1220_get_temperature(ads1220_dev_t *dev) {
  ads1220_enable_temperature_sensor(dev, true);
  k_msleep(100); // Wait for temperature sensor to stabilize

  int32_t raw_data = ads1220_get_data(dev);

  // Temperature calculation according to datasheet
  // T = (raw_data / 2^23) * 2.048V / 90.5µV/°C
  float temp_c = (float)raw_data / ADS1220_RANGE * 2048000.0f / 90.5f;

  ads1220_enable_temperature_sensor(dev, false);
  return temp_c;
}

/* Low-level functions */
int32_t ads1220_get_data(ads1220_dev_t *dev) {
  if (dev->conv_mode == ADS1220_SINGLE_SHOT) {
    ads1220_start(dev);
    k_msleep(100); // Wait for conversion (depends on data rate)
  }

  uint32_t raw_result = ads1220_read_result(dev);

  // Convert to signed 24-bit value
  int32_t result;
  if (raw_result & 0x800000) {
    // Negative value - sign extend
    result = (int32_t)(raw_result | 0xFF000000);
  } else {
    // Positive value
    result = (int32_t)(raw_result & 0x7FFFFF);
  }

  return result;
}

uint32_t ads1220_read_result(ads1220_dev_t *dev) {
  ads1220_command(dev, ADS1220_RDATA);

  uint8_t rx_buf[3] = {0};
  struct spi_buf rx_spi_buf = {.buf = rx_buf, .len = sizeof(rx_buf)};
  struct spi_buf_set rx_spi_buf_set = {.buffers = &rx_spi_buf, .count = 1};

  spi_read_dt(&dev->spi, &rx_spi_buf_set);

  // Combine 3 bytes into 24-bit result
  uint32_t result = ((uint32_t)rx_buf[0] << 16) | ((uint32_t)rx_buf[1] << 8) |
                    (uint32_t)rx_buf[2];

  LOG_DBG("Read result: 0x%06X", result);
  return result;
}

uint8_t ads1220_read_register(ads1220_dev_t *dev, uint8_t reg) {
  uint8_t cmd = ADS1220_RREG | (reg & 0x03);

  ads1220_set_cs_pin(dev, 0);
  k_usleep(1);

  uint8_t tx_buf[2] = {cmd, 0x00};
  uint8_t rx_buf[2] = {0};

  struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = 2};
  struct spi_buf rx_spi_buf = {.buf = rx_buf, .len = 2};
  struct spi_buf_set tx_spi_buf_set = {.buffers = &tx_spi_buf, .count = 1};
  struct spi_buf_set rx_spi_buf_set = {.buffers = &rx_spi_buf, .count = 1};

  spi_transceive_dt(&dev->spi, &tx_spi_buf_set, &rx_spi_buf_set);

  k_usleep(1);
  ads1220_set_cs_pin(dev, 1);

  LOG_DBG("Read register 0x%02X: 0x%02X", reg, rx_buf[1]);
  return rx_buf[1];
}

void ads1220_write_register(ads1220_dev_t *dev, uint8_t reg, uint8_t val) {
  uint8_t cmd = ADS1220_WREG | (reg & 0x03);

  ads1220_set_cs_pin(dev, 0);
  k_usleep(1);

  uint8_t tx_buf[2] = {cmd, val};
  struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = 2};
  struct spi_buf_set tx_spi_buf_set = {.buffers = &tx_spi_buf, .count = 1};

  spi_write_dt(&dev->spi, &tx_spi_buf_set);

  k_usleep(1);
  ads1220_set_cs_pin(dev, 1);

  LOG_DBG("Write register 0x%02X: 0x%02X", reg, val);
}

void ads1220_command(ads1220_dev_t *dev, uint8_t cmd) {
  ads1220_set_cs_pin(dev, 0);
  k_usleep(1);

  uint8_t tx_buf = cmd;
  struct spi_buf tx_spi_buf = {.buf = &tx_buf, .len = 1};
  struct spi_buf_set tx_spi_buf_set = {.buffers = &tx_spi_buf, .count = 1};

  spi_write_dt(&dev->spi, &tx_spi_buf_set);

  k_usleep(1);
  ads1220_set_cs_pin(dev, 1);

  LOG_DBG("Command: 0x%02X", cmd);
}

void ads1220_set_cs_pin(ads1220_dev_t *dev, uint8_t state) {
  // With modern spi_dt_spec, CS is usually handled automatically
  // Manual CS control only if needed for special timing requirements
  if (dev->spi->config.cs.gpio.port) {
    gpio_pin_set_dt(&dev->spi->config.cs.gpio, state);
  }
}
