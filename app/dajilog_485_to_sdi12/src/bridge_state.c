/* SPDX-License-Identifier: Apache-2.0 */

#include "bridge_state.h"

#include <string.h>

void bridge_state_init(struct bridge_state *state)
{
	if (state == NULL) {
		return;
	}

	k_mutex_init(&state->lock);
	memset(&state->data, 0, sizeof(state->data));
}

void bridge_state_mark_poll(struct bridge_state *state)
{
	if (state == NULL) {
		return;
	}

	k_mutex_lock(&state->lock, K_FOREVER);
	state->data.poll_count++;
	k_mutex_unlock(&state->lock);
}

void bridge_state_update_from_rs485(struct bridge_state *state,
				    const uint16_t *regs, uint8_t reg_count)
{
	uint8_t count = reg_count;

	if (state == NULL || regs == NULL) {
		return;
	}

	if (count > BRIDGE_STATE_MAX_REGS) {
		count = BRIDGE_STATE_MAX_REGS;
	}

	k_mutex_lock(&state->lock, K_FOREVER);
	for (uint8_t i = 0; i < count; i++) {
		state->data.regs[i] = regs[i];
	}
	state->data.reg_count = count;
	state->data.value_valid = true;
	state->data.last_error = 0;
	state->data.update_count++;
	state->data.last_update_ms = k_uptime_get();
	k_mutex_unlock(&state->lock);
}

void bridge_state_mark_timeout(struct bridge_state *state, int error)
{
	if (state == NULL) {
		return;
	}

	k_mutex_lock(&state->lock, K_FOREVER);
	state->data.timeout_count++;
	state->data.last_error = error;
	k_mutex_unlock(&state->lock);
}

void bridge_state_snapshot_get(struct bridge_state *state,
			       struct bridge_state_snapshot *out)
{
	if (state == NULL || out == NULL) {
		return;
	}

	k_mutex_lock(&state->lock, K_FOREVER);
	*out = state->data;
	k_mutex_unlock(&state->lock);
}
