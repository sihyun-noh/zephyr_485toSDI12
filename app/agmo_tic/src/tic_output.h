/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AGMO_TIC_OUTPUT_H_
#define AGMO_TIC_OUTPUT_H_

#include <stdbool.h>

#include "tic_types.h"

enum tic_led_id {
	TIC_LED_RESTART,
	TIC_LED_ACC,
	TIC_LED_MANUAL,
	TIC_LED_KEY,
	TIC_LED_COUNT,
};

int tic_output_init(void);
int tic_output_set_r1(bool on);
int tic_output_set_r2(bool on);
int tic_output_set_r3(bool on);
int tic_output_set_r4(bool on);
int tic_output_all_off(void);
void tic_output_sync_auto_leds(bool key_input_on);
int tic_output_led_set(enum tic_led_id id, bool on);
int tic_output_led_set_all(bool on);
void tic_output_led_auto_enable(bool enable, bool key_input_on);
void tic_output_get_snapshot(struct tic_output_snapshot *out);

#endif /* AGMO_TIC_OUTPUT_H_ */
