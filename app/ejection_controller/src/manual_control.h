/* SPDX-License-Identifier: Apache-2.0 */

#ifndef MANUAL_CONTROL_H_
#define MANUAL_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

struct manual_control_status {
	bool initialized;
	bool active;
	bool armed;
	uint16_t button_mask;
	uint32_t trigger_count;
	uint32_t rejected_count;
	int last_error;
	int8_t last_channel;
};

int manual_control_init(void);
bool manual_control_is_active(void);
void manual_control_get_status(struct manual_control_status *status);

#endif /* MANUAL_CONTROL_H_ */
