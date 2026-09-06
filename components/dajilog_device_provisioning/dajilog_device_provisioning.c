/* SPDX-License-Identifier: Apache-2.0 */

#include "dajilog_device_provisioning.h"

#include <errno.h>
#include <stdlib.h>

#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>

static struct dajilog_device_provisioning_config provisioning;

static int get_profile(struct dajilog_device_profile_state *profile)
{
	if (provisioning.device_info == NULL || provisioning.device_info->profile == NULL) {
		return -ENODEV;
	}

	dajilog_device_profile_get(provisioning.device_info->profile, profile);
	return 0;
}

static void get_runtime(struct dajilog_device_runtime *runtime)
{
	*runtime = (struct dajilog_device_runtime){
		.health = DAJILOG_DEVICE_HEALTH_UNASSIGNED,
		.last_error = -ENODEV,
	};

	if (provisioning.runtime_get != NULL) {
		provisioning.runtime_get(provisioning.user_data, runtime);
	}
}

static int cmd_device_info(const struct shell *shell, size_t argc, char **argv)
{
	struct dajilog_device_profile_state profile;
	struct dajilog_device_runtime runtime;
	const struct dajilog_device_identity *identity;
	uint8_t uid[DAJILOG_DEVICE_INFO_UID_SIZE];
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = get_profile(&profile);
	if (ret != 0) {
		shell_error(shell, "Device profile is unavailable (%d)", ret);
		return ret;
	}

	identity = &provisioning.device_info->identity;
	get_runtime(&runtime);
	shell_print(shell, "type=0x%02x node=%u serial=%u hw=%u", identity->device_type,
		    profile.data.node_id, (unsigned int)profile.data.serial_number,
		    identity->hardware_revision);
	shell_print(shell, "fw=%u.%u.%u build=%u app_proto=%u capability=0x%04x",
		    identity->firmware_major, identity->firmware_minor,
		    identity->firmware_patch, identity->firmware_build,
		    identity->app_protocol_version, identity->capability);
	shell_print(shell, "health=%u last_error=%d profile=%s",
		    runtime.health, runtime.last_error,
		    profile.persistent ? "persistent" :
		    (profile.development ? "development" : "invalid"));
	ret = dajilog_device_info_get_chip_uid(uid);
	if (ret == 0) {
		shell_print(shell,
			    "chip_uid=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
			    uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7],
			    uid[8], uid[9], uid[10], uid[11]);
	} else {
		shell_error(shell, "chip UID unavailable (%d)", ret);
	}
	return 0;
}

static int cmd_profile_get(const struct shell *shell, size_t argc, char **argv)
{
	struct dajilog_device_profile_state profile;
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ret = get_profile(&profile);
	if (ret != 0) {
		shell_error(shell, "Device profile is unavailable (%d)", ret);
		return ret;
	}

	shell_print(shell, "valid=%u persistent=%u development=%u last_error=%d",
		    profile.valid, profile.persistent, profile.development, profile.last_error);
	shell_print(shell, "node=%u serial=%u hw=%u sequence=%u",
		    profile.data.node_id, (unsigned int)profile.data.serial_number,
		    profile.data.hardware_revision, (unsigned int)profile.data.sequence);
	return 0;
}

static int parse_node_id(const char *text, uint8_t *node_id)
{
	char *end;
	unsigned long value;

	if (text == NULL || node_id == NULL) {
		return -EINVAL;
	}

	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno != 0 || end == text || *end != '\0' || value < 1U || value > 127U) {
		return -EINVAL;
	}

	*node_id = (uint8_t)value;
	return 0;
}

static int cmd_node_id_set(const struct shell *shell, size_t argc, char **argv)
{
	struct dajilog_device_profile_state current;
	struct dajilog_device_profile_data updated;
	uint8_t node_id;
	int ret;

	if (argc != 2U) {
		return -EINVAL;
	}

	ret = parse_node_id(argv[1], &node_id);
	if (ret != 0) {
		shell_error(shell, "node ID must be an integer from 1 to 127");
		return ret;
	}

	ret = get_profile(&current);
	if (ret != 0 || !current.valid) {
		shell_error(shell, "No active profile is available (%d)", ret);
		return ret != 0 ? ret : -ENOENT;
	}

	updated = current.data;
	updated.node_id = node_id;
	ret = dajilog_device_profile_save(provisioning.device_info->profile, &updated);
	if (ret != 0) {
		shell_error(shell, "Profile save/verify failed (%d); existing profile retained", ret);
		return ret;
	}

	shell_print(shell, "node ID %u saved to persistent profile; run 'device reboot'",
		    node_id);
	return 0;
}

static int cmd_device_reboot(const struct shell *shell, size_t argc, char **argv)
{
	ARG_UNUSED(shell);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(device_profile_cmds,
	SHELL_CMD(get, NULL, "Show active Device Profile", cmd_profile_get),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(device_node_cmds,
	SHELL_CMD_ARG(set, NULL, "Save node ID: device node set <1..127>",
		      cmd_node_id_set, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(device_cmds,
	SHELL_CMD(info, NULL, "Show identity, capability, and runtime health", cmd_device_info),
	SHELL_CMD(profile, &device_profile_cmds, "Inspect Device Profile", NULL),
	SHELL_CMD(node, &device_node_cmds, "Manage CAN node ID", NULL),
	SHELL_CMD(reboot, NULL, "Cold reboot the device", cmd_device_reboot),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(device, &device_cmds, "Dajilog device provisioning", NULL);

int dajilog_device_provisioning_init(
	const struct dajilog_device_provisioning_config *config)
{
	if (config == NULL || config->device_info == NULL ||
	    config->device_info->profile == NULL) {
		return -EINVAL;
	}

	provisioning = *config;
	return 0;
}
