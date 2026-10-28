/* SPDX-License-Identifier: Apache-2.0 */
#include "ejection_settings.h"
#include "controller_state.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <eeprom_store/eeprom_store.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>

#define SETTINGS_MAGIC 0x45433132U
#define SETTINGS_FORMAT_VERSION 2U
#define SETTINGS_SLOT_A_OFFSET 0x1000
#define SETTINGS_SLOT_B_OFFSET 0x1100

struct settings_record {
    uint32_t magic;
    uint16_t format_version, length;
    uint32_t sequence;
    struct ejection_settings_data data;
    uint32_t crc32;
};
struct legacy_data {
    uint32_t can_bitrate;
    uint16_t default_pulse_ms;
    uint8_t encoder_counts_per_step, reserved;
};
struct legacy_record {
    uint32_t magic;
    uint16_t format_version, length;
    uint32_t sequence;
    struct legacy_data data;
    uint32_t crc32;
};
BUILD_ASSERT(sizeof(struct legacy_record) == 24);
BUILD_ASSERT(sizeof(struct settings_record) < 0x100);
K_MUTEX_DEFINE(settings_mutex);
static struct ejection_settings_state settings_state;
static off_t last_slot;
static const struct ejection_settings_data default_settings = {
    .can_bitrate = 250000U, .default_pulse_ms = 50U, .encoder_counts_per_step = 4U,
    .conveyor_delay_ms = 500U, .ejection_hold_ms = 100U,
};
static bool editable(void)
{
    struct controller_state_snapshot state;
    controller_state_get(&state);
    return state.state == CONTROLLER_STATE_IDLE;
}
static bool valid(const struct ejection_settings_data *d)
{
    return (d->can_bitrate == 125000U || d->can_bitrate == 250000U ||
            d->can_bitrate == 500000U || d->can_bitrate == 1000000U) &&
           d->default_pulse_ms >= 5U && d->default_pulse_ms <= 1000U &&
           d->encoder_counts_per_step >= 1U && d->encoder_counts_per_step <= 16U &&
           d->conveyor_delay_ms <= 10000U &&
           d->ejection_hold_ms >= 1U && d->ejection_hold_ms <= 1000U;
}
static uint32_t checksum(const struct settings_record *r)
{
    return crc32_ieee((const uint8_t *)r, offsetof(struct settings_record, crc32));
}
static bool record_valid(const struct settings_record *r)
{
    return r->magic == SETTINGS_MAGIC && r->format_version == SETTINGS_FORMAT_VERSION &&
           r->length == sizeof(*r) && valid(&r->data) && r->crc32 == checksum(r);
}
static int read_record(off_t offset, struct settings_record *r, bool *migrated)
{
    int ret = eeprom_store_read(offset, r, sizeof(*r));
    if (ret != 0) { return ret; }
    *migrated = false;
    if (record_valid(r)) { return 0; }
    struct legacy_record old;
    memcpy(&old, r, sizeof(old));
    if (old.magic != SETTINGS_MAGIC || old.format_version != 1U ||
        old.length != sizeof(old) ||
        old.crc32 != crc32_ieee((const uint8_t *)&old,
                               offsetof(struct legacy_record, crc32))) {
        return -EBADMSG;
    }
    struct ejection_settings_data converted = default_settings;
    converted.can_bitrate = old.data.can_bitrate;
    converted.default_pulse_ms = old.data.default_pulse_ms;
    converted.encoder_counts_per_step = old.data.encoder_counts_per_step;
    if (!valid(&converted)) { return -EBADMSG; }
    *r = (struct settings_record) { .sequence = old.sequence, .data = converted };
    *migrated = true; /* Kept in RAM until an explicit save writes the other slot. */
    return 0;
}
int ejection_settings_defaults(void)
{
    k_mutex_lock(&settings_mutex, K_FOREVER);
    if (!editable()) {
        k_mutex_unlock(&settings_mutex); return -EBUSY;
    }
    settings_state.data = default_settings;
    settings_state.persistent = false;
    /* Keep sequence/last_slot so the next write supersedes the previous record. */
    k_mutex_unlock(&settings_mutex);
    return 0;
}
int ejection_settings_init(void)
{
    struct settings_record a, b;
    bool migrated_a = false, migrated_b = false;
    k_mutex_lock(&settings_mutex, K_FOREVER);
    memset(&settings_state, 0, sizeof(settings_state));
    settings_state.data = default_settings;
    int ret = eeprom_store_init();
    if (ret != 0) {
        settings_state.last_error = ret;
        k_mutex_unlock(&settings_mutex); return ret;
    }
    settings_state.ready = true;
    int ra = read_record(SETTINGS_SLOT_A_OFFSET, &a, &migrated_a);
    int rb = read_record(SETTINGS_SLOT_B_OFFSET, &b, &migrated_b);
    const struct settings_record *selected = NULL;
    bool migrated = false;
    if (ra == 0 && (rb != 0 || (int32_t)(a.sequence - b.sequence) >= 0)) {
        selected = &a; last_slot = SETTINGS_SLOT_A_OFFSET; migrated = migrated_a;
    } else if (rb == 0) {
        selected = &b; last_slot = SETTINGS_SLOT_B_OFFSET; migrated = migrated_b;
    }
    if (selected) {
        settings_state.data = selected->data;
        settings_state.sequence = selected->sequence;
        settings_state.persistent = !migrated;
    }
    k_mutex_unlock(&settings_mutex);
    return 0;
}
void ejection_settings_get(struct ejection_settings_state *state)
{
    if (state) {
        k_mutex_lock(&settings_mutex, K_FOREVER);
        *state = settings_state;
        k_mutex_unlock(&settings_mutex);
    }
}
int ejection_settings_set(const struct ejection_settings_data *data)
{
    if (!data || !valid(data)) { return -EINVAL; }
    k_mutex_lock(&settings_mutex, K_FOREVER);
    int ret = editable() ? 0 : -EBUSY;
    if (!ret) {
        settings_state.data = *data;
        settings_state.persistent = false;
    }
    k_mutex_unlock(&settings_mutex);
    return ret;
}
int ejection_settings_save(void)
{
    k_mutex_lock(&settings_mutex, K_FOREVER);
    if (!settings_state.ready || !editable()) {
        int ret = !settings_state.ready ? -ENODEV : -EBUSY;
        k_mutex_unlock(&settings_mutex); return ret;
    }
    struct settings_record record = {
        .magic = SETTINGS_MAGIC, .format_version = SETTINGS_FORMAT_VERSION,
        .length = sizeof(record), .sequence = settings_state.sequence + 1U,
        .data = settings_state.data,
    };
    record.crc32 = checksum(&record);
    off_t offset = last_slot == SETTINGS_SLOT_A_OFFSET ?
                   SETTINGS_SLOT_B_OFFSET : SETTINGS_SLOT_A_OFFSET;
    struct settings_record verify;
    int ret = eeprom_store_write(offset, &record, sizeof(record));
    if (ret == 0) { ret = eeprom_store_read(offset, &verify, sizeof(verify)); }
    if (!ret && (!record_valid(&verify) || memcmp(&record, &verify, sizeof(record)))) {
        ret = -EIO;
    }
    settings_state.last_error = ret;
    if (!ret) {
        settings_state.sequence = record.sequence;
        settings_state.persistent = true; last_slot = offset;
    }
    k_mutex_unlock(&settings_mutex);
    return ret;
}
