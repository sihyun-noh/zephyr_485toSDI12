/* SPDX-License-Identifier: Apache-2.0 */

#include "app_fsm.h"

#include <zephyr/sys/printk.h>

static void app_fsm_transition(struct app_fsm *fsm, enum app_state next)
{
	if (fsm->state == next) {
		return;
	}

	printk("FSM %s -> %s\r\n", app_state_name(fsm->state),
	       app_state_name(next));
	fsm->previous_state = fsm->state;
	fsm->state = next;
	fsm->entered_at_ms = k_uptime_get();
}

const char *app_state_name(enum app_state state)
{
	switch (state) {
	case APP_STATE_BOOT:
		return "BOOT";
	case APP_STATE_INIT_HW:
		return "INIT_HW";
	case APP_STATE_WAIT_FIRST_SAMPLE:
		return "WAIT_FIRST_SAMPLE";
	case APP_STATE_READY:
		return "READY";
	case APP_STATE_ACTIVE_SESSION:
		return "ACTIVE_SESSION";
	case APP_STATE_SENSOR_ERROR:
		return "SENSOR_ERROR";
	case APP_STATE_FATAL_ERROR:
		return "FATAL_ERROR";
	default:
		return "UNKNOWN";
	}
}

void app_fsm_init(struct app_fsm *fsm)
{
	if (fsm == NULL) {
		return;
	}

	fsm->state = APP_STATE_BOOT;
	fsm->previous_state = APP_STATE_BOOT;
	fsm->entered_at_ms = k_uptime_get();
	fsm->event_count = 0;
	fsm->last_error = 0;
}

void app_fsm_handle_msg(struct app_fsm *fsm, const struct app_msg *msg)
{
	if (fsm == NULL || msg == NULL) {
		return;
	}

	fsm->event_count++;

	switch (msg->type) {
	case APP_MSG_BOOT_DONE:
		app_fsm_transition(fsm, APP_STATE_INIT_HW);
		break;
	case APP_MSG_HW_READY:
		app_fsm_transition(fsm, APP_STATE_WAIT_FIRST_SAMPLE);
		break;
	case APP_MSG_RS485_VALUE_UPDATE:
	case APP_MSG_RS485_POLL_OK:
		app_fsm_transition(fsm, APP_STATE_READY);
		break;
	case APP_MSG_RS485_POLL_TIMEOUT:
		fsm->last_error = msg->data.rs485.error;
		app_fsm_transition(fsm, APP_STATE_SENSOR_ERROR);
		break;
	case APP_MSG_SDI12_COMMAND_RX:
		app_fsm_transition(fsm, APP_STATE_ACTIVE_SESSION);
		break;
	case APP_MSG_SDI12_RESPONSE_DONE:
		app_fsm_transition(fsm, APP_STATE_READY);
		break;
	case APP_MSG_ERROR:
		fsm->last_error = msg->data.error.code;
		app_fsm_transition(fsm, APP_STATE_FATAL_ERROR);
		break;
	default:
		break;
	}
}
