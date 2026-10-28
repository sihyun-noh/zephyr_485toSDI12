/* SPDX-License-Identifier: Apache-2.0 */

#include "manual_control.h"

#include "controller_state.h"
#include "ejection_io.h"
#include "ejection_scheduler.h"
#include "ejection_settings.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#define IO_NODE DT_NODELABEL(ejection_io)
#define MANUAL_SELECTOR_INPUT DT_PROP(IO_NODE, manual_selector_input)
#define MANUAL_BUTTON_COUNT DT_PROP(IO_NODE, manual_button_count)
#define MANUAL_POLL_MS 10
#define MANUAL_THREAD_STACK_SIZE 1024
#define MANUAL_THREAD_PRIORITY 4

BUILD_ASSERT(MANUAL_BUTTON_COUNT == EJECTION_OUTPUT_COUNT,
	     "Manual buttons must map one-to-one to all outputs");
BUILD_ASSERT(MANUAL_SELECTOR_INPUT < EJECTION_INPUT_COUNT,
	     "Manual selector input index is outside the input array");

static struct manual_control_status current;
static atomic_t active;
static struct k_thread manual_thread_data;
K_THREAD_STACK_DEFINE(manual_thread_stack, MANUAL_THREAD_STACK_SIZE);
K_MUTEX_DEFINE(manual_mutex);

static uint16_t read_button_mask(void)
{
	uint16_t mask = 0U;

	for (size_t i = 0; i < MANUAL_BUTTON_COUNT; i++) {
		if (ejection_input_get(i)) {
			mask |= BIT(i);
		}
	}
	return mask;
}

static void status_mode(bool is_active, bool armed, uint16_t buttons)
{
	k_mutex_lock(&manual_mutex, K_FOREVER);
	current.active = is_active;
	current.armed = armed;
	current.button_mask = buttons;
	k_mutex_unlock(&manual_mutex);
}

static void enter_manual(uint16_t buttons)
{
	/* Block new automatic commands before cancelling their output ownership. */
	atomic_set(&active, 1);
	struct controller_state_snapshot state;
	controller_state_get(&state);
	if (state.state == CONTROLLER_STATE_READY ||
	    state.state == CONTROLLER_STATE_RUNNING) {
		(void)controller_state_transition(CONTROLLER_STATE_IDLE);
	}
	(void)ejection_scheduler_cancel_all();
	status_mode(true, buttons == 0U, buttons);
}

static void leave_manual(uint16_t buttons)
{
	/* Keep automatic commands blocked until every manual output is off. */
	(void)ejection_scheduler_cancel_all();
	status_mode(false, false, buttons);
	atomic_clear(&active);
}

static void record_result(size_t channel, int ret)
{
	k_mutex_lock(&manual_mutex, K_FOREVER);
	current.last_channel = (int8_t)channel;
	current.last_error = ret;
	if (ret == 0) {
		current.trigger_count++;
	} else {
		current.rejected_count++;
	}
	k_mutex_unlock(&manual_mutex);
}

static void manual_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	bool was_manual = false;
	bool armed = false;
	uint16_t previous_buttons = read_button_mask();

	while (true) {
		bool is_manual = ejection_input_get(MANUAL_SELECTOR_INPUT);
		uint16_t buttons = read_button_mask();

		if (is_manual != was_manual) {
			if (is_manual) {
				enter_manual(buttons);
				armed = buttons == 0U;
			} else {
				leave_manual(buttons);
				armed = false;
			}
			was_manual = is_manual;
			previous_buttons = buttons;
		}

		if (is_manual) {
			struct controller_state_snapshot state;
			controller_state_get(&state);
			if (state.state == CONTROLLER_STATE_READY ||
			    state.state == CONTROLLER_STATE_RUNNING) {
				(void)controller_state_transition(CONTROLLER_STATE_IDLE);
			}

			if (!armed && buttons == 0U) {
				armed = true;
			}
			if (armed) {
				uint16_t rising = buttons & ~previous_buttons;
				if (rising != 0U) {
					struct ejection_settings_state settings;
					ejection_settings_get(&settings);
					for (size_t i = 0; i < MANUAL_BUTTON_COUNT; i++) {
						if ((rising & BIT(i)) != 0U) {
							int ret = ejection_scheduler_manual(
								i, true, settings.data.ejection_hold_ms);
							record_result(i, ret);
						}
					}
				}
			}
			status_mode(true, armed, buttons);
		}

		previous_buttons = buttons;
		k_sleep(K_MSEC(MANUAL_POLL_MS));
	}
}

int manual_control_init(void)
{
	k_mutex_lock(&manual_mutex, K_FOREVER);
	if (current.initialized) {
		k_mutex_unlock(&manual_mutex);
		return -EALREADY;
	}
	current.initialized = true;
	current.last_channel = -1;
	current.last_error = 0;
	k_mutex_unlock(&manual_mutex);

	k_thread_create(&manual_thread_data, manual_thread_stack,
			K_THREAD_STACK_SIZEOF(manual_thread_stack), manual_thread,
			NULL, NULL, NULL, MANUAL_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&manual_thread_data, "manual_control");
	return 0;
}

bool manual_control_is_active(void)
{
	return atomic_get(&active) != 0;
}

void manual_control_get_status(struct manual_control_status *status)
{
	if (status == NULL) {
		return;
	}
	k_mutex_lock(&manual_mutex, K_FOREVER);
	*status = current;
	k_mutex_unlock(&manual_mutex);
}
