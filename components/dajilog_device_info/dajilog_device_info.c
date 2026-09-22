/* SPDX-License-Identifier: Apache-2.0 */

#include "dajilog_device_info.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>

static void put_i32_le(uint8_t *data, int32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)((uint32_t)value >> 8);
	data[2] = (uint8_t)((uint32_t)value >> 16);
	data[3] = (uint8_t)((uint32_t)value >> 24);
}

static bool request_is_valid(const struct dajilog_device_info *ctx,
			     const struct can_frame *request,
			     struct dajilog_device_profile_state *profile)
{
	if (ctx == NULL || request == NULL || profile == NULL || ctx->profile == NULL) {
		return false;
	}

	dajilog_device_profile_get(ctx->profile, profile);
	return profile->valid &&
	       request->id == DAJILOG_DEVICE_INFO_REQUEST_BASE_ID + profile->data.node_id &&
	       (request->flags & CAN_FRAME_IDE) == 0U &&
	       request->dlc == 8U &&
	       request->data[0] == DAJILOG_DEVICE_INFO_PROTOCOL_VERSION;
}

int dajilog_device_info_get_chip_uid(uint8_t uid[DAJILOG_DEVICE_INFO_UID_SIZE])
{
	ssize_t length;

	if (uid == NULL) {
		return -EINVAL;
	}

	length = hwinfo_get_device_id(uid, DAJILOG_DEVICE_INFO_UID_SIZE);
	if (length < 0) {
		return (int)length;
	}

	return length == DAJILOG_DEVICE_INFO_UID_SIZE ? 0 : -EIO;
}

int dajilog_device_info_get_serial_uid_tail(
	uint8_t serial[DAJILOG_DEVICE_INFO_SERIAL_UID_TAIL_SIZE])
{
	uint8_t uid[DAJILOG_DEVICE_INFO_UID_SIZE];
	int ret;

	if (serial == NULL) {
		return -EINVAL;
	}

	ret = dajilog_device_info_get_chip_uid(uid);
	if (ret != 0) {
		return ret;
	}

	memcpy(serial, &uid[DAJILOG_DEVICE_INFO_UID_SIZE -
			    DAJILOG_DEVICE_INFO_SERIAL_UID_TAIL_SIZE],
	       DAJILOG_DEVICE_INFO_SERIAL_UID_TAIL_SIZE);
	return 0;
}

int dajilog_device_info_init(struct dajilog_device_info *ctx,
			     const struct dajilog_device_identity *identity,
			     struct dajilog_device_profile *profile)
{
	if (ctx == NULL || identity == NULL || profile == NULL) {
		return -EINVAL;
	}

	*ctx = (struct dajilog_device_info){
		.identity = *identity,
		.profile = profile,
	};
	return 0;
}

bool dajilog_device_info_is_request_for(const struct dajilog_device_info *ctx,
					const struct can_frame *request)
{
	struct dajilog_device_profile_state profile;

	return request_is_valid(ctx, request, &profile);
}

int dajilog_device_info_handle_request(struct dajilog_device_info *ctx,
				       const struct dajilog_device_runtime *runtime,
				       const struct can_frame *request,
				       struct can_frame *response)
{
	struct dajilog_device_profile_state profile;

	if (ctx == NULL || runtime == NULL || response == NULL ||
	    !request_is_valid(ctx, request, &profile)) {
		if (ctx != NULL) {
			ctx->reject_count++;
		}
		return -EINVAL;
	}

	*response = (struct can_frame){
		.id = DAJILOG_DEVICE_INFO_RESPONSE_BASE_ID + profile.data.node_id,
		.dlc = 8U,
	};
	response->data[0] = DAJILOG_DEVICE_INFO_PROTOCOL_VERSION;
	response->data[2] = request->data[2];
	ctx->request_count++;

	switch (request->data[1]) {
	case DAJILOG_DEVICE_INFO_GET_BASIC:
		response->data[1] = DAJILOG_DEVICE_INFO_BASIC_RESPONSE;
		response->data[3] = profile.data.node_id;
		response->data[4] = ctx->identity.device_type;
		response->data[5] = ctx->identity.hardware_revision;
		response->data[6] = (uint8_t)ctx->identity.capability;
		response->data[7] = (uint8_t)(ctx->identity.capability >> 8);
		break;
	case DAJILOG_DEVICE_INFO_GET_FIRMWARE:
		response->data[1] = DAJILOG_DEVICE_INFO_FIRMWARE_RESPONSE;
		response->data[3] = ctx->identity.firmware_major;
		response->data[4] = ctx->identity.firmware_minor;
		response->data[5] = ctx->identity.firmware_patch;
		response->data[6] = ctx->identity.app_protocol_version;
		response->data[7] = ctx->identity.firmware_build;
		break;
	case DAJILOG_DEVICE_INFO_GET_SERIAL: {
		uint8_t serial[DAJILOG_DEVICE_INFO_SERIAL_UID_TAIL_SIZE];
		uint8_t part = request->data[3];
		size_t offset;
		size_t length;
		int ret;

		if (part >= DAJILOG_DEVICE_INFO_SERIAL_PART_COUNT) {
			ctx->reject_count++;
			return -EINVAL;
		}

		ret = dajilog_device_info_get_serial_uid_tail(serial);
		if (ret != 0) {
			ctx->reject_count++;
			return ret;
		}

		offset = part * DAJILOG_DEVICE_INFO_SERIAL_PART_SIZE;
		length = MIN(DAJILOG_DEVICE_INFO_SERIAL_PART_SIZE,
			     DAJILOG_DEVICE_INFO_SERIAL_UID_TAIL_SIZE - offset);
		response->data[1] = DAJILOG_DEVICE_INFO_SERIAL_RESPONSE;
		response->data[3] = part;
		memcpy(&response->data[4], &serial[offset], length);
		break;
	}
	case DAJILOG_DEVICE_INFO_GET_UID_PART: {
		uint8_t uid[DAJILOG_DEVICE_INFO_UID_SIZE];
		uint8_t part = request->data[3];
		int ret;

		if (part >= DAJILOG_DEVICE_INFO_UID_PART_COUNT) {
			ctx->reject_count++;
			return -EINVAL;
		}

		ret = dajilog_device_info_get_chip_uid(uid);
		if (ret != 0) {
			ctx->reject_count++;
			return ret;
		}

		response->data[1] = DAJILOG_DEVICE_INFO_UID_PART_RESPONSE;
		response->data[3] = part;
		memcpy(&response->data[4],
		       &uid[part * DAJILOG_DEVICE_INFO_UID_PART_SIZE],
		       DAJILOG_DEVICE_INFO_UID_PART_SIZE);
		break;
	}
	case DAJILOG_DEVICE_INFO_GET_STATUS:
		response->data[1] = DAJILOG_DEVICE_INFO_STATUS_RESPONSE;
		response->data[3] = (uint8_t)runtime->health;
		put_i32_le(&response->data[4], runtime->last_error);
		break;
	default:
		ctx->reject_count++;
		return -ENOTSUP;
	}

	ctx->response_count++;
	return 0;
}
