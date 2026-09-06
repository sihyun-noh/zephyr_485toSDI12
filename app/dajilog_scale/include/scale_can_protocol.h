/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SCALE_CAN_PROTOCOL_H_
#define DAJILOG_SCALE_CAN_PROTOCOL_H_

#include <zephyr/drivers/can.h>

#include "scale_types.h"

#define SCALE_CAN_PROTOCOL_VERSION 0x01U
#define SCALE_CAN_ID_COMMAND 0x320U
#define SCALE_CAN_ID_RESPONSE 0x321U
#define SCALE_CAN_ID_MEASUREMENT 0x322U
#define SCALE_CAN_ID_HEARTBEAT 0x323U

enum scale_can_opcode {
	SCALE_CAN_OP_GET_STATUS = 0x01,
	SCALE_CAN_OP_CAL_START = 0x10,
	SCALE_CAN_OP_CAL_SET_REFERENCE_MG = 0x11,
	SCALE_CAN_OP_CAL_ZERO_CAPTURE = 0x12,
	SCALE_CAN_OP_CAL_SPAN_CAPTURE = 0x13,
	SCALE_CAN_OP_CAL_SAVE = 0x14,
	SCALE_CAN_OP_CAL_CANCEL = 0x15,
	SCALE_CAN_OP_TARE_SET = 0x20,
	SCALE_CAN_OP_TARE_CLEAR = 0x21,
	SCALE_CAN_OP_STATUS = 0x80,
	SCALE_CAN_OP_MEASUREMENT = 0x81,
	SCALE_CAN_OP_ACK = 0x82,
	SCALE_CAN_OP_ERROR = 0xff,
};

int scale_can_decode_command(const struct can_frame *frame,
			     struct scale_command_msg *command);
void scale_can_encode_status(struct can_frame *frame, const struct scale_state *state,
			     uint8_t sequence);
void scale_can_encode_measurement(struct can_frame *frame,
				  const struct scale_state *state);

#endif /* DAJILOG_SCALE_CAN_PROTOCOL_H_ */
