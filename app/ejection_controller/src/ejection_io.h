/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EJECTION_IO_H_
#define EJECTION_IO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EJECTION_OUTPUT_COUNT 12U
#define EJECTION_INPUT_COUNT 14U
#define EJECTION_PULSE_MIN_MS 5U
#define EJECTION_PULSE_MAX_MS 1000U

typedef void (*ejection_io_change_callback_t)(void *user_data);

int ejection_io_init(void);
int ejection_output_set(size_t channel, bool active);
int ejection_output_pulse(size_t channel, uint32_t duration_ms);
int ejection_output_apply_mask(uint16_t mask);
int ejection_output_all_off(void);
uint16_t ejection_output_mask_get(void);
bool ejection_output_get(size_t channel);
bool ejection_output_any_active(void);
bool ejection_input_get(size_t channel);
void ejection_io_set_change_callback(ejection_io_change_callback_t callback,
				     void *user_data);

#endif /* EJECTION_IO_H_ */
