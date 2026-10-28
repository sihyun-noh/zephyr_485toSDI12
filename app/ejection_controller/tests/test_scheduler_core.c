/* SPDX-License-Identifier: Apache-2.0 */
#include "scheduler_core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

static void timing_and_overlap(void)
{
    struct scheduler_core c;
    scheduler_core_init(&c);
    assert(scheduler_core_add(&c, 1, 0, 500, 100) == 0);
    assert(scheduler_core_add(&c, 2, 40, 500, 100) == 0);
    assert(scheduler_core_add(&c, 5, 80, 500, 100) == 0);
    assert(scheduler_core_step(&c, 499) == 0);
    assert(scheduler_core_step(&c, 500) == 1);
    assert(scheduler_core_step(&c, 540) == 3);
    assert(scheduler_core_step(&c, 580) == 7);
    assert(scheduler_core_step(&c, 600) == 7);
    assert(scheduler_core_step(&c, 640) == 5);
    assert(scheduler_core_step(&c, 680) == 0);
    assert(c.completed == 3 && c.history_count == 3);
    assert(c.history[0].rx_at == 0 && c.history[0].on_at == 500 &&
           c.history[0].off_at == 600);
    assert(scheduler_core_next(&c) == INT64_MAX);
}
static void capacity_and_cancel(void)
{
    struct scheduler_core c;
    scheduler_core_init(&c);
    for (int i = 0; i < 32; i++) {
        assert(scheduler_core_add(&c, 1, i, 1000, 100) == 0);
    }
    assert(scheduler_core_add(&c, 2, 33, 1000, 100) == -ENOSPC);
    assert(c.accepted == 32 && c.overflow == 1 && c.peak == 32);
    assert(scheduler_core_step(&c, 1000) == 1);
    assert(scheduler_core_add(&c, 2, 1000, 1, 10) == -ENOSPC);
    scheduler_core_cancel(&c, 1001);
    assert(c.cancelled == 32 && c.output_mask == 0);
    assert(scheduler_core_step(&c, 5000) == 0);
    assert(scheduler_core_add(&c, 0x800, 5000, 0, 10) == 0);
    assert(scheduler_core_step(&c, 5000) == 0x800);
}
static void boundaries_and_lateness(void)
{
    struct scheduler_core c;
    scheduler_core_init(&c);
    assert(scheduler_core_add(&c, 1, 0, 0, 100) == 0);
    assert(scheduler_core_add(&c, 1, 100, 0, 100) == 0);
    assert(scheduler_core_step(&c, 0) == 1);
    assert(scheduler_core_step(&c, 100) == 1);
    assert(scheduler_core_step(&c, 205) == 0);
    assert(c.max_off_late_ms == 5);
    assert(scheduler_core_add(&c, 4, 300, 0, 10) == 0);
    assert(scheduler_core_step(&c, 310) == 0);
    assert(c.missed == 1);
    assert(scheduler_core_add(&c, 4, 400, 0, 10) == 0);
    assert(scheduler_core_step(&c, 403) == 4);
    assert(c.max_on_late_ms == 3);
    assert(scheduler_core_step(&c, 410) == 0);
    assert(scheduler_core_add(&c, 0, 500, 0, 1) == -EINVAL);
    assert(scheduler_core_add(&c, 0x1000, 500, 0, 1) == -EINVAL);
    assert(scheduler_core_add(&c, 1, -1, 0, 1) == -EINVAL);
    assert(scheduler_core_add(&c, 1, 500, 0, 0) == -EINVAL);
    assert(scheduler_core_add(&c, 1, INT64_MAX, 1, 1) == -EINVAL);
}
static void randomized_reference(void)
{
    struct scheduler_core c;
    struct { int64_t start, end; uint16_t mask; } reference[2000];
    size_t n = 0;
    uint32_t random = 31;
    scheduler_core_init(&c);
    for (int64_t now = 0; now < 4000; now++) {
        if (now < 2000) {
            random = random * 1664525U + 1013904223U;
            uint32_t move = (random >> 8) % 80;
            uint32_t hold = (random >> 16) % 60 + 1;
            uint16_t mask = (random & 0xfff) | 1;
            int ret = scheduler_core_add(&c, mask, now, move, hold);
            if (ret == 0) {
                reference[n].start = now + move;
                reference[n].end = now + move + hold;
                reference[n++].mask = mask;
            } else { assert(ret == -ENOSPC); }
        }
        uint16_t expected = 0;
        for (size_t i = 0; i < n; i++) {
            if (reference[i].start <= now && now < reference[i].end) {
                expected |= reference[i].mask;
            }
        }
        assert(scheduler_core_step(&c, now) == expected);
        assert(scheduler_core_count(&c, true) + scheduler_core_count(&c, false) <= 32);
    }
    assert(c.completed == n && c.missed == 0);
}
int main(void)
{
    timing_and_overlap(); capacity_and_cancel();
    boundaries_and_lateness(); randomized_reference();
    puts("scheduler core: timing, overlap, 32/33, cancel, boundaries, lateness, reference PASS");
    return 0;
}
