/* SPDX-License-Identifier: Apache-2.0 */
/* Tests public registration/cancellation contracts; fake kernel does not run threads. */
#include "ejection_scheduler.h"
#include "ejection_io.h"
#include "ejection_settings.h"
#include "controller_state.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static bool running;
static uint16_t pins;
int64_t test_now;
bool controller_state_outputs_allowed(void) { return running; }
int ejection_output_all_off(void) { pins = 0; return 0; }
bool ejection_output_any_active(void) { return pins != 0; }
int ejection_output_apply_mask(uint16_t m) { pins = m; return 0; }
int ejection_output_set(size_t c, bool on)
{
    if (on && !running) { return -EACCES; }
    if (on) { pins |= 1U << c; } else { pins &= ~(1U << c); }
    return 0;
}
int ejection_output_pulse(size_t c, uint32_t ms)
{ (void)ms; return ejection_output_set(c, true); }
int ejection_output_manual_set(size_t c, bool on)
{ return ejection_output_set(c, on); }
int ejection_output_manual_pulse(size_t c, uint32_t ms)
{ return ejection_output_pulse(c, ms); }
void ejection_settings_get(struct ejection_settings_state *s)
{
    s->data.conveyor_delay_ms = 500;
    s->data.ejection_hold_ms = 100;
}
int main(void)
{
    struct ejection_scheduler_status s;
    assert(ejection_scheduler_submit(1, 0, 0) == -EAGAIN);
    ejection_scheduler_init();
    uint32_t epoch = ejection_scheduler_epoch();
    assert(ejection_scheduler_submit(1, 0, epoch) == -EACCES);
    running = true;
    assert(ejection_scheduler_manual(0, true, 0) == 0);
    assert(ejection_scheduler_submit(2, 0, epoch) == -EBUSY);
    assert(ejection_scheduler_manual(0, false, 0) == 0);
    for (int i = 0; i < 32; i++) {
        assert(ejection_scheduler_submit(1, i, epoch) == 0);
    }
    assert(ejection_scheduler_manual(0, true, 0) == -EBUSY);
    assert(ejection_scheduler_submit(1, 32, epoch) == -ENOSPC);
    ejection_scheduler_get_status(&s);
    assert(s.pending == 32 && s.accepted == 32 && s.overflow == 1);
    assert(ejection_scheduler_cancel_all() == 0);
    assert(pins == 0);
    ejection_scheduler_get_status(&s);
    assert(s.pending == 0 && s.cancelled == 32);
    assert(ejection_scheduler_submit(1, 40, epoch) == -ECANCELED);
    epoch = ejection_scheduler_epoch();
    assert(ejection_scheduler_submit(1, 40, epoch) == 0);
    ejection_scheduler_invalidate(); /* FSM change rejects buffered previous epoch. */
    assert(ejection_scheduler_submit(1, 50, epoch) == -ECANCELED);
    assert(ejection_scheduler_cancel_all() == 0);
    struct scheduler_event history[16];
    size_t n = ejection_scheduler_snapshot(history, 16, true);
    assert(n == 16);
    for (size_t i = 0; i < n; i++) {
        assert(history[i].state == EVENT_CANCELLED && history[i].on_at == -1 &&
               history[i].off_at == -1 && history[i].cancelled_at == test_now);
    }
    puts("scheduler API: state/epoch gates, 32 slots, manual ownership, cancellation PASS");
    return 0;
}
