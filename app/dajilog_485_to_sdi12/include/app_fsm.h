/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_APP_FSM_H_
#define DAJILOG_485_TO_SDI12_APP_FSM_H_

#include <stdint.h>

#include "app_msg.h"

enum app_state {
  APP_STATE_BOOT,
  APP_STATE_INIT_HW,
  APP_STATE_WAIT_FIRST_SAMPLE,
  APP_STATE_READY,
  APP_STATE_ACTIVE_SESSION,
  APP_STATE_SENSOR_ERROR,
  APP_STATE_FATAL_ERROR,
};

struct app_fsm {
  enum app_state state;
  enum app_state previous_state;
  int64_t entered_at_ms;
  uint32_t event_count;
  int last_error;
};

void app_fsm_init(struct app_fsm *fsm);
void app_fsm_handle_msg(struct app_fsm *fsm, const struct app_msg *msg);
const char *app_state_name(enum app_state state);

#endif /* DAJILOG_485_TO_SDI12_APP_FSM_H_ */
