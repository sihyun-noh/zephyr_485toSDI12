/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SCALE_CAN_SERVICE_H_
#define DAJILOG_SCALE_CAN_SERVICE_H_

#include <zephyr/kernel.h>

#include <can_bus/can_bus.h>
#include <dajilog_device_info/dajilog_device_info.h>

#include "scale_service.h"

struct scale_can_service {
	struct can_bus *bus;
	struct scale_service *scale;
	struct dajilog_device_info *device_info;
	struct k_thread thread;
};

int scale_can_service_init(struct scale_can_service *ctx, struct can_bus *bus,
			   struct scale_service *scale,
			   struct dajilog_device_info *device_info);
int scale_can_service_start(struct scale_can_service *ctx);

#endif /* DAJILOG_SCALE_CAN_SERVICE_H_ */
