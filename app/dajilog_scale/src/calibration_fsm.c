/* SPDX-License-Identifier: Apache-2.0 */

#include "calibration_fsm.h"

#include <errno.h>
#include <stddef.h>

void calibration_fsm_reset(struct scale_state *state)
{
	state->calibration_state = SCALE_CAL_IDLE;
}

int calibration_fsm_handle(struct scale_state *state,
			   const struct scale_command_msg *command)
{
	if (state == NULL || command == NULL) {
		return -EINVAL;
	}

	switch (command->command) {
	case SCALE_CMD_CAL_START:
		if (state->calibration_state != SCALE_CAL_IDLE) {
			return -EBUSY;
		}
		state->calibration.valid = false;
		state->calibration_state = SCALE_CAL_ZERO_WAIT_STABLE;
		return 0;
	case SCALE_CMD_CAL_ZERO_CAPTURE:
		if (state->calibration_state != SCALE_CAL_ZERO_WAIT_STABLE) {
			return -EPERM;
		}
		if (!state->stable) {
			return -EAGAIN;
		}
		state->calibration.zero_raw = state->filtered_raw;
		state->calibration_state = SCALE_CAL_ZERO_CAPTURED;
		return 0;
	case SCALE_CMD_CAL_SET_REFERENCE_MG:
		if (state->calibration_state != SCALE_CAL_ZERO_CAPTURED) {
			return -EPERM;
		}
		if (command->value_mg <= 0) {
			return -EINVAL;
		}
		state->calibration.reference_weight_mg = command->value_mg;
		state->calibration_state = SCALE_CAL_SPAN_WAIT_STABLE;
		return 0;
	case SCALE_CMD_CAL_SPAN_CAPTURE:
		if (state->calibration_state != SCALE_CAL_SPAN_WAIT_STABLE) {
			return -EPERM;
		}
		if (!state->stable) {
			return -EAGAIN;
		}
		state->calibration.span_raw = state->filtered_raw;
		if (state->calibration.span_raw == state->calibration.zero_raw) {
			state->calibration_state = SCALE_CAL_FAILED;
			return -ERANGE;
		}
		state->calibration.valid = true;
		state->calibration_state = SCALE_CAL_REVIEW_READY;
		return 0;
	case SCALE_CMD_CAL_SAVE:
		if (state->calibration_state != SCALE_CAL_REVIEW_READY ||
		    !state->calibration.valid) {
			return -EPERM;
		}
		state->calibration_state = SCALE_CAL_SAVE_PENDING;
		return 0;
	case SCALE_CMD_CAL_CANCEL:
		calibration_fsm_reset(state);
		return 0;
	default:
		return -ENOTSUP;
	}
}

const char *calibration_state_name(enum scale_calibration_state state)
{
	switch (state) {
	case SCALE_CAL_IDLE:
		return "IDLE";
	case SCALE_CAL_ZERO_WAIT_STABLE:
		return "ZERO_WAIT_STABLE";
	case SCALE_CAL_ZERO_CAPTURED:
		return "ZERO_CAPTURED";
	case SCALE_CAL_SPAN_WAIT_STABLE:
		return "SPAN_WAIT_STABLE";
	case SCALE_CAL_REVIEW_READY:
		return "REVIEW_READY";
	case SCALE_CAL_SAVE_PENDING:
		return "SAVE_PENDING";
	case SCALE_CAL_FAILED:
		return "FAILED";
	default:
		return "UNKNOWN";
	}
}
