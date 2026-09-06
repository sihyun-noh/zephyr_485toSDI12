/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_APP_CONTEXT_H_
#define DAJILOG_485_TO_SDI12_APP_CONTEXT_H_

#include <zephyr/kernel.h>

#include <rs485_modbus/rs485_modbus.h>
#include <sdi12_slave/sdi12_slave.h>
#include <display_oled/display_oled.h>

#include "app_fsm.h"
#include "app_msg.h"
#include "bridge_state.h"

struct app_context {
	struct app_fsm fsm;
	struct bridge_state bridge;

	struct k_msgq app_msgq;
	char app_msgq_buffer[CONFIG_BRIDGE_APP_MSGQ_DEPTH *
			     sizeof(struct app_msg)] __aligned(4);

	struct rs485_modbus rs485;
	struct sdi12_slave sdi12;
	struct oled_display oled;
};

extern struct app_context app;

int app_context_init(struct app_context *ctx);
int app_components_init(struct app_context *ctx);
int app_tasks_start(struct app_context *ctx);
void app_print_profile(void);

#endif /* DAJILOG_485_TO_SDI12_APP_CONTEXT_H_ */
