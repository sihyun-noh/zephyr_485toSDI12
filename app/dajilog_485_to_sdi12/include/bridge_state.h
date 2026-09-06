/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_BRIDGE_STATE_H_
#define DAJILOG_485_TO_SDI12_BRIDGE_STATE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#define BRIDGE_STATE_MAX_REGS 8

struct bridge_state_snapshot {
	bool value_valid;
	int last_error;
	uint32_t poll_count;
	uint32_t update_count;
	uint32_t timeout_count;
	int64_t last_update_ms;
	uint8_t reg_count;
	uint16_t regs[BRIDGE_STATE_MAX_REGS];
};

struct bridge_state {
	struct k_mutex lock;
	struct bridge_state_snapshot data;
};

void bridge_state_init(struct bridge_state *state);
void bridge_state_update_from_rs485(struct bridge_state *state,
				    const uint16_t *regs, uint8_t reg_count);
void bridge_state_mark_poll(struct bridge_state *state);
void bridge_state_mark_timeout(struct bridge_state *state, int error);
void bridge_state_snapshot_get(struct bridge_state *state,
			       struct bridge_state_snapshot *out);

#endif /* DAJILOG_485_TO_SDI12_BRIDGE_STATE_H_ */
