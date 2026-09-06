/* SPDX-License-Identifier: Apache-2.0 */

#include "sdi12_task.h"

#include <string.h>

#include <zephyr/sys/printk.h>

static K_THREAD_STACK_DEFINE(sdi12_task_stack,
			     CONFIG_BRIDGE_SDI12_TASK_THREAD_STACK_SIZE);
static struct k_thread sdi12_task_data;

static void sdi12_task_thread(void *arg1, void *arg2, void *arg3)
{
	struct app_context *ctx = arg1;
	uint32_t seen_command_count = 0;
	int64_t next_rx_log_ms = 0;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		struct sdi12_slave_state state;

		sdi12_slave_state_get(&ctx->sdi12, &state);
		if (state.command_count != seen_command_count) {
			struct app_msg msg = {
				.type = APP_MSG_SDI12_COMMAND_RX,
				.timestamp_ms = k_uptime_get(),
			};

			seen_command_count = state.command_count;
			strncpy(msg.data.sdi12.command, state.latest_command,
				sizeof(msg.data.sdi12.command) - 1);
			msg.data.sdi12.command[sizeof(msg.data.sdi12.command) -
					       1] = '\0';
			printk("SDI12 RX command=\"%s\" cmd=%lu rsp=%lu irq=%lu "
			       "break=%lu fail=%lu timeout=%lu rxerr=%lu "
			       "unsupported=%lu raw=%d logical=%d\r\n",
			       state.latest_command,
			       (unsigned long)state.command_count,
			       (unsigned long)state.response_count,
			       (unsigned long)state.irq_count,
			       (unsigned long)state.break_mark_count,
			       (unsigned long)state.break_fail_count,
			       (unsigned long)state.timeout_count,
			       (unsigned long)state.rx_error_count,
			       (unsigned long)state.unsupported_count,
			       state.rx_raw, state.rx_logical);
			(void)app_msg_publish(&ctx->app_msgq, &msg);
		}

		if (k_uptime_get() >= next_rx_log_ms) {
			printk("SDI12 RX status raw=%d logical=%d irq=%lu "
			       "break=%lu fail=%lu initial_high=%lu short=%lu "
			       "mark_timeout=%lu low_us=%d high_us=%d cmd=%lu "
			       "rsp=%lu timeout=%lu rxerr=%lu last=\"%s\"\r\n",
			       state.rx_raw, state.rx_logical,
			       (unsigned long)state.irq_count,
			       (unsigned long)state.break_mark_count,
			       (unsigned long)state.break_fail_count,
			       (unsigned long)state.break_initial_high_count,
			       (unsigned long)state.break_short_count,
			       (unsigned long)state.mark_timeout_count,
			       state.last_break_low_us,
			       state.last_mark_high_us,
			       (unsigned long)state.command_count,
			       (unsigned long)state.response_count,
			       (unsigned long)state.timeout_count,
			       (unsigned long)state.rx_error_count,
			       state.latest_command);
			next_rx_log_ms = k_uptime_get() + 1000;
		}

		k_sleep(K_MSEC(100));
	}
}

int sdi12_task_start(struct app_context *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	k_thread_create(&sdi12_task_data, sdi12_task_stack,
			K_THREAD_STACK_SIZEOF(sdi12_task_stack),
			sdi12_task_thread, ctx, NULL, NULL,
			CONFIG_BRIDGE_SDI12_TASK_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}
