/* SPDX-License-Identifier: Apache-2.0 */
/* Pure time/state logic, independent of Zephyr/GPIO. */
#include "scheduler_core.h"
#include <errno.h>
#include <string.h>
static void retire(struct scheduler_core *c, struct scheduler_event *e)
{
    c->history[c->history_count++ % SCHEDULER_HISTORY] = *e;
    e->state = EVENT_FREE;
}
void scheduler_core_init(struct scheduler_core *c) { memset(c, 0, sizeof(*c)); }
size_t scheduler_core_count(const struct scheduler_core *c, bool active)
{
    size_t n = 0;
    for (size_t i = 0; i < SCHEDULER_CAPACITY; i++) {
        n += c->events[i].state == (active ? EVENT_ACTIVE : EVENT_PENDING);
    }
    return n;
}
int scheduler_core_add(struct scheduler_core *c, uint16_t mask, int64_t rx,
                       uint32_t move, uint32_t hold)
{
    if (!mask || (mask & ~0x0fffU) || rx < 0 || move > 10000U ||
        hold < 1U || hold > 1000U || rx > INT64_MAX - move - hold) {
        return -EINVAL;
    }
    for (size_t i = 0; i < SCHEDULER_CAPACITY; i++) {
        if (c->events[i].state != EVENT_FREE) { continue; }
        c->events[i] = (struct scheduler_event) {
            .sequence = ++c->sequence, .mask = mask, .state = EVENT_PENDING,
            .rx_at = rx, .on_due = rx + move, .off_due = rx + move + hold,
            .on_at = -1, .off_at = -1, .cancelled_at = -1,
        };
        c->accepted++;
        size_t used = scheduler_core_count(c, false) + scheduler_core_count(c, true);
        if (used > c->peak) { c->peak = used; }
        return 0;
    }
    c->overflow++;
    return -ENOSPC;
}
uint16_t scheduler_core_step(struct scheduler_core *c, int64_t now)
{
    uint16_t mask = 0;
    /* Union all active events before GPIO: no glitch at adjacent boundaries. */
    for (size_t i = 0; i < SCHEDULER_CAPACITY; i++) {
        struct scheduler_event *e = &c->events[i];
        if (e->state == EVENT_PENDING && now >= e->off_due) {
            e->state = EVENT_MISSED; c->missed++; retire(c, e);
        } else if (e->state == EVENT_PENDING && now >= e->on_due) {
            e->state = EVENT_ACTIVE; e->on_at = now;
            if (now - e->on_due > c->max_on_late_ms) {
                c->max_on_late_ms = now - e->on_due;
            }
        }
        if (e->state == EVENT_ACTIVE && now >= e->off_due) {
            e->state = EVENT_COMPLETED; e->off_at = now; c->completed++;
            if (now - e->off_due > c->max_off_late_ms) {
                c->max_off_late_ms = now - e->off_due;
            }
            retire(c, e);
        }
        if (e->state == EVENT_ACTIVE) { mask |= e->mask; }
    }
    c->output_mask = mask;
    return mask;
}
void scheduler_core_cancel(struct scheduler_core *c, int64_t now)
{
    for (size_t i = 0; i < SCHEDULER_CAPACITY; i++) {
        struct scheduler_event *e = &c->events[i];
        if (e->state == EVENT_FREE) { continue; }
        if (e->state == EVENT_ACTIVE) { e->off_at = now; }
        e->cancelled_at = now;
        e->state = EVENT_CANCELLED; c->cancelled++;
        retire(c, e);
    }
    c->output_mask = 0;
}
int64_t scheduler_core_next(const struct scheduler_core *c)
{
    int64_t due = INT64_MAX;
    for (size_t i = 0; i < SCHEDULER_CAPACITY; i++) {
        const struct scheduler_event *e = &c->events[i];
        int64_t d = e->state == EVENT_PENDING ? e->on_due :
                    e->state == EVENT_ACTIVE ? e->off_due : INT64_MAX;
        if (d < due) { due = d; }
    }
    return due;
}
