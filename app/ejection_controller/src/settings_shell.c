/* SPDX-License-Identifier: Apache-2.0 */

#include "ejection_settings.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct ejection_settings_state state;
	ejection_settings_get(&state);
	shell_print(sh, "EEPROM=%s source=%s sequence=%lu error=%d",
		    state.ready ? "ready" : "error",
		    state.persistent ? "persistent" : "defaults/RAM",
		    (unsigned long)state.sequence, state.last_error);
	shell_print(sh, "can_bitrate=%lu pulse_ms=%u encoder_counts_per_step=%u",
		    (unsigned long)state.data.can_bitrate,
		    state.data.default_pulse_ms,
		    state.data.encoder_counts_per_step);
	shell_print(sh, "conveyor_delay_ms=%u ejection_hold_ms=%u (edit/save in IDLE)",
		    state.data.conveyor_delay_ms, state.data.ejection_hold_ms);
	return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	struct ejection_settings_state state;
	ejection_settings_get(&state);
	char *end;
	unsigned long value = strtoul(argv[2], &end, 0);
	if (*argv[2] == '\0' || *end != '\0' || argv[2][0] == '-' ||
	    value > 1000000UL) {
		return -EINVAL;
	}

	if (strcmp(argv[1], "can_bitrate") == 0) {
		state.data.can_bitrate = (uint32_t)value;
	} else if (strcmp(argv[1], "pulse_ms") == 0) {
		if (value > 1000U) { return -ERANGE; }
		state.data.default_pulse_ms = (uint16_t)value;
	} else if (strcmp(argv[1], "encoder_cps") == 0) {
		if (value > 16U) { return -ERANGE; }
		state.data.encoder_counts_per_step = (uint8_t)value;
	} else if (strcmp(argv[1], "conveyor_delay_ms") == 0) {
		if (value > 10000U) { return -ERANGE; }
		state.data.conveyor_delay_ms = (uint16_t)value;
	} else if (strcmp(argv[1], "ejection_hold_ms") == 0) {
		if (value > 1000U) { return -ERANGE; }
		state.data.ejection_hold_ms = (uint16_t)value;
	} else {
		shell_error(sh, "key: can_bitrate, pulse_ms, encoder_cps, conveyor_delay_ms, ejection_hold_ms");
		return -EINVAL;
	}

	int ret = ejection_settings_set(&state.data);
	if (ret != 0) {
		shell_error(sh, "settings error %d (edit in IDLE), value=%lu", ret, value);
		return ret;
	}
	shell_print(sh, "updated in RAM; run 'settings save' to persist");
	return 0;
}

static int cmd_save(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_settings_save();
	if (ret != 0) {
		shell_error(sh, "EEPROM save/verify failed: %d", ret);
		return ret;
	}
	shell_print(sh, "EEPROM save and readback verify ok");
	return 0;
}

static int cmd_defaults(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_settings_defaults();
	if (ret != 0) { shell_error(sh, "defaults: %d (IDLE only)", ret); return ret; }
	shell_print(sh, "defaults loaded in RAM; run 'settings save' to persist");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(settings_cmds,
	SHELL_CMD_ARG(status, NULL, "Show persistent settings", cmd_status, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Set <can_bitrate|pulse_ms|encoder_cps|conveyor_delay_ms|ejection_hold_ms> <value>",
		      cmd_set, 3, 0),
	SHELL_CMD_ARG(save, NULL, "Save and verify EEPROM", cmd_save, 1, 0),
	SHELL_CMD_ARG(defaults, NULL, "Load safe defaults into RAM", cmd_defaults, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(settings, &settings_cmds, "EEPROM settings commands", NULL);
