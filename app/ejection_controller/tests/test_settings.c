/* SPDX-License-Identifier: Apache-2.0 */
#include "ejection_settings.h"
#include "controller_state.h"
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
static uint8_t memory[8192];
static enum controller_state state = CONTROLLER_STATE_IDLE;
static bool fail_write;
uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t crc = ~0U;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
    }
    return ~crc;
}
int eeprom_store_init(void) { return 0; }
int eeprom_store_read(off_t off, void *data, size_t size)
{ assert(off >= 0 && off + size <= sizeof(memory)); memcpy(data, memory + off, size); return 0; }
int eeprom_store_write(off_t off, const void *data, size_t size)
{
    assert(off >= 0 && off + size <= sizeof(memory));
    memcpy(memory + off, data, fail_write ? 4 : size);
    return fail_write ? -EIO : 0;
}
void controller_state_get(struct controller_state_snapshot *s) { s->state = state; }
static void seed_legacy(void)
{
    struct legacy {
        uint32_t magic; uint16_t version, size; uint32_t seq;
        uint32_t bitrate; uint16_t pulse; uint8_t cps, reserved; uint32_t crc;
    } old = { .magic = 0x45433132, .version = 1, .size = 24, .seq = 100,
              .bitrate = 500000, .pulse = 70, .cps = 2 };
    _Static_assert(sizeof(old) == 24, "v1 layout");
    old.crc = crc32_ieee((const uint8_t *)&old, offsetof(struct legacy, crc));
    memcpy(memory + 0x1000, &old, sizeof(old));
}
int main(void)
{
    struct ejection_settings_state s;
    memset(memory, 0xff, sizeof(memory)); seed_legacy();
    assert(ejection_settings_init() == 0);
    ejection_settings_get(&s);
    assert(s.ready && !s.persistent && s.sequence == 100);
    assert(s.data.can_bitrate == 500000 && s.data.default_pulse_ms == 70 &&
           s.data.encoder_counts_per_step == 2);
    assert(s.data.conveyor_delay_ms == 500 && s.data.ejection_hold_ms == 100);
    uint8_t original[24]; memcpy(original, memory + 0x1000, sizeof(original));
    assert(ejection_settings_save() == 0);
    assert(memcmp(original, memory + 0x1000, sizeof(original)) == 0);
    assert(ejection_settings_init() == 0); ejection_settings_get(&s);
    assert(s.persistent && s.sequence == 101);
    s.data.conveyor_delay_ms = 900; s.data.ejection_hold_ms = 40;
    assert(ejection_settings_set(&s.data) == 0);
    fail_write = true;
    assert(ejection_settings_save() == -EIO);
    fail_write = false;
    assert(ejection_settings_init() == 0); ejection_settings_get(&s);
    assert(s.data.conveyor_delay_ms == 500 && s.sequence == 101);
    state = CONTROLLER_STATE_RUNNING;
    assert(ejection_settings_set(&s.data) == -EBUSY);
    assert(ejection_settings_save() == -EBUSY);
    assert(ejection_settings_defaults() == -EBUSY);
    state = CONTROLLER_STATE_IDLE;
    assert(ejection_settings_defaults() == 0);
    assert(ejection_settings_save() == 0);
    assert(ejection_settings_init() == 0); ejection_settings_get(&s);
    assert(s.data.can_bitrate == 250000 && s.sequence == 102);
    s.data.ejection_hold_ms = 0;
    assert(ejection_settings_set(&s.data) == -EINVAL);
    puts("settings: v1 migration, A/B save, torn write recovery, IDLE gate, defaults sequence PASS");
    return 0;
}
