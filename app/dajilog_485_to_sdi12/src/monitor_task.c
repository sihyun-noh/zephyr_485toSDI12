/* SPDX-License-Identifier: Apache-2.0 */

#include "monitor_task.h"

#include <zephyr/sys/printk.h>

static K_THREAD_STACK_DEFINE(monitor_task_stack,
			     CONFIG_BRIDGE_MONITOR_THREAD_STACK_SIZE);
static struct k_thread monitor_task_data;

static void print_bridge_state(struct app_context *ctx)
{
	struct bridge_state_snapshot snapshot;

	bridge_state_snapshot_get(&ctx->bridge, &snapshot);

	printk("MON state=%s valid=%d polls=%lu updates=%lu timeouts=%lu last_error=%d",
	       app_state_name(ctx->fsm.state), snapshot.value_valid,
	       (unsigned long)snapshot.poll_count,
	       (unsigned long)snapshot.update_count,
	       (unsigned long)snapshot.timeout_count, snapshot.last_error);

	if (snapshot.value_valid) {
		printk(" regs:");
		for (uint8_t i = 0; i < snapshot.reg_count; i++) {
			printk(" r%u=%u", i, snapshot.regs[i]);
		}
	}
	printk("\r\n");
}

static void monitor_task_thread(void *arg1, void *arg2, void *arg3)
{
	struct app_context *ctx = arg1;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	if (CONFIG_BRIDGE_MONITOR_PERIOD_MS == 0) {
		return;
	}

	while (1) {
		print_bridge_state(ctx);
		k_sleep(K_MSEC(CONFIG_BRIDGE_MONITOR_PERIOD_MS));
	}
}

int monitor_task_start(struct app_context *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	k_thread_create(&monitor_task_data, monitor_task_stack,
			K_THREAD_STACK_SIZEOF(monitor_task_stack),
			monitor_task_thread, ctx, NULL, NULL,
			CONFIG_BRIDGE_MONITOR_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}
