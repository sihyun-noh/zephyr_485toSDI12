/* SPDX-License-Identifier: Apache-2.0 */

#include "scale_service.h"

#include <errno.h>
#include <limits.h>

#include <zephyr/sys/printk.h>

#include "calibration_fsm.h"
#include "calibration_store.h"

K_THREAD_STACK_DEFINE(scale_service_stack, CONFIG_SCALE_SERVICE_THREAD_STACK_SIZE);

static void scale_update_weight(struct scale_state *state)
{
	int64_t numerator;

	if (!state->calibration.valid ||
	    state->calibration.span_raw == state->calibration.zero_raw) {
		state->gross_weight_mg = 0;
		state->net_weight_mg = 0;
		return;
	}

	numerator = (int64_t)(state->filtered_raw - state->calibration.zero_raw) *
		    state->calibration.reference_weight_mg;
	state->gross_weight_mg = (int32_t)(numerator /
		(state->calibration.span_raw - state->calibration.zero_raw));
	state->net_weight_mg = state->gross_weight_mg - state->tare_weight_mg;
}

static void scale_process_command(struct scale_service *ctx,
				  const struct scale_command_msg *command)
{
	struct scale_state *state = &ctx->state;
	int ret;

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (command->command == SCALE_CMD_TARE_SET) {
		if (!state->calibration.valid) {
			ret = -EPERM;
		} else {
			state->tare_weight_mg = state->gross_weight_mg;
			state->tare_active = true;
			state->net_weight_mg = 0;
			ret = 0;
		}
	} else if (command->command == SCALE_CMD_TARE_CLEAR) {
		state->tare_weight_mg = 0;
		state->tare_active = false;
		scale_update_weight(state);
		ret = 0;
	} else if (command->command == SCALE_CMD_CAL_SAVE) {
		ret = calibration_fsm_handle(state, command);
		if (ret == 0) {
			ret = calibration_store_save(&state->calibration);
			state->calibration_state = ret == 0 ? SCALE_CAL_IDLE : SCALE_CAL_FAILED;
		}
	} else if (command->command == SCALE_CMD_GET_STATUS) {
		ret = 0;
	} else {
		ret = calibration_fsm_handle(state, command);
	}
	state->last_error = ret;
	k_mutex_unlock(&ctx->lock);

	printk("SCALE cmd=%d seq=%u ret=%d cal=%s\r\n", command->command,
	       command->sequence, ret, calibration_state_name(state->calibration_state));
}

static void scale_sample(struct scale_service *ctx, int64_t *sum,
			 int32_t samples[CONFIG_SCALE_FILTER_WINDOW_SAMPLES], uint8_t *next,
			 uint8_t *count)
{
	int32_t raw;
	int32_t min = INT32_MAX;
	int32_t max = INT32_MIN;
	int ret = ads1232_sample(ctx->adc, &raw);

	k_mutex_lock(&ctx->lock, K_FOREVER);
	if (ret != 0) {
		ctx->state.last_error = ret;
		k_mutex_unlock(&ctx->lock);
		return;
	}

	ctx->state.ready = true;
	ctx->state.value_valid = true;
	ctx->state.last_error = 0;
	ctx->state.latest_raw = raw;

	if (IS_ENABLED(CONFIG_SCALE_RAW_DIAGNOSTIC_MODE)) {
		ctx->state.filtered_raw = raw;
		ctx->state.p2p_raw = 0;
		ctx->state.stable = true;
	} else {
		if (*count == CONFIG_SCALE_FILTER_WINDOW_SAMPLES) {
			*sum -= samples[*next];
		} else {
			(*count)++;
		}
		samples[*next] = raw;
		*sum += raw;
		*next = (*next + 1U) % CONFIG_SCALE_FILTER_WINDOW_SAMPLES;

		for (uint8_t i = 0; i < *count; i++) {
			if (samples[i] < min) {
				min = samples[i];
			}
			if (samples[i] > max) {
				max = samples[i];
			}
		}
		ctx->state.filtered_raw = (int32_t)(*sum / *count);
		ctx->state.p2p_raw = max - min;
		ctx->state.stable = *count == CONFIG_SCALE_FILTER_WINDOW_SAMPLES &&
				    ctx->state.p2p_raw <= CONFIG_SCALE_STABLE_P2P_RAW_LIMIT;
	}
	ctx->state.sample_count++;
	scale_update_weight(&ctx->state);
	k_mutex_unlock(&ctx->lock);
}

static void scale_service_thread(void *arg1, void *arg2, void *arg3)
{
	struct scale_service *ctx = arg1;
	struct scale_command_msg command;
	int32_t samples[CONFIG_SCALE_FILTER_WINDOW_SAMPLES] = {0};
	int64_t sum = 0;
	int64_t next_sample = k_uptime_get();
	uint8_t count = 0;
	uint8_t next = 0;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		int64_t now = k_uptime_get();
		int32_t timeout = now >= next_sample ? 0 : (int32_t)(next_sample - now);

		if (k_msgq_get(&ctx->command_msgq, &command, K_MSEC(timeout)) == 0) {
			scale_process_command(ctx, &command);
		}

		if (k_uptime_get() >= next_sample) {
			scale_sample(ctx, &sum, samples, &next, &count);
			next_sample += CONFIG_SCALE_SAMPLE_PERIOD_MS;
		}
	}
}

int scale_service_init(struct scale_service *ctx, struct ads1232 *adc)
{
	int ret;

	if (ctx == NULL || adc == NULL) {
		return -EINVAL;
	}

	ctx->adc = adc;
	ctx->state = (struct scale_state){0};
	k_mutex_init(&ctx->lock);
	k_msgq_init(&ctx->command_msgq, ctx->command_buffer,
		    sizeof(struct scale_command_msg), CONFIG_SCALE_SERVICE_MSGQ_DEPTH);
	calibration_fsm_reset(&ctx->state);

	if (IS_ENABLED(CONFIG_SCALE_LOAD_CALIBRATION_AT_BOOT)) {
		ret = calibration_store_load(&ctx->state.calibration);
		if (ret != 0) {
			ctx->state.last_error = ret;
		}
	}

	return 0;
}

int scale_service_start(struct scale_service *ctx)
{
	if (ctx == NULL || ctx->adc == NULL) {
		return -EINVAL;
	}

	k_thread_create(&ctx->thread, scale_service_stack,
			K_THREAD_STACK_SIZEOF(scale_service_stack), scale_service_thread,
			ctx, NULL, NULL, CONFIG_SCALE_SERVICE_THREAD_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

int scale_service_post_command(struct scale_service *ctx,
			       const struct scale_command_msg *command)
{
	if (ctx == NULL || command == NULL) {
		return -EINVAL;
	}

	return k_msgq_put(&ctx->command_msgq, command, K_NO_WAIT);
}

void scale_service_state_get(struct scale_service *ctx, struct scale_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	k_mutex_lock(&ctx->lock, K_FOREVER);
	*out = ctx->state;
	k_mutex_unlock(&ctx->lock);
}
