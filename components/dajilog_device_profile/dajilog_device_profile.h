/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_DEVICE_PROFILE_H_
#define DAJILOG_DEVICE_PROFILE_H_

#include <stdbool.h>
#include <stdint.h>

struct dajilog_device_profile_data {
	uint8_t node_id;
	uint8_t hardware_revision;
	uint32_t serial_number;
	uint32_t sequence;
};

struct dajilog_device_profile_config {
	bool allow_development_profile;
	uint8_t development_node_id;
	uint8_t development_hardware_revision;
	uint32_t development_serial_number;
};

struct dajilog_device_profile_state {
	bool valid;
	bool persistent;
	bool development;
	int last_error;
	struct dajilog_device_profile_data data;
};

struct dajilog_device_profile {
	struct dajilog_device_profile_config config;
	struct dajilog_device_profile_state state;
};

int dajilog_device_profile_init(struct dajilog_device_profile *ctx);
int dajilog_device_profile_save(struct dajilog_device_profile *ctx,
				const struct dajilog_device_profile_data *data);
void dajilog_device_profile_get(const struct dajilog_device_profile *ctx,
				struct dajilog_device_profile_state *out);

#endif /* DAJILOG_DEVICE_PROFILE_H_ */
