/* SPDX-License-Identifier: Apache-2.0 */

#include "sdi12_response.h"

#include <string.h>

#include <zephyr/sys/printk.h>

#include "app_context.h"

#define SDI12_IDENT_VERSION "14"
#define SDI12_IDENT_VENDOR_LEN 8
#define SDI12_IDENT_MODEL_LEN 6
#define SDI12_IDENT_FW_LEN 3
#define SDI12_IDENT_SERIAL_MAX_LEN 13

static int append_fixed_field(char *response, size_t response_len, size_t *pos,
			      const char *field, size_t field_len)
{
	size_t input_len = strlen(field);

	for (size_t i = 0; i < field_len; i++) {
		if (*pos + 1 >= response_len) {
			return -ENOMEM;
		}

		response[*pos] = i < input_len ? field[i] : ' ';
		(*pos)++;
	}

	return 0;
}

static int append_limited_field(char *response, size_t response_len,
				size_t *pos, const char *field, size_t max_len)
{
	size_t input_len = strlen(field);
	size_t copy_len = MIN(input_len, max_len);

	if (*pos + copy_len >= response_len) {
		return -ENOMEM;
	}

	memcpy(&response[*pos], field, copy_len);
	*pos += copy_len;

	return 0;
}

static int append_scaled_value(char *response, size_t response_len, size_t *pos,
			       uint16_t raw, uint32_t divisor)
{
	if (divisor <= 1) {
		return snprintk(&response[*pos], response_len - *pos, "+%u",
				raw);
	}

	if (divisor == 10) {
		return snprintk(&response[*pos], response_len - *pos, "+%u.%01u",
				raw / 10, raw % 10);
	}

	if (divisor == 100) {
		return snprintk(&response[*pos], response_len - *pos, "+%u.%02u",
				raw / 100, raw % 100);
	}

	return snprintk(&response[*pos], response_len - *pos, "+%u",
			raw / divisor);
}

static uint32_t divisor_for_index(uint8_t index)
{
	if (IS_ENABLED(CONFIG_BRIDGE_RESPONSE_FORMAT_RAW)) {
		return 1;
	}
	if (IS_ENABLED(CONFIG_BRIDGE_RESPONSE_FORMAT_X10)) {
		return 10;
	}
	if (IS_ENABLED(CONFIG_BRIDGE_RESPONSE_FORMAT_X100)) {
		return 100;
	}

	if (index == 0) {
		return CONFIG_BRIDGE_VALUE0_SCALE_DIVISOR;
	}
	if (index == 1) {
		return CONFIG_BRIDGE_VALUE1_SCALE_DIVISOR;
	}

	return 1;
}

static int build_data_response(struct app_context *ctx, char *response,
			       size_t response_len)
{
	struct bridge_state_snapshot snapshot;
	size_t pos = 0;
	int ret;

	bridge_state_snapshot_get(&ctx->bridge, &snapshot);

	if (!snapshot.value_valid || snapshot.reg_count == 0) {
		return snprintk(response, response_len, "%s+9999\r\n",
				CONFIG_BRIDGE_SDI12_ADDRESS);
	}

	ret = snprintk(response, response_len, "%s",
		       CONFIG_BRIDGE_SDI12_ADDRESS);
	if (ret < 0) {
		return ret;
	}
	pos += (size_t)ret;

	for (uint8_t i = 0; i < snapshot.reg_count; i++) {
		ret = append_scaled_value(response, response_len, &pos,
					  snapshot.regs[i],
					  divisor_for_index(i));
		if (ret < 0) {
			return ret;
		}
		pos += (size_t)ret;
		if (pos >= response_len) {
			return -ENOMEM;
		}
	}

	ret = snprintk(&response[pos], response_len - pos, "\r\n");
	if (ret < 0) {
		return ret;
	}

	return (int)(pos + (size_t)ret);
}

static int build_identification_response(char *response, size_t response_len)
{
	size_t pos = 0;
	int ret;

	ret = snprintk(response, response_len, "%s%s",
		       CONFIG_BRIDGE_SDI12_ADDRESS, SDI12_IDENT_VERSION);
	if (ret < 0) {
		return ret;
	}
	pos += (size_t)ret;

	ret = append_fixed_field(response, response_len, &pos,
				 CONFIG_BRIDGE_SDI12_VENDOR,
				 SDI12_IDENT_VENDOR_LEN);
	if (ret != 0) {
		return ret;
	}

	ret = append_fixed_field(response, response_len, &pos,
				 CONFIG_BRIDGE_SDI12_MODEL,
				 SDI12_IDENT_MODEL_LEN);
	if (ret != 0) {
		return ret;
	}

	ret = append_fixed_field(response, response_len, &pos,
				 CONFIG_BRIDGE_SDI12_VERSION,
				 SDI12_IDENT_FW_LEN);
	if (ret != 0) {
		return ret;
	}

	ret = append_limited_field(response, response_len, &pos,
				   CONFIG_BRIDGE_SDI12_SERIAL,
				   SDI12_IDENT_SERIAL_MAX_LEN);
	if (ret != 0) {
		return ret;
	}

	ret = snprintk(&response[pos], response_len - pos, "\r\n");
	if (ret < 0) {
		return ret;
	}

	return (int)(pos + (size_t)ret);
}

static bool command_matches_address(const char *cmd)
{
	return cmd != NULL && cmd[0] == CONFIG_BRIDGE_SDI12_ADDRESS[0];
}

int sdi12_bridge_response_cb(struct sdi12_slave *sdi12, const char *cmd,
			     char *response, size_t response_len,
			     void *user_data)
{
	struct app_context *ctx = user_data;

	ARG_UNUSED(sdi12);

	if (ctx == NULL || cmd == NULL || response == NULL ||
	    response_len == 0) {
		return -EINVAL;
	}

	if (strcmp(cmd, "?!") == 0) {
		return snprintk(response, response_len, "%s\r\n",
				CONFIG_BRIDGE_SDI12_ADDRESS);
	}

	if (!command_matches_address(cmd)) {
		return -ENOTSUP;
	}

	if (strcmp(&cmd[1], "!") == 0) {
		return snprintk(response, response_len, "%s\r\n",
				CONFIG_BRIDGE_SDI12_ADDRESS);
	}

	if (strcmp(&cmd[1], "I!") == 0) {
		return build_identification_response(response, response_len);
	}

	if (strcmp(&cmd[1], "M!") == 0) {
		uint8_t value_count = CONFIG_BRIDGE_RS485_REG_COUNT;

		if (value_count == 0U) {
			value_count = 1U;
		}
		if (value_count > 9U) {
			value_count = 9U;
		}

		return snprintk(response, response_len, "%s001%u\r\n",
				CONFIG_BRIDGE_SDI12_ADDRESS, value_count);
	}

	if (strcmp(&cmd[1], "D0!") == 0 || strcmp(&cmd[1], "R0!") == 0) {
		return build_data_response(ctx, response, response_len);
	}

	return -ENOTSUP;
}
