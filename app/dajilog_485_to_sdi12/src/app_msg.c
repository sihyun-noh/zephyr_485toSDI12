/* SPDX-License-Identifier: Apache-2.0 */

#include "app_msg.h"

const char *app_msg_type_name(enum app_msg_type type)
{
	switch (type) {
	case APP_MSG_BOOT_DONE:
		return "BOOT_DONE";
	case APP_MSG_HW_READY:
		return "HW_READY";
	case APP_MSG_RS485_POLL_OK:
		return "RS485_POLL_OK";
	case APP_MSG_RS485_POLL_TIMEOUT:
		return "RS485_POLL_TIMEOUT";
	case APP_MSG_RS485_VALUE_UPDATE:
		return "RS485_VALUE_UPDATE";
	case APP_MSG_SDI12_COMMAND_RX:
		return "SDI12_COMMAND_RX";
	case APP_MSG_SDI12_RESPONSE_DONE:
		return "SDI12_RESPONSE_DONE";
	case APP_MSG_ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}

int app_msg_publish(struct k_msgq *msgq, const struct app_msg *msg)
{
	if (msgq == NULL || msg == NULL) {
		return -EINVAL;
	}

	return k_msgq_put(msgq, msg, K_NO_WAIT);
}
