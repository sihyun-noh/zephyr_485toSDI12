/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AGMO_TIC_FSM_H_
#define AGMO_TIC_FSM_H_

#include <zephyr/kernel.h>

#include "tic_types.h"

int tic_fsm_start(void);
int tic_fsm_post(const struct tic_event *event, k_timeout_t timeout);
void tic_fsm_get_snapshot(struct tic_fsm_snapshot *out);
const char *tic_fsm_state_name(enum tic_state state);

#endif /* AGMO_TIC_FSM_H_ */
