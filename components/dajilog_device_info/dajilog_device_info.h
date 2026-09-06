/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_DEVICE_INFO_H_
#define DAJILOG_DEVICE_INFO_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/can.h>

#include <dajilog_device_profile/dajilog_device_profile.h>

#define DAJILOG_DEVICE_INFO_PROTOCOL_VERSION 0x01U
#define DAJILOG_DEVICE_INFO_REQUEST_BASE_ID 0x600U
#define DAJILOG_DEVICE_INFO_RESPONSE_BASE_ID 0x580U
#define DAJILOG_DEVICE_INFO_UID_SIZE 12U
#define DAJILOG_DEVICE_INFO_UID_PART_SIZE 4U
#define DAJILOG_DEVICE_INFO_UID_PART_COUNT \
	(DAJILOG_DEVICE_INFO_UID_SIZE / DAJILOG_DEVICE_INFO_UID_PART_SIZE)

enum dajilog_device_type {
	DAJILOG_DEVICE_TYPE_SCALE = 0x01,
	DAJILOG_DEVICE_TYPE_485_TO_SDI12 = 0x02,
	DAJILOG_DEVICE_TYPE_AGMO_TIC = 0x03,
	DAJILOG_DEVICE_TYPE_TEST = 0x7F,
};

enum dajilog_device_capability {
	DAJILOG_CAPABILITY_MEASUREMENT = (1U << 0),
	DAJILOG_CAPABILITY_CALIBRATION = (1U << 1),
	DAJILOG_CAPABILITY_EEPROM_STORE = (1U << 2),
	DAJILOG_CAPABILITY_ZERO_TRACKING = (1U << 3),
	DAJILOG_CAPABILITY_RS485_MODBUS = (1U << 4),
	DAJILOG_CAPABILITY_SDI12_SLAVE = (1U << 5),
	DAJILOG_CAPABILITY_CAN_FD = (1U << 6),
	DAJILOG_CAPABILITY_DIGITAL_IO = (1U << 7),
	DAJILOG_CAPABILITY_DISPLAY_LOCAL = (1U << 8),
	DAJILOG_CAPABILITY_SERVICE_MODE = (1U << 9),
};

enum dajilog_device_info_command {
	DAJILOG_DEVICE_INFO_GET_BASIC = 0x01,
	DAJILOG_DEVICE_INFO_GET_FIRMWARE = 0x02,
	DAJILOG_DEVICE_INFO_GET_SERIAL = 0x03,
	DAJILOG_DEVICE_INFO_GET_UID_PART = 0x04,
	DAJILOG_DEVICE_INFO_GET_STATUS = 0x05,
};

enum dajilog_device_info_response {
	DAJILOG_DEVICE_INFO_BASIC_RESPONSE = 0x81,
	DAJILOG_DEVICE_INFO_FIRMWARE_RESPONSE = 0x82,
	DAJILOG_DEVICE_INFO_SERIAL_RESPONSE = 0x83,
	DAJILOG_DEVICE_INFO_STATUS_RESPONSE = 0x84,
	DAJILOG_DEVICE_INFO_UID_PART_RESPONSE = 0x86,
};

enum dajilog_device_health {
	DAJILOG_DEVICE_HEALTH_UNASSIGNED = 0,
	DAJILOG_DEVICE_HEALTH_READY = 1,
	DAJILOG_DEVICE_HEALTH_DEGRADED = 2,
	DAJILOG_DEVICE_HEALTH_FAULT = 3,
};

struct dajilog_device_identity {
	uint8_t device_type;
	uint8_t hardware_revision;
	uint8_t firmware_major;
	uint8_t firmware_minor;
	uint8_t firmware_patch;
	uint8_t firmware_build;
	uint8_t app_protocol_version;
	uint16_t capability;
};

struct dajilog_device_runtime {
	enum dajilog_device_health health;
	int last_error;
};

struct dajilog_device_info {
	struct dajilog_device_identity identity;
	struct dajilog_device_profile *profile;
	uint32_t request_count;
	uint32_t reject_count;
	uint32_t response_count;
};

int dajilog_device_info_init(struct dajilog_device_info *ctx,
			     const struct dajilog_device_identity *identity,
			     struct dajilog_device_profile *profile);
int dajilog_device_info_get_chip_uid(uint8_t uid[DAJILOG_DEVICE_INFO_UID_SIZE]);
bool dajilog_device_info_is_request_for(const struct dajilog_device_info *ctx,
					const struct can_frame *request);
int dajilog_device_info_handle_request(struct dajilog_device_info *ctx,
				       const struct dajilog_device_runtime *runtime,
				       const struct can_frame *request,
				       struct can_frame *response);

#endif /* DAJILOG_DEVICE_INFO_H_ */
