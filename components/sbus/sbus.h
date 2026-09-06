/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SBUS_H_
#define DAJILOG_SBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#define SBUS_CHANNEL_COUNT 16

struct sbus_config {
	const struct device *uart;
	uint32_t baudrate;
	bool inverted_by_hardware;
};

struct sbus_state {
	bool ready;
	bool thread_started;
	bool frame_valid;
	int last_error;
	uint16_t channels[SBUS_CHANNEL_COUNT];
	uint8_t flags;
	uint32_t frame_count;
	uint32_t sync_lost_count;
	uint32_t bad_end_count;
};

struct sbus {
	struct sbus_config config;
	struct sbus_state state;
};

int sbus_init(struct sbus *ctx);
int sbus_start(struct sbus *ctx);
void sbus_state_get(const struct sbus *ctx, struct sbus_state *out);

#endif /* DAJILOG_SBUS_H_ */
