/* SPDX-License-Identifier: Apache-2.0 */

#include "ads1232.h"

#include <limits.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>

static int ads1232_wait_drdy(struct ads1232 *ctx)
{
	int64_t deadline = k_uptime_get() + ctx->config.drdy_timeout_ms;

	while (k_uptime_get() <= deadline) {
		int dout = gpio_pin_get_dt(&ctx->config.dout);

		if (dout < 0) {
			return dout;
		}
		if (dout == 0) {
			return 0;
		}
		k_sleep(K_MSEC(1));
	}

	return -ETIMEDOUT;
}

static int32_t ads1232_sign_extend_24(uint32_t value)
{
	if ((value & 0x800000U) != 0) {
		value |= 0xff000000U;
	}

	return (int32_t)value;
}

void ads1232_reset_stats(struct ads1232 *ctx)
{
	ctx->state.value_valid = false;
	ctx->state.latest_raw = 0;
	ctx->state.min_raw = INT32_MAX;
	ctx->state.max_raw = INT32_MIN;
	ctx->state.sum_raw = 0;
	ctx->state.sample_count = 0;
}

int ads1232_init(struct ads1232 *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ads1232_reset_stats(ctx);
	ctx->state.ready = false;
	ctx->state.last_error = 0;

	if (!device_is_ready(ctx->config.sclk.port) ||
	    !device_is_ready(ctx->config.dout.port) ||
	    !device_is_ready(ctx->config.pwdn.port)) {
		ctx->state.last_error = -ENODEV;
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&ctx->config.sclk, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	ret = gpio_pin_configure_dt(&ctx->config.pwdn, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	ret = gpio_pin_configure_dt(&ctx->config.dout, GPIO_INPUT);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	gpio_pin_set_dt(&ctx->config.sclk, 0);
	gpio_pin_set_dt(&ctx->config.pwdn, 1);
	k_sleep(K_MSEC(ctx->config.startup_delay_ms));

	ctx->state.ready = true;
	return 0;
}

int ads1232_read_raw(struct ads1232 *ctx, int32_t *out)
{
	uint32_t raw = 0;
	int ret;

	if (ctx == NULL || out == NULL) {
		return -EINVAL;
	}

	if (!ctx->state.ready) {
		return -ENODEV;
	}

	ret = ads1232_wait_drdy(ctx);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	for (int i = 0; i < 24; i++) {
		int dout;

		ret = gpio_pin_set_dt(&ctx->config.sclk, 1);
		if (ret != 0) {
			goto error;
		}
		k_busy_wait(ctx->config.sclk_half_period_us);
		dout = gpio_pin_get_dt(&ctx->config.dout);
		if (dout < 0) {
			ret = dout;
			goto error;
		}
		raw = (raw << 1) | (dout != 0 ? 1U : 0U);
		ret = gpio_pin_set_dt(&ctx->config.sclk, 0);
		if (ret != 0) {
			goto error;
		}
		k_busy_wait(ctx->config.sclk_half_period_us);
	}

	/* 25 clocks select channel A with gain 128 for the next conversion. */
	ret = gpio_pin_set_dt(&ctx->config.sclk, 1);
	if (ret != 0) {
		goto error;
	}
	k_busy_wait(ctx->config.sclk_half_period_us);
	ret = gpio_pin_set_dt(&ctx->config.sclk, 0);
	if (ret != 0) {
		goto error;
	}
	k_busy_wait(ctx->config.sclk_half_period_us);

	*out = ads1232_sign_extend_24(raw);
	ctx->state.last_error = 0;
	return 0;

error:
	(void)gpio_pin_set_dt(&ctx->config.sclk, 0);
	ctx->state.last_error = ret;
	return ret;
}

int ads1232_sample(struct ads1232 *ctx, int32_t *out)
{
	int32_t raw;
	int ret;

	ret = ads1232_read_raw(ctx, &raw);
	if (ret != 0) {
		return ret;
	}

	ctx->state.latest_raw = raw;
	ctx->state.value_valid = true;
	ctx->state.sample_count++;
	ctx->state.sum_raw += raw;

	if (raw < ctx->state.min_raw) {
		ctx->state.min_raw = raw;
	}

	if (raw > ctx->state.max_raw) {
		ctx->state.max_raw = raw;
	}

	if (out != NULL) {
		*out = raw;
	}

	return 0;
}

void ads1232_state_get(const struct ads1232 *ctx, struct ads1232_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
}
