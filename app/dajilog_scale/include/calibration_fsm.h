/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_CALIBRATION_FSM_H_
#define DAJILOG_CALIBRATION_FSM_H_

#include "scale_types.h"

void calibration_fsm_reset(struct scale_state *state);
int calibration_fsm_handle(struct scale_state *state,
			   const struct scale_command_msg *command);
const char *calibration_state_name(enum scale_calibration_state state);

#endif /* DAJILOG_CALIBRATION_FSM_H_ */
