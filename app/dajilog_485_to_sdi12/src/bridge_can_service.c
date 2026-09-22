/* SPDX-License-Identifier: Apache-2.0 */

#include "bridge_can_service.h"

#include <errno.h>

#include <zephyr/sys/printk.h>

#include "app_context.h"

void bridge_device_runtime_get(void *user_data,
			       struct dajilog_device_runtime *runtime)
{
	struct app_context *app = user_data;
	struct bridge_state_snapshot snapshot;

	if (runtime == NULL) {
		return;
	}

	*runtime = (struct dajilog_device_runtime){
		.health = DAJILOG_DEVICE_HEALTH_UNASSIGNED,
		.last_error = -ENODEV,
	};
	if (app == NULL) {
		return;
	}

	bridge_state_snapshot_get(&app->bridge, &snapshot);
	if (app->fsm.state == APP_STATE_FATAL_ERROR) {
		runtime->health = DAJILOG_DEVICE_HEALTH_FAULT;
		runtime->last_error = app->fsm.last_error;
		return;
	}

	if (snapshot.value_valid && snapshot.last_error == 0) {
		runtime->health = DAJILOG_DEVICE_HEALTH_READY;
		runtime->last_error = 0;
		return;
	}

	runtime->health = DAJILOG_DEVICE_HEALTH_DEGRADED;
	runtime->last_error = snapshot.last_error != 0 ? snapshot.last_error : -EAGAIN;
}

static bool bridge_can_is_device_info_request(
	const struct bridge_can_service *ctx, const struct can_frame *frame)
{
	struct dajilog_device_profile_state profile;

	if (ctx == NULL || ctx->device_info == NULL ||
	    ctx->device_info->profile == NULL || frame == NULL) {
		return false;
	}

	dajilog_device_profile_get(ctx->device_info->profile, &profile);
	return profile.valid &&
	       frame->id == DAJILOG_DEVICE_INFO_REQUEST_BASE_ID + profile.data.node_id;
}

static bool bridge_can_is_measurement_request(
	const struct bridge_can_service *ctx, const struct can_frame *frame)
{
	struct dajilog_device_profile_state profile;

	if (ctx == NULL || ctx->device_info == NULL ||
	    ctx->device_info->profile == NULL || frame == NULL || frame->dlc < 3U) {
		return false;
	}

	dajilog_device_profile_get(ctx->device_info->profile, &profile);
	if (!profile.valid ||
	    frame->id != BRIDGE_CAN_TELEMETRY_BASE_ID + profile.data.node_id ||
	    frame->data[0] != BRIDGE_CAN_PROTOCOL_VERSION ||
	    frame->data[1] != BRIDGE_CAN_OP_MEASUREMENT) {
		return false;
	}

	/* A request has no health/value payload; a response always sets health. */
	for (uint8_t i = 3U; i < frame->dlc; i++) {
		if (frame->data[i] != 0U) {
			return false;
		}
	}

	return true;
}

static void bridge_can_handle_device_info(struct bridge_can_service *ctx,
					 const struct can_frame *request)
{
	struct can_frame response;
	struct dajilog_device_runtime runtime;
	int ret;

	bridge_device_runtime_get(ctx->app, &runtime);
	ret = dajilog_device_info_handle_request(ctx->device_info, &runtime,
					 request, &response);
	if (ret == 0) {
		ret = can_bus_send(ctx->bus, &response);
	}

	printk("BRIDGE DEVICE INFO req=0x%03x op=0x%02x seq=%u response=0x%03x ret=%d health=%u\r\n",
	       request->id, request->data[1], request->data[2],
	       ret == 0 ? response.id : 0U, ret, runtime.health);
}

int bridge_can_service_init(struct bridge_can_service *ctx, struct can_bus *bus,
			    struct app_context *app,
			    struct dajilog_device_info *device_info)
{
	if (ctx == NULL || bus == NULL || app == NULL || device_info == NULL) {
		return -EINVAL;
	}

	*ctx = (struct bridge_can_service){
		.bus = bus,
		.app = app,
		.device_info = device_info,
	};
	return 0;
}

void bridge_can_service_process(struct bridge_can_service *ctx,
				const struct can_frame *frame)
{
	if (ctx == NULL || frame == NULL) {
		return;
	}

	if (bridge_can_is_device_info_request(ctx, frame)) {
		bridge_can_handle_device_info(ctx, frame);
	} else if (bridge_can_is_measurement_request(ctx, frame)) {
		int ret = bridge_can_service_respond_measurement(ctx, frame->data[2]);

		printk("BRIDGE CAN measurement request id=0x%03x seq=%u ret=%d\r\n",
		       frame->id, frame->data[2], ret);
	}
}

static int bridge_can_send_measurement(struct bridge_can_service *ctx,
				       uint8_t sequence)
{
	struct bridge_state_snapshot snapshot;
	struct dajilog_device_profile_state profile;
	struct dajilog_device_runtime runtime;
	struct can_frame frame = {
		.dlc = 8U,
		.flags = 0U,
	};
	uint16_t temperature;
	uint16_t humidity;
	int ret;

	if (ctx == NULL || ctx->bus == NULL || ctx->app == NULL ||
	    ctx->device_info == NULL || ctx->device_info->profile == NULL) {
		return -EINVAL;
	}

	bridge_state_snapshot_get(&ctx->app->bridge, &snapshot);
	if (!snapshot.value_valid || snapshot.reg_count < 2U) {
		return -EAGAIN;
	}

	dajilog_device_profile_get(ctx->device_info->profile, &profile);
	if (!profile.valid) {
		return -ENODEV;
	}

	/* Sensor map is humidity=reg[0], temperature=reg[1], both x10. */
	humidity = snapshot.regs[0];
	temperature = snapshot.regs[1];
	bridge_device_runtime_get(ctx->app, &runtime);

	frame.id = BRIDGE_CAN_TELEMETRY_BASE_ID + profile.data.node_id;
	frame.data[0] = BRIDGE_CAN_PROTOCOL_VERSION;
	frame.data[1] = BRIDGE_CAN_OP_MEASUREMENT;
	frame.data[2] = sequence;
	frame.data[3] = runtime.health;
	frame.data[4] = (uint8_t)(temperature & 0xffU);
	frame.data[5] = (uint8_t)(temperature >> 8);
	frame.data[6] = (uint8_t)(humidity & 0xffU);
	frame.data[7] = (uint8_t)(humidity >> 8);

	ret = can_bus_send(ctx->bus, &frame);
	if (ret == 0) {
		printk("BRIDGE CAN TX id=0x%03x seq=%u temp_x10=%u hum_x10=%u health=%u\r\n",
		       frame.id, frame.data[2], temperature, humidity, runtime.health);
	}

	return ret;
}

int bridge_can_service_publish_measurement(struct bridge_can_service *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	return bridge_can_send_measurement(ctx, ctx->telemetry_sequence++);
}

int bridge_can_service_respond_measurement(struct bridge_can_service *ctx,
				   uint8_t sequence)
{
	return bridge_can_send_measurement(ctx, sequence);
}
