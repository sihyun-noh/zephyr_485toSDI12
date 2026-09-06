/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <stdlib.h>
#include <string.h>

#include "tic_can.h"
#include "tic_fsm.h"
#include "tic_input.h"
#include "tic_output.h"

static int post_event(const struct shell *sh, enum tic_event_type type,
		      uint16_t crank_time_ms)
{
	const struct tic_event event = {
		.type = type,
		.crank_time_ms = crank_time_ms,
		.command_code = 0,
		.source = TIC_CONTROL_SHELL,
	};
	int ret = tic_fsm_post(&event, K_MSEC(20));

	if (ret != 0) {
		shell_error(sh, "FSM queue error: %d", ret);
	}
	return ret;
}

static int parse_on_off(const char *text, bool *on)
{
	if (strcmp(text, "on") == 0) {
		*on = true;
		return 0;
	}
	if (strcmp(text, "off") == 0) {
		*on = false;
		return 0;
	}
	return -EINVAL;
}

static int cmd_tic(const struct shell *sh, size_t argc, char **argv)
{
	struct tic_fsm_snapshot fsm;
	struct tic_input_snapshot input;
	struct tic_output_snapshot output;
	bool on;

	if (strcmp(argv[1], "status") == 0 && argc == 2) {
		tic_fsm_get_snapshot(&fsm);
		tic_input_get_snapshot(&input);
		tic_output_get_snapshot(&output);
		shell_print(sh, "state=%s auto=%u source=%s valid=%u timeout=%u fault=%u inhibit=%u",
			    tic_fsm_state_name(fsm.state), fsm.auto_enabled,
			    fsm.auto_control_source == TIC_CONTROL_CAN ? "can" :
			    fsm.auto_control_source == TIC_CONTROL_SHELL ? "shell" : "none",
			    fsm.command_valid, fsm.can_timeout, fsm.fault,
			    fsm.inhibit);
		shell_print(sh, "start: accepted=%u cranking=%u complete=%u rejected=%u",
			    fsm.start_accepted,
			    fsm.state == TIC_STATE_AUTO_CRANKING,
			    fsm.start_complete, fsm.start_rejected);
		shell_print(sh, "stop: accepted=%u active=%u complete=%u rejected=%u",
			    fsm.stop_accepted,
			    fsm.state == TIC_STATE_ENGINE_STOPPING,
			    fsm.stop_complete, fsm.stop_rejected);
		shell_print(sh, "input: key=%u manual_crank=%u di3=%u",
			    input.di1_key_on, input.di2_manual_crank, input.di3);
		shell_print(sh, "relay: R1_key=%u R2_crank=%u R3_lockout=%u R4_stop=%u remaining=%u ms",
			    output.r1_key_on, output.r2_restart,
			    output.r3_manual_lockout, output.r4_engine_stop,
			    fsm.crank_remaining_ms);
		return 0;
	}

	if (strcmp(argv[1], "all-off") == 0 && argc == 2) {
		return post_event(sh, TIC_EVENT_ALL_OFF, 0);
	}

	if (strcmp(argv[1], "stop") == 0 && argc == 2) {
		return post_event(sh, TIC_EVENT_STOP_REQUEST, 0);
	}

	if (strcmp(argv[1], "auto") == 0 && argc == 3 &&
	    parse_on_off(argv[2], &on) == 0) {
		return post_event(sh, on ? TIC_EVENT_AUTO_ENABLE :
					 TIC_EVENT_AUTO_DISABLE, 0);
	}

	if (strcmp(argv[1], "start") == 0 && argc == 3) {
		char *end;
		unsigned long value = strtoul(argv[2], &end, 10);

		if (*argv[2] == '\0' || *end != '\0' || value < 1000UL ||
		    value > 3000UL) {
			shell_error(sh, "start time must be 1000..3000 ms");
			return -EINVAL;
		}
		return post_event(sh, TIC_EVENT_START_REQUEST, (uint16_t)value);
	}

	shell_error(sh, "usage: tic <status|stop|all-off|auto on|auto off|start 1000..3000>");
	return -EINVAL;
}

static int cmd_input(const struct shell *sh, size_t argc, char **argv)
{
	struct tic_input_snapshot input;

	ARG_UNUSED(argc);
	if (strcmp(argv[1], "status") != 0) {
		shell_error(sh, "usage: input status");
		return -EINVAL;
	}
	tic_input_get_snapshot(&input);
	shell_print(sh, "DI1 key=%u, DI2 manual_crank=%u, DI3=%u",
		    input.di1_key_on, input.di2_manual_crank, input.di3);
	return 0;
}

static int cmd_can(const struct shell *sh, size_t argc, char **argv)
{
	struct tic_can_snapshot can;

	ARG_UNUSED(argc);
	if (strcmp(argv[1], "status") != 0) {
		shell_error(sh, "usage: can status");
		return -EINVAL;
	}
	tic_can_get_snapshot(&can);
	shell_print(sh, "ready=%u state=%s command=%u timeout=%u last_error=%d",
		    can.ready, tic_can_state_name(can.bus_state),
		    can.command_received, can.timeout, can.last_error);
	shell_print(sh, "rx=%u drop=%u invalid=%u tx=%u tx_error=%u TEC=%u REC=%u",
		    can.rx_count, can.rx_drop_count, can.rx_invalid_count,
		    can.tx_count, can.tx_error_count, can.error_count.tx_err_cnt,
		    can.error_count.rx_err_cnt);
	return 0;
}

static int cmd_led(const struct shell *sh, size_t argc, char **argv)
{
	static const char *const names[] = { "restart", "acc", "manual", "key" };
	struct tic_input_snapshot input;
	struct tic_output_snapshot output;
	bool on;
	int ret;

	if (strcmp(argv[1], "status") == 0 && argc == 2) {
		tic_output_get_snapshot(&output);
		shell_print(sh, "mode=%s restart=%u acc=%u manual=%u key=%u",
			    output.led_auto ? "auto" : "manual",
			    output.led_restart, output.led_acc,
			    output.led_manual, output.led_key);
		return 0;
	}
	if (strcmp(argv[1], "auto") == 0 && argc == 2) {
		tic_input_get_snapshot(&input);
		tic_output_led_auto_enable(true, input.di1_key_on);
		return 0;
	}
	if (argc != 3 || parse_on_off(argv[2], &on) != 0) {
		shell_error(sh, "usage: led <status|auto|restart|acc|manual|key|all> [on|off]");
		return -EINVAL;
	}
	if (strcmp(argv[1], "all") == 0) {
		return tic_output_led_set_all(on);
	}
	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		if (strcmp(argv[1], names[i]) == 0) {
			ret = tic_output_led_set((enum tic_led_id)i, on);
			if (ret != 0) {
				shell_error(sh, "LED error: %d", ret);
			}
			return ret;
		}
	}
	shell_error(sh, "unknown LED");
	return -EINVAL;
}

SHELL_CMD_ARG_REGISTER(tic, NULL,
	"TIC control: tic <status|stop|all-off|auto on|auto off|start 1000..3000>",
	cmd_tic, 2, 1);
SHELL_CMD_ARG_REGISTER(input, NULL, "Input diagnostics: input status",
	cmd_input, 2, 0);
SHELL_CMD_ARG_REGISTER(can, NULL, "CAN diagnostics: can status",
	cmd_can, 2, 0);
SHELL_CMD_ARG_REGISTER(led, NULL,
	"LED control: led <status|auto|restart|acc|manual|key|all> [on|off]",
	cmd_led, 2, 1);
