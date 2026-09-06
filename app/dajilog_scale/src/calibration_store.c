/* SPDX-License-Identifier: Apache-2.0 */

#include "calibration_store.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/crc.h>

#include <eeprom_store/eeprom_store.h>

#define CALIBRATION_MAGIC 0x444a4341U
#define CALIBRATION_FORMAT_VERSION 1U
#define CALIBRATION_SLOT_A_OFFSET 0x0000
#define CALIBRATION_SLOT_B_OFFSET 0x0100

struct calibration_record {
	uint32_t magic;
	uint16_t format_version;
	uint16_t reserved;
	uint32_t sequence;
	int32_t zero_raw;
	int32_t span_raw;
	int32_t reference_weight_mg;
	uint32_t crc32;
};

static uint32_t record_crc(const struct calibration_record *record)
{
	return crc32_ieee((const uint8_t *)record,
			  offsetof(struct calibration_record, crc32));
}

static bool record_is_valid(const struct calibration_record *record)
{
	return record->magic == CALIBRATION_MAGIC &&
	       record->format_version == CALIBRATION_FORMAT_VERSION &&
	       record->reference_weight_mg > 0 &&
	       record->span_raw != record->zero_raw &&
	       record->crc32 == record_crc(record);
}

static int read_record(off_t offset, struct calibration_record *record)
{
	int ret = eeprom_store_read(offset, record, sizeof(*record));

	if (ret != 0 || !record_is_valid(record)) {
		return ret != 0 ? ret : -EBADMSG;
	}

	return 0;
}

int calibration_store_load(struct scale_calibration *calibration)
{
	struct calibration_record slot_a;
	struct calibration_record slot_b;
	const struct calibration_record *selected = NULL;
	int ret_a;
	int ret_b;

	if (calibration == NULL) {
		return -EINVAL;
	}

	ret_a = read_record(CALIBRATION_SLOT_A_OFFSET, &slot_a);
	ret_b = read_record(CALIBRATION_SLOT_B_OFFSET, &slot_b);
	if (ret_a == 0 && (ret_b != 0 || slot_a.sequence >= slot_b.sequence)) {
		selected = &slot_a;
	} else if (ret_b == 0) {
		selected = &slot_b;
	} else {
		return -ENOENT;
	}

	calibration->valid = true;
	calibration->zero_raw = selected->zero_raw;
	calibration->span_raw = selected->span_raw;
	calibration->reference_weight_mg = selected->reference_weight_mg;
	calibration->sequence = selected->sequence;
	return 0;
}

int calibration_store_save(struct scale_calibration *calibration)
{
	struct calibration_record record = {
		.magic = CALIBRATION_MAGIC,
		.format_version = CALIBRATION_FORMAT_VERSION,
		.sequence = calibration->sequence + 1U,
		.zero_raw = calibration->zero_raw,
		.span_raw = calibration->span_raw,
		.reference_weight_mg = calibration->reference_weight_mg,
	};
	struct calibration_record verify;
	off_t offset;
	int ret;

	if (calibration == NULL || !calibration->valid ||
	    calibration->reference_weight_mg <= 0 ||
	    calibration->span_raw == calibration->zero_raw) {
		return -EINVAL;
	}

	record.crc32 = record_crc(&record);
	offset = (record.sequence & 1U) != 0U ? CALIBRATION_SLOT_A_OFFSET :
						     CALIBRATION_SLOT_B_OFFSET;

	ret = eeprom_store_write(offset, &record, sizeof(record));
	if (ret != 0) {
		return ret;
	}
	ret = eeprom_store_read(offset, &verify, sizeof(verify));
	if (ret != 0) {
		return ret;
	}
	if (!record_is_valid(&verify) || memcmp(&record, &verify, sizeof(record)) != 0) {
		return -EIO;
	}

	calibration->sequence = record.sequence;
	return 0;
}
