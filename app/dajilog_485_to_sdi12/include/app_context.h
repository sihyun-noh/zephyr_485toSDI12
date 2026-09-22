/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_APP_CONTEXT_H_
#define DAJILOG_485_TO_SDI12_APP_CONTEXT_H_

#include <zephyr/kernel.h>

#include <can_bus/can_bus.h>
#include <dajilog_device_info/dajilog_device_info.h>
#include <dajilog_device_profile/dajilog_device_profile.h>
#include <rs485_modbus/rs485_modbus.h>
#include <sdi12_slave/sdi12_slave.h>

#include "app_fsm.h"
#include "app_msg.h"
#include "bridge_can_service.h"
#include "bridge_state.h"

struct app_context {
  struct app_fsm fsm;
  struct bridge_state bridge;

  struct k_msgq app_msgq;
  char app_msgq_buffer[CONFIG_BRIDGE_APP_MSGQ_DEPTH *
                       sizeof(struct app_msg)] __aligned(4);

  struct rs485_modbus rs485;
  struct sdi12_slave sdi12;
  struct can_bus can;
  struct dajilog_device_profile device_profile;
  struct dajilog_device_info device_info;
  struct bridge_can_service can_service;
};

extern struct app_context app;

int app_context_init(struct app_context *ctx);
int app_device_services_init(struct app_context *ctx);
int app_components_init(struct app_context *ctx);
int app_tasks_start(struct app_context *ctx);
void app_print_profile(void);

#endif /* DAJILOG_485_TO_SDI12_APP_CONTEXT_H_ */
