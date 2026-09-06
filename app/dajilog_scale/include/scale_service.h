/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SCALE_SERVICE_H_
#define DAJILOG_SCALE_SERVICE_H_

#include <zephyr/kernel.h>

#include <ads1232/ads1232.h>

#include "scale_types.h"

struct scale_service {
	struct ads1232 *adc;
	struct scale_state state;
	struct k_mutex lock;
	struct k_msgq command_msgq;
	char command_buffer[CONFIG_SCALE_SERVICE_MSGQ_DEPTH *
			    sizeof(struct scale_command_msg)] __aligned(4);
	struct k_thread thread;
};

int scale_service_init(struct scale_service *ctx, struct ads1232 *adc);
int scale_service_start(struct scale_service *ctx);
int scale_service_post_command(struct scale_service *ctx,
			       const struct scale_command_msg *command);
void scale_service_state_get(struct scale_service *ctx, struct scale_state *out);

#endif /* DAJILOG_SCALE_SERVICE_H_ */
