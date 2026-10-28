/* SPDX-License-Identifier: Apache-2.0 */

#include "controller_state.h"

#include "ejection_io.h"
#include "ejection_scheduler.h"

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>

static struct controller_state_snapshot current;
static controller_state_change_callback_t change_callback;
static void *change_callback_user_data;
K_MUTEX_DEFINE(state_mutex);

static bool required_devices_ready(void)
{
	return current.prerequisites.io_ready &&
	       current.prerequisites.settings_ready &&
	       current.prerequisites.can_ready &&
	       current.prerequisites.encoder_ready;
}

static bool transition_allowed(enum controller_state from,
			       enum controller_state to)
{
	if (to == CONTROLLER_STATE_FAULT) {
		return from != CONTROLLER_STATE_FAULT;
	}

	switch (from) {
	case CONTROLLER_STATE_BOOT:
		return to == CONTROLLER_STATE_SELF_TEST;
	case CONTROLLER_STATE_SELF_TEST:
		return to == CONTROLLER_STATE_IDLE;
	case CONTROLLER_STATE_IDLE:
		return to == CONTROLLER_STATE_READY;
	case CONTROLLER_STATE_READY:
		return to == CONTROLLER_STATE_RUNNING ||
		       to == CONTROLLER_STATE_IDLE;
	case CONTROLLER_STATE_RUNNING:
		return to == CONTROLLER_STATE_IDLE;
	case CONTROLLER_STATE_FAULT:
	default:
		return false;
	}
}

static void commit_state(enum controller_state next)
{
	current.previous_state = current.state;
	current.state = next;
	current.sequence++;
	current.entered_at_ms = k_uptime_get();
	ejection_scheduler_invalidate();
	if (change_callback != NULL) {
		change_callback(change_callback_user_data);
	}
}

void controller_state_init(void)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	memset(&current, 0, sizeof(current));
	current.state = CONTROLLER_STATE_BOOT;
	current.previous_state = CONTROLLER_STATE_BOOT;
	current.fault = CONTROLLER_FAULT_NONE;
	current.sequence = 1U;
	current.entered_at_ms = k_uptime_get();
	k_mutex_unlock(&state_mutex);
}

void controller_state_set_prerequisites(
	const struct controller_prerequisites *prerequisites)
{
	if (prerequisites == NULL) {
		return;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	current.prerequisites = *prerequisites;
	k_mutex_unlock(&state_mutex);
}

int controller_state_transition(enum controller_state next)
{
	int ret = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (next < CONTROLLER_STATE_BOOT || next > CONTROLLER_STATE_FAULT) {
		ret = -EINVAL;
	} else if (!transition_allowed(current.state, next)) {
		ret = -EPERM;
	} else if ((next == CONTROLLER_STATE_IDLE ||
		    next == CONTROLLER_STATE_READY ||
		    next == CONTROLLER_STATE_RUNNING) &&
		   !required_devices_ready()) {
		ret = -EHOSTDOWN;
	} else if (next == CONTROLLER_STATE_RUNNING && !current.run_permitted) {
		ret = -ENOTCONN;
	} else {
		commit_state(next);
	}
	k_mutex_unlock(&state_mutex);

	if (ret == 0 && next != CONTROLLER_STATE_RUNNING) {
		(void)ejection_scheduler_cancel_all();
	}
	return ret;
}

void controller_state_set_run_permission(bool permitted)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	current.run_permitted = permitted;
	k_mutex_unlock(&state_mutex);
}

int controller_state_enter_fault(enum controller_fault fault)
{
	if (fault <= CONTROLLER_FAULT_NONE ||
	    fault > CONTROLLER_FAULT_EMERGENCY_STOP) {
		return -EINVAL;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (current.state != CONTROLLER_STATE_FAULT) {
		commit_state(CONTROLLER_STATE_FAULT);
	}
	if (current.fault == CONTROLLER_FAULT_NONE) {
		current.fault = fault;
	}
	k_mutex_unlock(&state_mutex);

	/* Change state first so no new ON command can race the safety shutdown. */
	return ejection_scheduler_cancel_all();
}

int controller_state_clear_fault(void)
{
	int ret = 0;

	k_mutex_lock(&state_mutex, K_FOREVER);
	if (current.state != CONTROLLER_STATE_FAULT) {
		ret = -EALREADY;
	} else if (!required_devices_ready()) {
		ret = -EHOSTDOWN;
	} else if (ejection_output_any_active()) {
		ret = -EBUSY;
	} else {
		current.fault = CONTROLLER_FAULT_NONE;
		commit_state(CONTROLLER_STATE_IDLE);
	}
	k_mutex_unlock(&state_mutex);
	return ret;
}

void controller_state_get(struct controller_state_snapshot *snapshot)
{
	if (snapshot == NULL) {
		return;
	}

	k_mutex_lock(&state_mutex, K_FOREVER);
	*snapshot = current;
	k_mutex_unlock(&state_mutex);
}

bool controller_state_outputs_allowed(void)
{
	bool allowed;

	k_mutex_lock(&state_mutex, K_FOREVER);
	allowed = current.state == CONTROLLER_STATE_RUNNING &&
		  current.fault == CONTROLLER_FAULT_NONE;
	k_mutex_unlock(&state_mutex);
	return allowed;
}

bool controller_state_manual_outputs_allowed(void)
{
	bool allowed;

	k_mutex_lock(&state_mutex, K_FOREVER);
	allowed = current.state == CONTROLLER_STATE_IDLE &&
		  current.fault == CONTROLLER_FAULT_NONE &&
		  required_devices_ready();
	k_mutex_unlock(&state_mutex);
	return allowed;
}

const char *controller_state_name(enum controller_state state)
{
	switch (state) {
	case CONTROLLER_STATE_BOOT: return "BOOT";
	case CONTROLLER_STATE_SELF_TEST: return "SELF_TEST";
	case CONTROLLER_STATE_IDLE: return "IDLE";
	case CONTROLLER_STATE_READY: return "READY";
	case CONTROLLER_STATE_RUNNING: return "RUNNING";
	case CONTROLLER_STATE_FAULT: return "FAULT";
	default: return "UNKNOWN";
	}
}

const char *controller_fault_name(enum controller_fault fault)
{
	switch (fault) {
	case CONTROLLER_FAULT_NONE: return "NONE";
	case CONTROLLER_FAULT_IO_INIT: return "IO_INIT";
	case CONTROLLER_FAULT_CAN_INIT: return "CAN_INIT";
	case CONTROLLER_FAULT_OLED_INIT: return "OLED_INIT";
	case CONTROLLER_FAULT_EEPROM_INIT: return "EEPROM_INIT";
	case CONTROLLER_FAULT_INVALID_CONFIG: return "INVALID_CONFIG";
	case CONTROLLER_FAULT_CAN_TIMEOUT: return "CAN_TIMEOUT";
	case CONTROLLER_FAULT_CAN_BUS_OFF: return "CAN_BUS_OFF";
	case CONTROLLER_FAULT_EMERGENCY_STOP: return "EMERGENCY_STOP";
	default: return "UNKNOWN";
	}
}

void controller_state_set_change_callback(
	controller_state_change_callback_t callback, void *user_data)
{
	k_mutex_lock(&state_mutex, K_FOREVER);
	change_callback = callback;
	change_callback_user_data = user_data;
	k_mutex_unlock(&state_mutex);
}
