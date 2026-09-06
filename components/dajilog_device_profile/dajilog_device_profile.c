/* SPDX-License-Identifier: Apache-2.0 */

#include "dajilog_device_profile.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/sys/crc.h>

#include <eeprom_store/eeprom_store.h>

#define DAJILOG_DEVICE_PROFILE_MAGIC 0x444A5052U
#define DAJILOG_DEVICE_PROFILE_FORMAT_VERSION 1U
#define DAJILOG_DEVICE_PROFILE_SLOT_A_OFFSET 0x0200
#define DAJILOG_DEVICE_PROFILE_SLOT_B_OFFSET 0x0300

struct dajilog_device_profile_record {
	uint32_t magic;
	uint16_t format_version;
	uint16_t length;
	uint32_t sequence;
	uint32_t serial_number;
	uint8_t node_id;
	uint8_t hardware_revision;
	uint16_t reserved;
	uint32_t crc32;
};

static bool node_id_is_valid(uint8_t node_id)
{
	return node_id >= 1U && node_id <= 127U;
}

static uint32_t profile_crc(const struct dajilog_device_profile_record *record)
{
	return crc32_ieee((const uint8_t *)record,
			  offsetof(struct dajilog_device_profile_record, crc32));
}

static bool profile_record_is_valid(const struct dajilog_device_profile_record *record)
{
	return record->magic == DAJILOG_DEVICE_PROFILE_MAGIC &&
	       record->format_version == DAJILOG_DEVICE_PROFILE_FORMAT_VERSION &&
	       record->length == sizeof(*record) &&
	       node_id_is_valid(record->node_id) &&
	       record->serial_number != 0U &&
	       record->crc32 == profile_crc(record);
}

static int profile_read(off_t offset, struct dajilog_device_profile_record *record)
{
	int ret = eeprom_store_read(offset, record, sizeof(*record));

	if (ret != 0) {
		return ret;
	}

	return profile_record_is_valid(record) ? 0 : -EBADMSG;
}

static void profile_state_from_record(struct dajilog_device_profile_state *state,
				      const struct dajilog_device_profile_record *record)
{
	state->valid = true;
	state->persistent = true;
	state->development = false;
	state->last_error = 0;
	state->data = (struct dajilog_device_profile_data){
		.node_id = record->node_id,
		.hardware_revision = record->hardware_revision,
		.serial_number = record->serial_number,
		.sequence = record->sequence,
	};
}

static bool development_profile_is_valid(const struct dajilog_device_profile_config *config)
{
	return config->allow_development_profile &&
	       node_id_is_valid(config->development_node_id) &&
	       config->development_serial_number != 0U;
}

int dajilog_device_profile_init(struct dajilog_device_profile *ctx)
{
	struct dajilog_device_profile_record slot_a;
	struct dajilog_device_profile_record slot_b;
	const struct dajilog_device_profile_record *selected = NULL;
	int ret_a;
	int ret_b;
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ctx->state = (struct dajilog_device_profile_state){0};
	ret = eeprom_store_init();
	if (ret == 0) {
		ret_a = profile_read(DAJILOG_DEVICE_PROFILE_SLOT_A_OFFSET, &slot_a);
		ret_b = profile_read(DAJILOG_DEVICE_PROFILE_SLOT_B_OFFSET, &slot_b);
		if (ret_a == 0 && (ret_b != 0 || slot_a.sequence >= slot_b.sequence)) {
			selected = &slot_a;
		} else if (ret_b == 0) {
			selected = &slot_b;
		}
	}

	if (selected != NULL) {
		profile_state_from_record(&ctx->state, selected);
		return 0;
	}

	if (development_profile_is_valid(&ctx->config)) {
		ctx->state.valid = true;
		ctx->state.development = true;
		ctx->state.last_error = 0;
		ctx->state.data = (struct dajilog_device_profile_data){
			.node_id = ctx->config.development_node_id,
			.hardware_revision = ctx->config.development_hardware_revision,
			.serial_number = ctx->config.development_serial_number,
		};
		return 0;
	}

	ctx->state.last_error = ret != 0 ? ret : -ENOENT;
	return ctx->state.last_error;
}

int dajilog_device_profile_save(struct dajilog_device_profile *ctx,
				const struct dajilog_device_profile_data *data)
{
	struct dajilog_device_profile_record record;
	struct dajilog_device_profile_record verify;
	off_t offset;
	int ret;

	if (ctx == NULL || data == NULL || !node_id_is_valid(data->node_id) ||
	    data->serial_number == 0U) {
		return -EINVAL;
	}

	ret = eeprom_store_init();
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	record = (struct dajilog_device_profile_record){
		.magic = DAJILOG_DEVICE_PROFILE_MAGIC,
		.format_version = DAJILOG_DEVICE_PROFILE_FORMAT_VERSION,
		.length = sizeof(record),
		.sequence = ctx->state.data.sequence + 1U,
		.serial_number = data->serial_number,
		.node_id = data->node_id,
		.hardware_revision = data->hardware_revision,
	};
	record.crc32 = profile_crc(&record);
	offset = (record.sequence & 1U) != 0U ?
		 DAJILOG_DEVICE_PROFILE_SLOT_A_OFFSET :
		 DAJILOG_DEVICE_PROFILE_SLOT_B_OFFSET;

	ret = eeprom_store_write(offset, &record, sizeof(record));
	if (ret == 0) {
		ret = eeprom_store_read(offset, &verify, sizeof(verify));
	}
	if (ret != 0 || !profile_record_is_valid(&verify) ||
	    memcmp(&record, &verify, sizeof(record)) != 0) {
		ctx->state.last_error = ret != 0 ? ret : -EIO;
		return ctx->state.last_error;
	}

	profile_state_from_record(&ctx->state, &record);
	return 0;
}

void dajilog_device_profile_get(const struct dajilog_device_profile *ctx,
				struct dajilog_device_profile_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
}
