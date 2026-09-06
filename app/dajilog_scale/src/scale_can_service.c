/* SPDX-License-Identifier: Apache-2.0 */

#include "scale_can_service.h"

#include <errno.h>

#include "scale_can_protocol.h"

K_THREAD_STACK_DEFINE(scale_can_service_stack, CONFIG_SCALE_CAN_THREAD_STACK_SIZE);

static bool scale_can_is_device_info_request(const struct scale_can_service *ctx,
					      const struct can_frame *frame)
{
	struct dajilog_device_profile_state profile;

	if (ctx->device_info == NULL || ctx->device_info->profile == NULL) {
		return false;
	}

	dajilog_device_profile_get(ctx->device_info->profile, &profile);
	return profile.valid &&
	       frame->id == DAJILOG_DEVICE_INFO_REQUEST_BASE_ID + profile.data.node_id;
}

static struct dajilog_device_runtime scale_can_runtime_state(
	const struct scale_state *state)
{
	if (!state->ready || !state->value_valid || !state->calibration.valid ||
	    state->last_error != 0) {
		return (struct dajilog_device_runtime){
			.health = DAJILOG_DEVICE_HEALTH_DEGRADED,
			.last_error = state->last_error,
		};
	}

	return (struct dajilog_device_runtime){
		.health = DAJILOG_DEVICE_HEALTH_READY,
		.last_error = 0,
	};
}

static void scale_can_handle_device_info(struct scale_can_service *ctx,
					 const struct can_frame *request)
{
	struct can_frame response;
	struct scale_state state;
	struct dajilog_device_runtime runtime;
	int ret;

	scale_service_state_get(ctx->scale, &state);
	runtime = scale_can_runtime_state(&state);
	ret = dajilog_device_info_handle_request(ctx->device_info, &runtime,
					 request, &response);
	if (ret == 0) {
		ret = can_bus_send(ctx->bus, &response);
	}

	if (IS_ENABLED(CONFIG_SCALE_CAN_DIAGNOSTIC)) {
		printk("DEVICE INFO req=0x%03x op=0x%02x seq=%u response=0x%03x ret=%d health=%u\r\n",
		       request->id, request->data[1], request->data[2],
		       ret == 0 ? response.id : 0U,
		       ret, runtime.health);
	}
}

static void scale_can_service_thread(void *arg1, void *arg2, void *arg3)
{
	struct scale_can_service *ctx = arg1;
	int64_t next_measurement = k_uptime_get();

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		struct can_frame rx;
		struct scale_command_msg command;
		struct scale_state state;
		int ret;

		ret = can_bus_receive(ctx->bus, &rx, K_MSEC(50));
		if (ret == 0 && scale_can_is_device_info_request(ctx, &rx)) {
			scale_can_handle_device_info(ctx, &rx);
		} else if (ret == 0 && rx.id == SCALE_CAN_ID_COMMAND) {
			ret = scale_can_decode_command(&rx, &command);
			if (ret == 0) {
				ret = scale_service_post_command(ctx->scale, &command);
				if (IS_ENABLED(CONFIG_SCALE_CAN_DIAGNOSTIC)) {
					printk("SCALE CAN RX std id=0x%03x op=0x%02x seq=%u value_mg=%ld post=%d\r\n",
					       rx.id, rx.data[1], command.sequence,
					       (long)command.value_mg, ret);
				}

				if (command.command == SCALE_CMD_GET_STATUS) {
					struct can_frame status;

					scale_service_state_get(ctx->scale, &state);
					scale_can_encode_status(&status, &state, command.sequence);
					(void)can_bus_send(ctx->bus, &status);
				}
			} else if (IS_ENABLED(CONFIG_SCALE_CAN_DIAGNOSTIC)) {
				printk("SCALE CAN RX reject id=0x%03x op=0x%02x ret=%d\r\n",
				       rx.id, rx.data[1], ret);
			}
		}

		if (k_uptime_get() < next_measurement) {
			continue;
		}

		scale_service_state_get(ctx->scale, &state);
		if (state.value_valid) {
			struct can_frame tx;

			scale_can_encode_measurement(&tx, &state);
			ret = can_bus_send(ctx->bus, &tx);
			if (IS_ENABLED(CONFIG_SCALE_CAN_DIAGNOSTIC)) {
				printk("SCALE CAN TX std id=0x%03x net_mg=%ld stable=%d ret=%d\r\n",
				       tx.id, (long)state.net_weight_mg, state.stable, ret);
			}
		}
		next_measurement += CONFIG_SCALE_CAN_MEASUREMENT_PERIOD_MS;
	}
}

int scale_can_service_init(struct scale_can_service *ctx, struct can_bus *bus,
			   struct scale_service *scale,
			   struct dajilog_device_info *device_info)
{
	if (ctx == NULL || bus == NULL || scale == NULL || device_info == NULL) {
		return -EINVAL;
	}

	ctx->bus = bus;
	ctx->scale = scale;
	ctx->device_info = device_info;
	return 0;
}

int scale_can_service_start(struct scale_can_service *ctx)
{
	if (ctx == NULL || ctx->bus == NULL || ctx->scale == NULL ||
	    ctx->device_info == NULL) {
		return -EINVAL;
	}

	k_thread_create(&ctx->thread, scale_can_service_stack,
			K_THREAD_STACK_SIZEOF(scale_can_service_stack), scale_can_service_thread,
			ctx, NULL, NULL, CONFIG_SCALE_CAN_THREAD_PRIORITY, 0, K_NO_WAIT);
	return 0;
}
