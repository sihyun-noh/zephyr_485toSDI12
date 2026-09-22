/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_BRIDGE_CAN_SERVICE_H_
#define DAJILOG_485_TO_SDI12_BRIDGE_CAN_SERVICE_H_

#include <zephyr/kernel.h>

#include <can_bus/can_bus.h>
#include <dajilog_device_info/dajilog_device_info.h>

#define BRIDGE_CAN_TELEMETRY_BASE_ID 0x400U
#define BRIDGE_CAN_PROTOCOL_VERSION 0x01U
#define BRIDGE_CAN_OP_MEASUREMENT 0x81U

struct app_context;

struct bridge_can_service {
  struct can_bus *bus;
  struct app_context *app;
  struct dajilog_device_info *device_info;
  uint8_t telemetry_sequence;
};

int bridge_can_service_init(struct bridge_can_service *ctx, struct can_bus *bus,
                            struct app_context *app,
                            struct dajilog_device_info *device_info);
void bridge_can_service_process(struct bridge_can_service *ctx,
                                const struct can_frame *frame);
int bridge_can_service_publish_measurement(struct bridge_can_service *ctx);
int bridge_can_service_respond_measurement(struct bridge_can_service *ctx,
					   uint8_t sequence);
void bridge_device_runtime_get(void *user_data,
                               struct dajilog_device_runtime *runtime);

#endif /* DAJILOG_485_TO_SDI12_BRIDGE_CAN_SERVICE_H_ */
