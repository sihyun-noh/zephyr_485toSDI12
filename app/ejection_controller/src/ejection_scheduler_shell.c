/* SPDX-License-Identifier: Apache-2.0 */
#include "ejection_scheduler.h"
#include "ejection_settings.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
static int status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    struct ejection_scheduler_status s;
    struct ejection_settings_state settings;
    ejection_scheduler_get_status(&s); ejection_settings_get(&settings);
    shell_print(sh, "pending=%u active=%u used=%u/32 peak=%u mask=0x%03x",
                s.pending, s.active, s.pending + s.active, s.peak, s.mask);
    shell_print(sh, "move_ms=%u hold_ms=%u next_in_ms=%lld",
                settings.data.conveyor_delay_ms, settings.data.ejection_hold_ms,
                s.next_due == INT64_MAX ? -1LL : s.next_due - k_uptime_get());
    shell_print(sh, "accepted=%u completed=%u cancelled=%u missed=%u",
                s.accepted, s.completed, s.cancelled, s.missed);
    shell_print(sh, "rejected=%u overflow=%u io_errors=%u max_on_late_ms=%lld max_off_late_ms=%lld",
                s.rejected, s.overflow, s.io_errors, s.max_on_late_ms, s.max_off_late_ms);
    return 0;
}
static struct scheduler_event snapshot[SCHEDULER_CAPACITY];

static const char *state_name(enum scheduler_event_state state)
{
    switch (state) {
    case EVENT_PENDING: return "PENDING";
    case EVENT_ACTIVE: return "ACTIVE";
    case EVENT_COMPLETED: return "DONE";
    case EVENT_CANCELLED: return "CANCELLED";
    case EVENT_MISSED: return "MISSED";
    default: return "UNKNOWN";
    }
}

static void print_history_event(const struct shell *sh, const struct scheduler_event *e,
                                int64_t previous_rx_at)
{
    int64_t planned_delay = e->on_due - e->rx_at;
    int64_t planned_hold = e->off_due - e->on_due;
    int64_t rx_gap = previous_rx_at < 0 ? -1 : e->rx_at - previous_rx_at;

    if (e->state == EVENT_COMPLETED) {
        shell_print(sh,
                    "#%u mask=%03x %-9s rx_gap=%lld ms delay=%lld/%lld ms hold=%lld/%lld ms late(on/off)=%+lld/%+lld ms",
                    e->sequence, e->mask, state_name(e->state), rx_gap,
                    e->on_at - e->rx_at, planned_delay,
                    e->off_at - e->on_at, planned_hold,
                    e->on_at - e->on_due, e->off_at - e->off_due);
    } else if (e->state == EVENT_CANCELLED) {
        shell_print(sh,
                    "#%u mask=%03x %-9s rx_gap=%lld ms after_rx=%lld ms phase=%s",
                    e->sequence, e->mask, state_name(e->state), rx_gap,
                    e->cancelled_at - e->rx_at,
                    e->on_at < 0 ? "before ON" : "after ON");
    } else if (e->state == EVENT_MISSED) {
        shell_print(sh,
                    "#%u mask=%03x %-9s rx_gap=%lld ms planned_delay=%lld ms planned_hold=%lld ms",
                    e->sequence, e->mask, state_name(e->state), rx_gap,
                    planned_delay, planned_hold);
    } else {
        shell_print(sh, "#%u mask=%03x %s rx_gap=%lld ms", e->sequence,
                    e->mask, state_name(e->state), rx_gap);
    }
}

static int list(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    bool history = strcmp(argv[0], "history") == 0;
    size_t n = ejection_scheduler_snapshot(snapshot, ARRAY_SIZE(snapshot), history);

    if (history) {
        shell_print(sh, "units=ms; rx_gap=interval from previous RX (-1 for first)");
        shell_print(sh, "actual/planned; late=software dispatch minus due time");
        shell_print(sh, "delay/hold are software times, not measured physical edges");
    } else {
        shell_print(sh, "state: 1=pending 2=active 3=done 4=cancelled 5=missed; -1=unset");
        shell_print(sh, "T1/T2: software dispatch times, not measured physical edges");
    }
    for (size_t i = 0; i < n; i++) {
        struct scheduler_event *e = &snapshot[i];
        if (history) {
            print_history_event(sh, e, i == 0 ? -1 : snapshot[i - 1].rx_at);
        } else {
            shell_print(sh, "#%u mask=%03x state=%u T0=%lld on_due=%lld off_due=%lld T1=%lld T2=%lld cancel_at=%lld",
                        e->sequence, e->mask, e->state, e->rx_at, e->on_due,
                        e->off_due, e->on_at, e->off_at, e->cancelled_at);
        }
    }
    return 0;
}
static int clear(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc); ARG_UNUSED(argv);
    int ret = ejection_scheduler_cancel_all();
    shell_print(sh, "cancel/all-off: %d", ret); return ret;
}
SHELL_STATIC_SUBCMD_SET_CREATE(scheduler_commands,
    SHELL_CMD(status, NULL, "Queue, timing and counters", status),
    SHELL_CMD(stats, NULL, "Queue, timing and counters", status),
    SHELL_CMD(list, NULL, "Pending and active events", list),
    SHELL_CMD(history, NULL, "Last 32 completed/cancelled/missed events", list),
    SHELL_CMD(clear, NULL, "Cancel all and turn outputs off", clear),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(scheduler, &scheduler_commands, "Queued ejection", NULL);
