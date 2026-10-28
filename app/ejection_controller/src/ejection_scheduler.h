/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EJECTION_SCHEDULER_H_
#define EJECTION_SCHEDULER_H_
#include "scheduler_core.h"
struct ejection_scheduler_status {
    uint8_t pending, active, peak;
    uint32_t accepted, completed, cancelled, missed, overflow, rejected, io_errors;
    uint16_t mask;
    int64_t next_due, max_on_late_ms, max_off_late_ms;
};
void ejection_scheduler_init(void);
uint32_t ejection_scheduler_epoch(void);
void ejection_scheduler_invalidate(void);
int ejection_scheduler_submit(uint16_t mask, int64_t rx_at, uint32_t epoch);
int ejection_scheduler_cancel_all(void);
void ejection_scheduler_get_status(struct ejection_scheduler_status *status);
size_t ejection_scheduler_snapshot(struct scheduler_event *events, size_t capacity,
                                  bool history);
int ejection_scheduler_manual(size_t channel, bool on, uint32_t pulse_ms);
#endif
