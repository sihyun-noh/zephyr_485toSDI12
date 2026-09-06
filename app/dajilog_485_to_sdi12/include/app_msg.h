/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_APP_MSG_H_
#define DAJILOG_485_TO_SDI12_APP_MSG_H_

#include <stdint.h>

#include <zephyr/kernel.h>

#define APP_MSG_SDI12_COMMAND_MAX_LEN 24
#define APP_MSG_RS485_MAX_REGS 8

enum app_msg_type {
	APP_MSG_BOOT_DONE,
	APP_MSG_HW_READY,
	APP_MSG_RS485_POLL_OK,
	APP_MSG_RS485_POLL_TIMEOUT,
	APP_MSG_RS485_VALUE_UPDATE,
	APP_MSG_SDI12_COMMAND_RX,
	APP_MSG_SDI12_RESPONSE_DONE,
	APP_MSG_ERROR,
};

struct app_msg {
	enum app_msg_type type;
	int64_t timestamp_ms;

	union {
		struct {
			uint16_t regs[APP_MSG_RS485_MAX_REGS];
			uint8_t reg_count;
			int error;
		} rs485;

		struct {
			char command[APP_MSG_SDI12_COMMAND_MAX_LEN];
		} sdi12;

		struct {
			int code;
		} error;
	} data;
};

const char *app_msg_type_name(enum app_msg_type type);
int app_msg_publish(struct k_msgq *msgq, const struct app_msg *msg);

#endif /* DAJILOG_485_TO_SDI12_APP_MSG_H_ */
