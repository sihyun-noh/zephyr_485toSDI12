/* SPDX-License-Identifier: Apache-2.0 */

#include "controller_state.h"
#include "phase3_devices.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static void refresh_ui(void)
{
	phase3_ui_refresh();
}

static int print_transition_result(const struct shell *sh, int ret)
{
	if (ret != 0) {
		shell_error(sh, "state transition rejected: %d", ret);
		return ret;
	}

	struct controller_state_snapshot snapshot;
	controller_state_get(&snapshot);
	refresh_ui();
	shell_print(sh, "controller state=%s sequence=%lu",
		    controller_state_name(snapshot.state),
		    (unsigned long)snapshot.sequence);
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct controller_state_snapshot snapshot;
	controller_state_get(&snapshot);
	shell_print(sh, "state=%s previous=%s fault=%s sequence=%lu",
		    controller_state_name(snapshot.state),
		    controller_state_name(snapshot.previous_state),
		    controller_fault_name(snapshot.fault),
		    (unsigned long)snapshot.sequence);
	shell_print(sh, "entered_at=%lld ms state_age=%lld ms",
		    snapshot.entered_at_ms,
		    k_uptime_get() - snapshot.entered_at_ms);
	shell_print(sh, "required: IO=%u SETTINGS=%u CAN=%u ENCODER=%u; OLED=%u",
		    snapshot.prerequisites.io_ready ? 1U : 0U,
		    snapshot.prerequisites.settings_ready ? 1U : 0U,
		    snapshot.prerequisites.can_ready ? 1U : 0U,
		    snapshot.prerequisites.encoder_ready ? 1U : 0U,
		    snapshot.prerequisites.oled_ready ? 1U : 0U);
	return 0;
}

static int cmd_ready(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_READY));
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_RUNNING));
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_IDLE));
}

static int cmd_fault(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_enter_fault(CONTROLLER_FAULT_EMERGENCY_STOP));
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(sh, controller_state_clear_fault());
}

SHELL_STATIC_SUBCMD_SET_CREATE(controller_cmds,
	SHELL_CMD_ARG(status, NULL, "Show controller state and prerequisites",
		      cmd_status, 1, 0),
	SHELL_CMD_ARG(ready, NULL, "Transition IDLE to READY", cmd_ready, 1, 0),
	SHELL_CMD_ARG(start, NULL, "Transition READY to RUNNING", cmd_start, 1, 0),
	SHELL_CMD_ARG(stop, NULL, "Transition READY/RUNNING to IDLE", cmd_stop, 1, 0),
	SHELL_CMD_ARG(fault, NULL, "Inject a latched emergency-stop fault",
		      cmd_fault, 1, 0),
	SHELL_CMD_ARG(clear, NULL, "Clear FAULT and return to IDLE", cmd_clear, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(controller, &controller_cmds,
		   "Controller state-machine commands", NULL);
