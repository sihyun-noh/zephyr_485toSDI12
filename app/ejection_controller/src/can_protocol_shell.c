/* SPDX-License-Identifier: Apache-2.0 */

#include "can_protocol.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct can_protocol_status status;
	can_protocol_get_status(&status);
	int64_t age = status.last_heartbeat_ms == 0 ? -1 :
		      k_uptime_get() - status.last_heartbeat_ms;

	shell_print(sh, "init=%u link=%u heartbeat_required=%u status_tx=%u heartbeat_seq=%u age_ms=%lld",
		    status.initialized ? 1U : 0U, status.link_active ? 1U : 0U,
		    status.heartbeat_required ? 1U : 0U,
		    status.status_publishing ? 1U : 0U,
		    status.last_heartbeat_sequence, age);
	shell_print(sh, "rx=%lu cmd=%lu hb=%lu echo=%lu status=%lu",
		    (unsigned long)status.rx_count,
		    (unsigned long)status.command_count,
		    (unsigned long)status.heartbeat_count,
		    (unsigned long)status.echo_count,
		    (unsigned long)status.status_count);
	shell_print(sh, "reject=%lu duplicate=%lu dropped=%lu tx_error=%lu",
		    (unsigned long)status.reject_count,
		    (unsigned long)status.duplicate_count,
		    (unsigned long)status.dropped_count,
		    (unsigned long)status.tx_error_count);
	return 0;
}

static int cmd_heartbeat(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	bool required;

	if (strcmp(argv[1], "on") == 0) {
		required = true;
	} else if (strcmp(argv[1], "off") == 0) {
		required = false;
	} else {
		shell_error(sh, "usage: canproto heartbeat <on|off>");
		return -EINVAL;
	}
	can_protocol_set_heartbeat_required(required);
	shell_print(sh, "heartbeat gate %s", required ? "enabled" : "disabled");
	return 0;
}

static int cmd_status_send(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = can_protocol_send_status();

	if (ret != 0) {
		shell_error(sh, "status send failed: %d", ret);
		return ret;
	}
	shell_print(sh, "status 0x%03x sent", CAN_PROTOCOL_STATUS_ID);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(canproto_cmds,
	SHELL_CMD_ARG(status, NULL, "Show protocol counters and heartbeat state",
		      cmd_status, 1, 0),
	SHELL_CMD_ARG(status_send, NULL, "Send one 0x201 status frame",
		      cmd_status_send, 1, 0),
	SHELL_CMD_ARG(heartbeat, NULL, "Set optional heartbeat gate: <on|off>",
		      cmd_heartbeat, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(canproto, &canproto_cmds, "CAN protocol commands", NULL);
