/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_ADS1232_H_
#define DAJILOG_ADS1232_H_

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

struct ads1232_config {
	struct gpio_dt_spec sclk;
	struct gpio_dt_spec dout;
	struct gpio_dt_spec pwdn;
	uint32_t drdy_timeout_ms;
	uint32_t startup_delay_ms;
	uint32_t sclk_half_period_us;
};

struct ads1232_state {
	bool ready;
	bool value_valid;
	int last_error;
	int32_t latest_raw;
	int32_t min_raw;
	int32_t max_raw;
	int64_t sum_raw;
	uint32_t sample_count;
};

struct ads1232 {
	struct ads1232_config config;
	struct ads1232_state state;
};

int ads1232_init(struct ads1232 *ctx);
int ads1232_read_raw(struct ads1232 *ctx, int32_t *out);
int ads1232_sample(struct ads1232 *ctx, int32_t *out);
void ads1232_reset_stats(struct ads1232 *ctx);
void ads1232_state_get(const struct ads1232 *ctx, struct ads1232_state *out);

#endif /* DAJILOG_ADS1232_H_ */
