/* SPDX-License-Identifier: Apache-2.0 */

#include "ejection_io.h"
#include "ejection_settings.h"
#include "ejection_scheduler.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/shell/shell.h>

static int parse_channel(const struct shell *sh, const char *text,
			 size_t limit, size_t *channel)
{
	char *end;
	unsigned long value = strtoul(text, &end, 10);

	if (*text == '\0' || *end != '\0' || value == 0 || value > limit) {
		shell_error(sh, "channel must be 1..%u", (unsigned int)limit);
		return -EINVAL;
	}
	*channel = (size_t)value - 1U;
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "outputs (1=active):");
	for (size_t i = 0; i < EJECTION_OUTPUT_COUNT; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%u%s",
			      ejection_output_get(i) ? 1U : 0U,
			      i + 1U == EJECTION_OUTPUT_COUNT ? "\n" : " ");
	}
	shell_print(sh, "inputs (1=active, debounced):");
	for (size_t i = 0; i < EJECTION_INPUT_COUNT; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%u%s",
			      ejection_input_get(i) ? 1U : 0U,
			      i + 1U == EJECTION_INPUT_COUNT ? "\n" : " ");
	}
	return 0;
}

static int cmd_off(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_scheduler_cancel_all();

	if (ret != 0) {
		shell_error(sh, "all-off failed: %d", ret);
		return ret;
	}
	shell_print(sh, "all outputs off");
	return 0;
}

static int cmd_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	size_t channel;
	int ret = parse_channel(sh, argv[1], EJECTION_OUTPUT_COUNT, &channel);

	if (ret != 0) {
		return ret;
	}
	bool active;
	if (strcmp(argv[2], "on") == 0) {
		active = true;
	} else if (strcmp(argv[2], "off") == 0) {
		active = false;
	} else {
		shell_error(sh, "state must be on or off");
		return -EINVAL;
	}

	ret = ejection_scheduler_manual(channel, active, 0);
	if (ret != 0) {
		shell_error(sh, "output command failed: %d", ret);
		return ret;
	}
	shell_print(sh, "output %u %s", (unsigned int)channel + 1U,
		    active ? "on" : "off");
	return 0;
}

static int cmd_pulse(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	size_t channel;
	int ret = parse_channel(sh, argv[1], EJECTION_OUTPUT_COUNT, &channel);

	if (ret != 0) {
		return ret;
	}

	struct ejection_settings_state settings;
	ejection_settings_get(&settings);
	char *end = NULL;
	unsigned long duration = settings.data.default_pulse_ms;
	if (argc >= 3U) {
		duration = strtoul(argv[2], &end, 10);
	}
	if ((argc >= 3U && (*argv[2] == '\0' || *end != '\0')) ||
	    duration < EJECTION_PULSE_MIN_MS ||
	    duration > EJECTION_PULSE_MAX_MS) {
		shell_error(sh, "duration must be %u..%u ms",
			    EJECTION_PULSE_MIN_MS, EJECTION_PULSE_MAX_MS);
		return -ERANGE;
	}

	ret = ejection_scheduler_manual(channel, true, (uint32_t)duration);
	if (ret != 0) {
		shell_error(sh, "pulse failed: %d", ret);
		return ret;
	}
	shell_print(sh, "output %u pulse %lu ms", (unsigned int)channel + 1U,
		    duration);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_cmds,
	SHELL_CMD_ARG(status, NULL, "Show all output and debounced input states",
		      cmd_status, 1, 0),
	SHELL_CMD_ARG(all_off, NULL, "Turn every output off", cmd_off, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Set output: set <1-12> <on|off>", cmd_set, 3, 0),
	SHELL_CMD_ARG(pulse, NULL, "Pulse output: pulse <1-12> [5-1000 ms]",
		      cmd_pulse, 2, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ejection, &ejection_cmds,
		   "12-channel ejection controller commands", NULL);
