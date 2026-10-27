/* SPDX-License-Identifier: Apache-2.0 */

#ifndef CONTROLLER_STATE_H_
#define CONTROLLER_STATE_H_

#include <stdbool.h>
#include <stdint.h>

enum controller_state {
	CONTROLLER_STATE_BOOT,
	CONTROLLER_STATE_SELF_TEST,
	CONTROLLER_STATE_IDLE,
	CONTROLLER_STATE_READY,
	CONTROLLER_STATE_RUNNING,
	CONTROLLER_STATE_FAULT,
};

enum controller_fault {
	CONTROLLER_FAULT_NONE,
	CONTROLLER_FAULT_IO_INIT,
	CONTROLLER_FAULT_CAN_INIT,
	CONTROLLER_FAULT_OLED_INIT,
	CONTROLLER_FAULT_EEPROM_INIT,
	CONTROLLER_FAULT_INVALID_CONFIG,
	CONTROLLER_FAULT_CAN_TIMEOUT,
	CONTROLLER_FAULT_CAN_BUS_OFF,
	CONTROLLER_FAULT_OUTPUT_TIMEOUT,
	CONTROLLER_FAULT_EMERGENCY_STOP,
};

struct controller_prerequisites {
	bool io_ready;
	bool settings_ready;
	bool can_ready;
	bool encoder_ready;
	bool oled_ready;
};

struct controller_state_snapshot {
	enum controller_state state;
	enum controller_state previous_state;
	enum controller_fault fault;
	struct controller_prerequisites prerequisites;
	uint32_t sequence;
	int64_t entered_at_ms;
};

void controller_state_init(void);
void controller_state_set_prerequisites(
	const struct controller_prerequisites *prerequisites);
int controller_state_transition(enum controller_state next);
int controller_state_enter_fault(enum controller_fault fault);
int controller_state_clear_fault(void);
void controller_state_get(struct controller_state_snapshot *snapshot);
bool controller_state_outputs_allowed(void);
const char *controller_state_name(enum controller_state state);
const char *controller_fault_name(enum controller_fault fault);

#endif /* CONTROLLER_STATE_H_ */
