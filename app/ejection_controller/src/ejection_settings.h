/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EJECTION_SETTINGS_H_
#define EJECTION_SETTINGS_H_

#include <stdbool.h>
#include <stdint.h>

struct ejection_settings_data {
	uint32_t can_bitrate;
	uint16_t default_pulse_ms;
	uint8_t encoder_counts_per_step;
	uint8_t reserved;
	uint16_t conveyor_delay_ms;
	uint16_t ejection_hold_ms;
};

struct ejection_settings_state {
	bool ready;
	bool persistent;
	int last_error;
	uint32_t sequence;
	struct ejection_settings_data data;
};

int ejection_settings_init(void);
void ejection_settings_get(struct ejection_settings_state *state);
int ejection_settings_set(const struct ejection_settings_data *data);
int ejection_settings_save(void);
int ejection_settings_defaults(void);

#endif /* EJECTION_SETTINGS_H_ */
