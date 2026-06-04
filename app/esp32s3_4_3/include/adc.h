#ifndef ADC_H
#define ADC_H

#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include "ads1220_we.h"

// Function declarations
int adc_init(void);
int adc_read_channel(uint8_t channel, int32_t *value);
void adc_test_loop(void);
void adc_app_start(void);

// ADS1220_WE specific functions
int ads1220_read_differential(ads1220_mux_t mux_config, int32_t *value);
int ads1220_read_single_ended(uint8_t channel, int32_t *value);
float adc_raw_to_voltage(int32_t raw_value);

#endif