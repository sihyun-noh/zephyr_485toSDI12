/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <zephyr/shell/shell.h>

#include "app_context.h"

static int cmd_bridge_values(const struct shell *shell, size_t argc, char **argv)
{
	struct bridge_state_snapshot snapshot;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	bridge_state_snapshot_get(&app.bridge, &snapshot);
	shell_print(shell,
		    "valid=%u regs=%u updates=%u polls=%u timeouts=%u last_error=%d age_ms=%lld",
		    snapshot.value_valid, snapshot.reg_count, snapshot.update_count,
		    snapshot.poll_count, snapshot.timeout_count, snapshot.last_error,
		    (long long)(k_uptime_get() - snapshot.last_update_ms));

	if (!snapshot.value_valid || snapshot.reg_count == 0U) {
		shell_warn(shell, "No valid RS485 sensor response yet");
		return -EAGAIN;
	}

	for (uint8_t i = 0; i < snapshot.reg_count; i++) {
		shell_print(shell, "reg[%u]=%u", i, snapshot.regs[i]);
	}

	if (snapshot.reg_count >= 2U) {
		shell_print(shell, "temperature=%u.%01u C humidity=%u.%01u %%RH",
			    snapshot.regs[1] / 10U, snapshot.regs[1] % 10U,
			    snapshot.regs[0] / 10U, snapshot.regs[0] % 10U);
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bridge_cmds,
	SHELL_CMD(values, NULL, "Show latest RS485 sensor registers and scaled values",
		  cmd_bridge_values),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bridge, &bridge_cmds, "485-to-SDI12 bridge diagnostics", NULL);
