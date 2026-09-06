/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AGMO_TIC_CAN_H_
#define AGMO_TIC_CAN_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/can.h>

struct tic_can_snapshot {
	bool ready;
	bool command_received;
	bool timeout;
	enum can_state bus_state;
	struct can_bus_err_cnt error_count;
	uint32_t rx_count;
	uint32_t rx_drop_count;
	uint32_t rx_invalid_count;
	uint32_t tx_count;
	uint32_t tx_error_count;
	int last_error;
};

int tic_can_init(void);
int tic_can_start(void);
void tic_can_get_snapshot(struct tic_can_snapshot *out);
const char *tic_can_state_name(enum can_state state);

#endif /* AGMO_TIC_CAN_H_ */
