/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_CAN_BUS_H_
#define DAJILOG_CAN_BUS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>

enum can_bus_msg_type {
	CAN_BUS_MSG_SEND_FRAME,
};

struct can_bus_msg {
	enum can_bus_msg_type type;
	struct can_frame frame;
};

struct can_bus_config {
	const struct device *dev;
	uint32_t default_tx_id;
};

struct can_bus_state {
	bool ready;
	bool thread_started;
	int last_error;
	int rx_filter_id;
	enum can_state bus_state;
	struct can_bus_err_cnt err_cnt;
	uint32_t rx_count;
	uint32_t rx_drop_count;
	uint32_t tx_count;
	uint32_t tx_error_count;
	struct can_frame latest_rx;
	bool latest_rx_valid;
};

struct can_bus {
	struct can_bus_config config;
	struct can_bus_state state;
	struct k_msgq msgq;
	char msgq_buffer[CONFIG_DAJILOG_CAN_MSGQ_DEPTH *
			 sizeof(struct can_bus_msg)] __aligned(4);
	struct k_msgq rx_msgq;
	char rx_msgq_buffer[CONFIG_DAJILOG_CAN_RX_MSGQ_DEPTH *
			      sizeof(struct can_frame)] __aligned(4);
};

int can_bus_init(struct can_bus *ctx);
int can_bus_start(struct can_bus *ctx);
int can_bus_send(struct can_bus *ctx, const struct can_frame *frame);
int can_bus_receive(struct can_bus *ctx, struct can_frame *frame, k_timeout_t timeout);
int can_bus_send_test(struct can_bus *ctx);
void can_bus_state_get(const struct can_bus *ctx, struct can_bus_state *out);
const char *can_bus_state_name(enum can_state state);

#endif /* DAJILOG_CAN_BUS_H_ */
