/* SPDX-License-Identifier: Apache-2.0 */

#include "ejection_settings.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <eeprom_store/eeprom_store.h>
#include <zephyr/sys/crc.h>

#define SETTINGS_MAGIC 0x45433132U
#define SETTINGS_FORMAT_VERSION 1U
#define SETTINGS_SLOT_A_OFFSET 0x1000
#define SETTINGS_SLOT_B_OFFSET 0x1100

struct settings_record {
	uint32_t magic;
	uint16_t format_version;
	uint16_t length;
	uint32_t sequence;
	struct ejection_settings_data data;
	uint32_t crc32;
};

static struct ejection_settings_state settings_state;

static const struct ejection_settings_data default_settings = {
	.can_bitrate = 250000U,
	.default_pulse_ms = 50U,
	.encoder_counts_per_step = 4U,
};

static bool settings_data_is_valid(const struct ejection_settings_data *data)
{
	return (data->can_bitrate == 250000U || data->can_bitrate == 500000U) &&
	       data->default_pulse_ms >= 5U && data->default_pulse_ms <= 1000U &&
	       data->encoder_counts_per_step >= 1U &&
	       data->encoder_counts_per_step <= 16U;
}

static uint32_t settings_crc(const struct settings_record *record)
{
	return crc32_ieee((const uint8_t *)record,
			  offsetof(struct settings_record, crc32));
}

static bool record_is_valid(const struct settings_record *record)
{
	return record->magic == SETTINGS_MAGIC &&
	       record->format_version == SETTINGS_FORMAT_VERSION &&
	       record->length == sizeof(*record) &&
	       settings_data_is_valid(&record->data) &&
	       record->crc32 == settings_crc(record);
}

static int read_record(off_t offset, struct settings_record *record)
{
	int ret = eeprom_store_read(offset, record, sizeof(*record));
	if (ret != 0) {
		return ret;
	}
	return record_is_valid(record) ? 0 : -EBADMSG;
}

void ejection_settings_defaults(void)
{
	settings_state.data = default_settings;
	settings_state.persistent = false;
	settings_state.sequence = 0U;
	settings_state.last_error = 0;
}

int ejection_settings_init(void)
{
	struct settings_record slot_a;
	struct settings_record slot_b;
	const struct settings_record *selected = NULL;
	int ret = eeprom_store_init();

	settings_state = (struct ejection_settings_state){0};
	if (ret != 0) {
		settings_state.last_error = ret;
		return ret;
	}
	settings_state.ready = true;
	int ret_a = read_record(SETTINGS_SLOT_A_OFFSET, &slot_a);
	int ret_b = read_record(SETTINGS_SLOT_B_OFFSET, &slot_b);
	if (ret_a == 0 && (ret_b != 0 || slot_a.sequence >= slot_b.sequence)) {
		selected = &slot_a;
	} else if (ret_b == 0) {
		selected = &slot_b;
	}

	if (selected == NULL) {
		ejection_settings_defaults();
		settings_state.ready = true;
		return 0;
	}

	settings_state.persistent = true;
	settings_state.sequence = selected->sequence;
	settings_state.data = selected->data;
	return 0;
}

void ejection_settings_get(struct ejection_settings_state *state)
{
	if (state != NULL) {
		*state = settings_state;
	}
}

int ejection_settings_set(const struct ejection_settings_data *data)
{
	if (data == NULL || !settings_data_is_valid(data)) {
		return -EINVAL;
	}
	settings_state.data = *data;
	settings_state.persistent = false;
	return 0;
}

int ejection_settings_save(void)
{
	if (!settings_state.ready) {
		return -ENODEV;
	}

	struct settings_record record = {
		.magic = SETTINGS_MAGIC,
		.format_version = SETTINGS_FORMAT_VERSION,
		.length = sizeof(record),
		.sequence = settings_state.sequence + 1U,
		.data = settings_state.data,
	};
	record.crc32 = settings_crc(&record);
	off_t offset = (record.sequence & 1U) != 0U ?
		       SETTINGS_SLOT_A_OFFSET : SETTINGS_SLOT_B_OFFSET;
	struct settings_record verify;
	int ret = eeprom_store_write(offset, &record, sizeof(record));
	if (ret == 0) {
		ret = eeprom_store_read(offset, &verify, sizeof(verify));
	}
	if (ret != 0 || !record_is_valid(&verify) ||
	    memcmp(&record, &verify, sizeof(record)) != 0) {
		settings_state.last_error = ret != 0 ? ret : -EIO;
		return settings_state.last_error;
	}

	settings_state.sequence = record.sequence;
	settings_state.persistent = true;
	settings_state.last_error = 0;
	return 0;
}
