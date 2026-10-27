/* SPDX-License-Identifier: Apache-2.0 */

#include "phase3_devices.h"

#include <errno.h>
#include <stdlib.h>
#include <zephyr/shell/shell.h>

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct phase3_status status;
	phase3_status_get(&status);
	shell_print(sh, "CAN=%s OLED=%s ENCODER=%s",
		    status.can_ready ? "ready" : "error",
		    status.oled_ready ? "ready" : "error",
		    status.encoder_ready ? "ready" : "error");
	shell_print(sh, "encoder position=%ld key=%u",
		    (long)status.encoder_position, status.encoder_pressed ? 1U : 0U);
	shell_print(sh, "CAN RX count=%lu last id=0x%03x dlc=%u",
		    (unsigned long)status.can_rx_count, status.last_rx.id,
		    status.last_rx.dlc);
	return 0;
}

static int cmd_can_send(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long id = strtoul(argv[1], &end, 0);
	if (*argv[1] == '\0' || *end != '\0' || id > CAN_STD_ID_MASK) {
		shell_error(sh, "standard CAN id must be 0..0x7ff");
		return -EINVAL;
	}
	uint8_t data[CAN_MAX_DLEN] = {0};
	size_t length = argc - 2U;
	for (size_t i = 0; i < length; i++) {
		unsigned long value = strtoul(argv[i + 2U], &end, 16);
		if (*argv[i + 2U] == '\0' || *end != '\0' || value > 0xff) {
			shell_error(sh, "data bytes must be hexadecimal 00..ff");
			return -EINVAL;
		}
		data[i] = (uint8_t)value;
	}
	int ret = phase3_can_send((uint32_t)id, data, length);
	if (ret != 0) {
		shell_error(sh, "CAN send failed: %d", ret);
		return ret;
	}
	shell_print(sh, "CAN sent id=0x%03lx dlc=%u", id, (unsigned int)length);
	return 0;
}

static int cmd_oled(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	int ret = phase3_oled_show("12CH EJECTION", argv[1], "OLED TEST", "I2C3 0x3C");
	if (ret != 0) {
		shell_error(sh, "OLED write failed: %d", ret);
		return ret;
	}
	shell_print(sh, "OLED updated");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(phase3_cmds,
	SHELL_CMD_ARG(status, NULL, "Show CAN/OLED/encoder state", cmd_status, 1, 0),
	SHELL_CMD_ARG(can_send, NULL, "Send: can_send <id> [00..ff]", cmd_can_send, 2, 8),
	SHELL_CMD_ARG(oled, NULL, "Show one word: oled <text>", cmd_oled, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(phase3, &phase3_cmds, "Phase 3 bring-up commands", NULL);
