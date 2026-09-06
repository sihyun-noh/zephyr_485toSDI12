/* SPDX-License-Identifier: Apache-2.0 */

#include "scale_can_protocol.h"

#include <errno.h>

static int32_t get_i32_le(const uint8_t *data)
{
	return (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) |
			 ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
}

static void put_i32_le(uint8_t *data, int32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)((uint32_t)value >> 8);
	data[2] = (uint8_t)((uint32_t)value >> 16);
	data[3] = (uint8_t)((uint32_t)value >> 24);
}

int scale_can_decode_command(const struct can_frame *frame,
			     struct scale_command_msg *command)
{
	if (frame == NULL || command == NULL || frame->id != SCALE_CAN_ID_COMMAND ||
	    (frame->flags & CAN_FRAME_IDE) != 0U || frame->dlc != 8 ||
	    frame->data[0] != SCALE_CAN_PROTOCOL_VERSION) {
		return -EINVAL;
	}

	command->sequence = frame->data[2];
	command->value_mg = get_i32_le(&frame->data[4]);
	switch (frame->data[1]) {
	case SCALE_CAN_OP_GET_STATUS:
		command->command = SCALE_CMD_GET_STATUS;
		break;
	case SCALE_CAN_OP_CAL_START:
		command->command = SCALE_CMD_CAL_START;
		break;
	case SCALE_CAN_OP_CAL_SET_REFERENCE_MG:
		command->command = SCALE_CMD_CAL_SET_REFERENCE_MG;
		break;
	case SCALE_CAN_OP_CAL_ZERO_CAPTURE:
		command->command = SCALE_CMD_CAL_ZERO_CAPTURE;
		break;
	case SCALE_CAN_OP_CAL_SPAN_CAPTURE:
		command->command = SCALE_CMD_CAL_SPAN_CAPTURE;
		break;
	case SCALE_CAN_OP_CAL_SAVE:
		command->command = SCALE_CMD_CAL_SAVE;
		break;
	case SCALE_CAN_OP_CAL_CANCEL:
		command->command = SCALE_CMD_CAL_CANCEL;
		break;
	case SCALE_CAN_OP_TARE_SET:
		command->command = SCALE_CMD_TARE_SET;
		break;
	case SCALE_CAN_OP_TARE_CLEAR:
		command->command = SCALE_CMD_TARE_CLEAR;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

void scale_can_encode_status(struct can_frame *frame, const struct scale_state *state,
			     uint8_t sequence)
{
	*frame = (struct can_frame){
		.id = SCALE_CAN_ID_RESPONSE,
		.dlc = 8,
	};
	frame->data[0] = SCALE_CAN_PROTOCOL_VERSION;
	frame->data[1] = SCALE_CAN_OP_STATUS;
	frame->data[2] = sequence;
	frame->data[3] = (uint8_t)state->calibration_state;
	put_i32_le(&frame->data[4], state->last_error);
}

void scale_can_encode_measurement(struct can_frame *frame,
				  const struct scale_state *state)
{
	*frame = (struct can_frame){
		.id = SCALE_CAN_ID_MEASUREMENT,
		.dlc = 8,
	};
	frame->data[0] = SCALE_CAN_PROTOCOL_VERSION;
	frame->data[1] = SCALE_CAN_OP_MEASUREMENT;
	frame->data[2] = state->stable ? 1U : 0U;
	frame->data[3] = state->tare_active ? 1U : 0U;
	put_i32_le(&frame->data[4], state->net_weight_mg);
}
