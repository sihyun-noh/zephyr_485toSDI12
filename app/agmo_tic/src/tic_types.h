/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AGMO_TIC_TYPES_H_
#define AGMO_TIC_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

enum tic_state {
	TIC_STATE_INIT,
	TIC_STATE_SAFE_OFF,
	TIC_STATE_KEY_DETECTED,
	TIC_STATE_AUTO_ARMING,
	TIC_STATE_AUTO_READY,
	TIC_STATE_AUTO_CRANKING,
	TIC_STATE_AUTO_COMPLETE,
	TIC_STATE_ENGINE_STOPPING,
	TIC_STATE_FAULT,
};

enum tic_event_type {
	TIC_EVENT_BOOT_COMPLETE,
	TIC_EVENT_KEY_ON,
	TIC_EVENT_KEY_OFF,
	TIC_EVENT_MANUAL_CRANK_ON,
	TIC_EVENT_MANUAL_CRANK_OFF,
	TIC_EVENT_AUTO_ENABLE,
	TIC_EVENT_AUTO_DISABLE,
	TIC_EVENT_START_REQUEST,
	TIC_EVENT_STOP_REQUEST,
	TIC_EVENT_ALL_OFF,
	TIC_EVENT_AUTO_ARM_DELAY_DONE,
	TIC_EVENT_CRANK_TIMEOUT,
	TIC_EVENT_STOP_TIMEOUT,
	TIC_EVENT_KEY_RECOVERY_TIMEOUT,
	TIC_EVENT_CAN_VALID,
	TIC_EVENT_CAN_TIMEOUT,
	TIC_EVENT_CAN_BUS_OFF,
	TIC_EVENT_INTERNAL_FAULT,
};

enum tic_control_source {
	TIC_CONTROL_NONE,
	TIC_CONTROL_CAN,
	TIC_CONTROL_SHELL,
};

enum tic_fault_code {
	TIC_FAULT_NONE,
	TIC_FAULT_CAN_TIMEOUT,
	TIC_FAULT_CAN_BUS_OFF,
	TIC_FAULT_OUTPUT,
	TIC_FAULT_INTERNAL,
};

enum tic_inhibit_reason {
	TIC_INHIBIT_NONE,
	TIC_INHIBIT_KEY_OFF,
	TIC_INHIBIT_MANUAL_CRANK_ACTIVE,
	TIC_INHIBIT_AUTO_NOT_READY,
	TIC_INHIBIT_CRANK_TIME,
	TIC_INHIBIT_START_NOT_REARMED,
	TIC_INHIBIT_COMMAND_INVALID,
	TIC_INHIBIT_STOP_NOT_ALLOWED,
};

struct tic_event {
	enum tic_event_type type;
	uint16_t crank_time_ms;
	uint8_t command_code;
	enum tic_control_source source;
};

struct tic_fsm_snapshot {
	enum tic_state state;
	enum tic_fault_code fault;
	enum tic_inhibit_reason inhibit;
	bool key_on;
	bool manual_crank;
	bool auto_enabled;
	enum tic_control_source auto_control_source;
	bool start_accepted;
	bool start_complete;
	bool start_rejected;
	bool stop_accepted;
	bool stop_complete;
	bool stop_rejected;
	bool command_valid;
	bool can_timeout;
	uint8_t last_command_code;
	uint16_t crank_remaining_ms;
};

struct tic_input_snapshot {
	bool di1_key_on;
	bool di2_manual_crank;
};

struct tic_output_snapshot {
	bool r1_key_on;
	bool r2_restart;
	bool r3_manual_lockout;
	bool r4_engine_stop;
	bool r5_aux;
	bool led_restart;
	bool led_acc;
	bool led_manual;
	bool led_key;
	bool led_auto;
};

#endif /* AGMO_TIC_TYPES_H_ */
