/* SPDX-License-Identifier: Apache-2.0 */

#include "rs485_task.h"

#include <string.h>

#include <zephyr/sys/printk.h>

static K_THREAD_STACK_DEFINE(rs485_task_stack,
			     CONFIG_BRIDGE_RS485_TASK_THREAD_STACK_SIZE);
static struct k_thread rs485_task_data;

static void publish_rs485_event(struct app_context *ctx, enum app_msg_type type,
				const struct rs485_modbus_state *state,
				int error)
{
	struct app_msg msg = {
		.type = type,
		.timestamp_ms = k_uptime_get(),
	};

	msg.data.rs485.error = error;
	if (state != NULL) {
		msg.data.rs485.reg_count = (uint8_t)state->latest_reg_count;
		for (uint8_t i = 0; i < msg.data.rs485.reg_count &&
				    i < APP_MSG_RS485_MAX_REGS;
		     i++) {
			msg.data.rs485.regs[i] = state->regs[i];
		}
	}

	(void)app_msg_publish(&ctx->app_msgq, &msg);
}

static void rs485_task_thread(void *arg1, void *arg2, void *arg3)
{
	struct app_context *ctx = arg1;
	uint32_t seen_response_count = 0;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		struct rs485_modbus_state state;
		int ret;

		bridge_state_mark_poll(&ctx->bridge);

		ret = rs485_modbus_request_read_regs(
			&ctx->rs485, CONFIG_BRIDGE_RS485_FUNCTION_CODE,
			CONFIG_BRIDGE_RS485_SLAVE_ADDR,
			CONFIG_BRIDGE_RS485_START_REG,
			CONFIG_BRIDGE_RS485_REG_COUNT);
		if (ret != 0) {
			bridge_state_mark_timeout(&ctx->bridge, ret);
			publish_rs485_event(ctx, APP_MSG_RS485_POLL_TIMEOUT,
					    NULL, ret);
			k_sleep(K_MSEC(CONFIG_BRIDGE_RS485_POLL_PERIOD_MS));
			continue;
		}

		k_sleep(K_MSEC(CONFIG_BRIDGE_RS485_RESPONSE_TIMEOUT_MS + 20));
		rs485_modbus_state_get(&ctx->rs485, &state);

		if (state.response_count != seen_response_count &&
		    state.value_valid) {
			seen_response_count = state.response_count;
			bridge_state_update_from_rs485(
				&ctx->bridge, state.regs,
				(uint8_t)state.latest_reg_count);
			if (IS_ENABLED(CONFIG_BRIDGE_CAN_ENABLE)) {
				ret = bridge_can_service_publish_measurement(
					&ctx->can_service);
				if (ret != 0) {
					printk("BRIDGE CAN telemetry queue failed ret=%d\r\n", ret);
				}
			}
			publish_rs485_event(ctx, APP_MSG_RS485_VALUE_UPDATE,
					    &state, 0);
		} else if (state.last_error != 0) {
			bridge_state_mark_timeout(&ctx->bridge,
						  state.last_error);
			publish_rs485_event(ctx, APP_MSG_RS485_POLL_TIMEOUT,
					    &state, state.last_error);
		} else {
			publish_rs485_event(ctx, APP_MSG_RS485_POLL_OK, &state,
					    0);
		}

		k_sleep(K_MSEC(CONFIG_BRIDGE_RS485_POLL_PERIOD_MS));
	}
}

int rs485_task_start(struct app_context *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	k_thread_create(&rs485_task_data, rs485_task_stack,
			K_THREAD_STACK_SIZEOF(rs485_task_stack),
			rs485_task_thread, ctx, NULL, NULL,
			CONFIG_BRIDGE_RS485_TASK_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}
