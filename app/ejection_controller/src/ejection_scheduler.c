/* SPDX-License-Identifier: Apache-2.0 */
#include "ejection_scheduler.h"
#include "controller_state.h"
#include "ejection_io.h"
#include "ejection_settings.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
K_MUTEX_DEFINE(scheduler_mutex);
K_SEM_DEFINE(scheduler_wake, 0, 1);
K_THREAD_STACK_DEFINE(scheduler_stack, 1536);
static struct k_thread scheduler_thread;
static struct scheduler_core core;
static atomic_t epoch;
static bool initialized;
static uint32_t rejected, io_errors;
static uint32_t observed_epoch;
uint32_t ejection_scheduler_epoch(void) { return (uint32_t)atomic_get(&epoch); }
void ejection_scheduler_invalidate(void)
{
    atomic_inc(&epoch);
    k_sem_give(&scheduler_wake);
}
static int cancel_locked(void)
{
    scheduler_core_cancel(&core, k_uptime_get());
    return ejection_output_all_off();
}
static void reconcile_epoch_locked(void)
{
    uint32_t current = ejection_scheduler_epoch();
    if (observed_epoch != current) {
        (void)cancel_locked();
        observed_epoch = current;
    }
}
int ejection_scheduler_cancel_all(void)
{
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    ejection_scheduler_invalidate();
    int ret = cancel_locked();
    observed_epoch = ejection_scheduler_epoch();
    k_mutex_unlock(&scheduler_mutex);
    return ret;
}
int ejection_scheduler_submit(uint16_t mask, int64_t rx_at, uint32_t expected_epoch)
{
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    reconcile_epoch_locked();
    int ret;
    if (!initialized) { ret = -EAGAIN; }
    else if (expected_epoch != ejection_scheduler_epoch()) { ret = -ECANCELED; }
    else if (!controller_state_outputs_allowed()) { ret = -EACCES; }
    else if (scheduler_core_count(&core, true) == 0 && ejection_output_any_active()) {
        ret = -EBUSY; /* Manual command currently owns the pins. */
    } else {
        struct ejection_settings_state settings;
        ejection_settings_get(&settings);
        ret = scheduler_core_add(&core, mask, rx_at, settings.data.conveyor_delay_ms,
                                 settings.data.ejection_hold_ms);
    }
    if (ret != 0) { rejected++; }
    k_mutex_unlock(&scheduler_mutex);
    if (ret == 0) { k_sem_give(&scheduler_wake); }
    return ret;
}
static void run(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    while (true) {
        k_mutex_lock(&scheduler_mutex, K_FOREVER);
        reconcile_epoch_locked();
        uint16_t previous = core.output_mask;
        size_t count = scheduler_core_count(&core, false) + scheduler_core_count(&core, true);
        if (count && !controller_state_outputs_allowed()) {
            (void)cancel_locked();
        } else if (count) {
            uint16_t mask = scheduler_core_step(&core, k_uptime_get());
            if (mask != previous && ejection_output_apply_mask(mask) != 0) {
                io_errors++;
                ejection_scheduler_invalidate();
                (void)cancel_locked();
            }
        }
        int64_t due = scheduler_core_next(&core);
        int64_t remaining = due == INT64_MAX ? 0 : due - k_uptime_get();
        k_mutex_unlock(&scheduler_mutex);
        (void)k_sem_take(&scheduler_wake, due == INT64_MAX ? K_FOREVER :
                        K_MSEC(MAX(remaining, 0)));
    }
}
void ejection_scheduler_init(void)
{
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    if (!initialized) {
        scheduler_core_init(&core); initialized = true;
        observed_epoch = ejection_scheduler_epoch();
        k_thread_create(&scheduler_thread, scheduler_stack,
                        K_THREAD_STACK_SIZEOF(scheduler_stack), run,
                        NULL, NULL, NULL, 3, 0, K_NO_WAIT);
        k_thread_name_set(&scheduler_thread, "ejection_schedule");
    }
    k_mutex_unlock(&scheduler_mutex);
}
void ejection_scheduler_get_status(struct ejection_scheduler_status *s)
{
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    *s = (struct ejection_scheduler_status) {
        .pending = scheduler_core_count(&core, false),
        .active = scheduler_core_count(&core, true), .peak = core.peak,
        .accepted = core.accepted, .completed = core.completed,
        .cancelled = core.cancelled, .missed = core.missed, .overflow = core.overflow,
        .rejected = rejected, .io_errors = io_errors, .mask = core.output_mask,
        .next_due = scheduler_core_next(&core),
        .max_on_late_ms = core.max_on_late_ms, .max_off_late_ms = core.max_off_late_ms,
    };
    k_mutex_unlock(&scheduler_mutex);
}
size_t ejection_scheduler_snapshot(struct scheduler_event *out, size_t cap, bool history)
{
    size_t n = 0;
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    if (history) {
        size_t count = MIN(core.history_count, SCHEDULER_HISTORY);
        for (size_t i = 0; i < count && n < cap; i++) {
            out[n++] = core.history[(core.history_count - count + i) % SCHEDULER_HISTORY];
        }
    } else {
        for (size_t i = 0; i < SCHEDULER_CAPACITY && n < cap; i++) {
            if (core.events[i].state != EVENT_FREE) { out[n++] = core.events[i]; }
        }
    }
    k_mutex_unlock(&scheduler_mutex);
    for (size_t i = 1; i < n; i++) {
        struct scheduler_event e = out[i];
        size_t j = i;
        while (j && (int32_t)(out[j - 1].sequence - e.sequence) > 0) {
            out[j] = out[j - 1]; j--;
        }
        out[j] = e;
    }
    return n;
}
int ejection_scheduler_manual(size_t channel, bool on, uint32_t pulse_ms)
{
    k_mutex_lock(&scheduler_mutex, K_FOREVER);
    reconcile_epoch_locked();
    int ret;
    if (scheduler_core_count(&core, false) + scheduler_core_count(&core, true)) {
        ret = -EBUSY;
    } else {
        ret = pulse_ms ? ejection_output_manual_pulse(channel, pulse_ms) :
                        ejection_output_manual_set(channel, on);
    }
    k_mutex_unlock(&scheduler_mutex);
    return ret;
}
