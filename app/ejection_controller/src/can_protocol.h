/* SPDX-License-Identifier: Apache-2.0 */

#ifndef CAN_PROTOCOL_H_
#define CAN_PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>

#define CAN_PROTOCOL_OUTPUT_ID 0x101U
#define CAN_PROTOCOL_HEARTBEAT_ID 0x102U
#define CAN_PROTOCOL_STATUS_ID 0x201U
#define CAN_PROTOCOL_VERSION 0x01U
#define CAN_PROTOCOL_STATUS_PERIOD_MS 100U
#define CAN_PROTOCOL_HEARTBEAT_TIMEOUT_MS 500U

struct can_protocol_status {
	bool initialized;
	bool link_active;
	bool heartbeat_required;
	bool status_publishing;
	uint8_t last_heartbeat_sequence;
	int64_t last_heartbeat_ms;
	uint32_t rx_count;
	uint32_t command_count;
	uint32_t heartbeat_count;
	uint32_t echo_count;
	uint32_t status_count;
	uint32_t reject_count;
	uint32_t duplicate_count;
	uint32_t dropped_count;
	uint32_t tx_error_count;
};

int can_protocol_init(void);
void can_protocol_get_status(struct can_protocol_status *status);
int can_protocol_send_status(void);
void can_protocol_set_heartbeat_required(bool required);

#endif /* CAN_PROTOCOL_H_ */
