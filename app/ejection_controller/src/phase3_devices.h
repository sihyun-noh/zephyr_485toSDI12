/* SPDX-License-Identifier: Apache-2.0 */

#ifndef PHASE3_DEVICES_H_
#define PHASE3_DEVICES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/can.h>

struct phase3_status {
	bool can_ready;
	bool oled_ready;
	bool encoder_ready;
	int32_t encoder_position;
	bool encoder_pressed;
	uint32_t can_rx_count;
	struct can_frame last_rx;
};

int phase3_devices_init(void);
void phase3_status_get(struct phase3_status *status);
int phase3_can_send(uint32_t id, const uint8_t *data, size_t length);
int phase3_oled_show(const char *line0, const char *line1,
		     const char *line2, const char *line3);

#endif /* PHASE3_DEVICES_H_ */
