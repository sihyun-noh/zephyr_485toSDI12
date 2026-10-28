/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SCHEDULER_CORE_H_
#define SCHEDULER_CORE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SCHEDULER_CAPACITY 32U
#define SCHEDULER_HISTORY 32U
enum scheduler_event_state { EVENT_FREE, EVENT_PENDING, EVENT_ACTIVE,
    EVENT_COMPLETED, EVENT_CANCELLED, EVENT_MISSED };
struct scheduler_event {
    uint32_t sequence;
    uint16_t mask;
    enum scheduler_event_state state;
    int64_t rx_at, on_due, off_due, on_at, off_at, cancelled_at;
};
struct scheduler_core {
    struct scheduler_event events[SCHEDULER_CAPACITY];
    struct scheduler_event history[SCHEDULER_HISTORY];
    uint32_t sequence, accepted, completed, cancelled, missed, overflow;
    uint32_t history_count;
    uint16_t output_mask;
    uint8_t peak;
    int64_t max_on_late_ms, max_off_late_ms;
};
void scheduler_core_init(struct scheduler_core *core);
int scheduler_core_add(struct scheduler_core *core, uint16_t mask,
                      int64_t rx_at, uint32_t move_ms, uint32_t hold_ms);
uint16_t scheduler_core_step(struct scheduler_core *core, int64_t now);
void scheduler_core_cancel(struct scheduler_core *core, int64_t now);
int64_t scheduler_core_next(const struct scheduler_core *core);
size_t scheduler_core_count(const struct scheduler_core *core, bool active);
#endif
