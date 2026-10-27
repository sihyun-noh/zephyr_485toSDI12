/* SPDX-License-Identifier: Apache-2.0 */

#include "ejection_io.h"
#include "controller_state.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define IO_NODE DT_NODELABEL(ejection_io)
#define INPUT_SAMPLE_MS 10
#define INPUT_DEBOUNCE_SAMPLES 3
#define INPUT_THREAD_STACK_SIZE 768
#define INPUT_THREAD_PRIORITY 5

BUILD_ASSERT(DT_PROP_LEN(IO_NODE, output_gpios) == EJECTION_OUTPUT_COUNT,
	     "The board must define 12 ejection outputs");
BUILD_ASSERT(DT_PROP_LEN(IO_NODE, input_gpios) == EJECTION_INPUT_COUNT,
	     "The board must define 14 isolated inputs");

#define GPIO_SPEC(node_id, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx)

static const struct gpio_dt_spec output_gpios[EJECTION_OUTPUT_COUNT] = {
	GPIO_SPEC(IO_NODE, output_gpios, 0), GPIO_SPEC(IO_NODE, output_gpios, 1),
	GPIO_SPEC(IO_NODE, output_gpios, 2), GPIO_SPEC(IO_NODE, output_gpios, 3),
	GPIO_SPEC(IO_NODE, output_gpios, 4), GPIO_SPEC(IO_NODE, output_gpios, 5),
	GPIO_SPEC(IO_NODE, output_gpios, 6), GPIO_SPEC(IO_NODE, output_gpios, 7),
	GPIO_SPEC(IO_NODE, output_gpios, 8), GPIO_SPEC(IO_NODE, output_gpios, 9),
	GPIO_SPEC(IO_NODE, output_gpios, 10), GPIO_SPEC(IO_NODE, output_gpios, 11),
};

static const struct gpio_dt_spec input_gpios[EJECTION_INPUT_COUNT] = {
	GPIO_SPEC(IO_NODE, input_gpios, 0), GPIO_SPEC(IO_NODE, input_gpios, 1),
	GPIO_SPEC(IO_NODE, input_gpios, 2), GPIO_SPEC(IO_NODE, input_gpios, 3),
	GPIO_SPEC(IO_NODE, input_gpios, 4), GPIO_SPEC(IO_NODE, input_gpios, 5),
	GPIO_SPEC(IO_NODE, input_gpios, 6), GPIO_SPEC(IO_NODE, input_gpios, 7),
	GPIO_SPEC(IO_NODE, input_gpios, 8), GPIO_SPEC(IO_NODE, input_gpios, 9),
	GPIO_SPEC(IO_NODE, input_gpios, 10), GPIO_SPEC(IO_NODE, input_gpios, 11),
	GPIO_SPEC(IO_NODE, input_gpios, 12), GPIO_SPEC(IO_NODE, input_gpios, 13),
};

struct output_state {
	struct k_work_delayable off_work;
	atomic_t active;
	size_t channel;
};

static struct output_state outputs[EJECTION_OUTPUT_COUNT];
static bool input_stable[EJECTION_INPUT_COUNT];
static bool input_candidate[EJECTION_INPUT_COUNT];
static uint8_t input_candidate_count[EJECTION_INPUT_COUNT];
static bool initialized;
K_MUTEX_DEFINE(output_mutex);
K_MUTEX_DEFINE(input_mutex);
K_THREAD_STACK_DEFINE(input_thread_stack, INPUT_THREAD_STACK_SIZE);
static struct k_thread input_thread_data;

static void output_off_handler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct output_state *state =
		CONTAINER_OF(delayable, struct output_state, off_work);

	(void)gpio_pin_set_dt(&output_gpios[state->channel], 0);
	atomic_clear(&state->active);
}

static void input_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		for (size_t i = 0; i < EJECTION_INPUT_COUNT; i++) {
			int value = gpio_pin_get_dt(&input_gpios[i]);

			if (value < 0) {
				continue;
			}

			bool sample = value != 0;
			if (sample == input_stable[i]) {
				input_candidate[i] = sample;
				input_candidate_count[i] = 0;
				continue;
			}

			if (sample != input_candidate[i]) {
				input_candidate[i] = sample;
				input_candidate_count[i] = 1;
			} else if (input_candidate_count[i] < UINT8_MAX) {
				input_candidate_count[i]++;
			}

			if (input_candidate_count[i] >= INPUT_DEBOUNCE_SAMPLES) {
				k_mutex_lock(&input_mutex, K_FOREVER);
				input_stable[i] = sample;
				k_mutex_unlock(&input_mutex);
				input_candidate_count[i] = 0;
			}
		}

		k_sleep(K_MSEC(INPUT_SAMPLE_MS));
	}
}

int ejection_io_init(void)
{
	if (initialized) {
		return 0;
	}

	for (size_t i = 0; i < EJECTION_OUTPUT_COUNT; i++) {
		if (!gpio_is_ready_dt(&output_gpios[i])) {
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&output_gpios[i], GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}

		outputs[i].channel = i;
		atomic_clear(&outputs[i].active);
		k_work_init_delayable(&outputs[i].off_work, output_off_handler);
	}

	for (size_t i = 0; i < EJECTION_INPUT_COUNT; i++) {
		if (!gpio_is_ready_dt(&input_gpios[i])) {
			(void)ejection_output_all_off();
			return -ENODEV;
		}

		int ret = gpio_pin_configure_dt(&input_gpios[i], GPIO_INPUT);
		if (ret != 0) {
			(void)ejection_output_all_off();
			return ret;
		}

		int value = gpio_pin_get_dt(&input_gpios[i]);
		if (value < 0) {
			(void)ejection_output_all_off();
			return value;
		}
		input_stable[i] = value != 0;
		input_candidate[i] = input_stable[i];
	}

	initialized = true;
	k_thread_create(&input_thread_data, input_thread_stack,
			K_THREAD_STACK_SIZEOF(input_thread_stack), input_thread,
			NULL, NULL, NULL, INPUT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&input_thread_data, "ejection_inputs");
	return 0;
}

int ejection_output_set(size_t channel, bool active)
{
	if (!initialized) {
		return -EAGAIN;
	}
	if (channel >= EJECTION_OUTPUT_COUNT) {
		return -EINVAL;
	}
	if (active && !controller_state_outputs_allowed()) {
		return -EACCES;
	}

	k_mutex_lock(&output_mutex, K_FOREVER);
	(void)k_work_cancel_delayable(&outputs[channel].off_work);
	int ret = gpio_pin_set_dt(&output_gpios[channel], active ? 1 : 0);
	if (ret == 0) {
		atomic_set(&outputs[channel].active, active ? 1 : 0);
	}
	k_mutex_unlock(&output_mutex);
	return ret;
}

int ejection_output_pulse(size_t channel, uint32_t duration_ms)
{
	if (duration_ms < EJECTION_PULSE_MIN_MS ||
	    duration_ms > EJECTION_PULSE_MAX_MS) {
		return -ERANGE;
	}

	int ret = ejection_output_set(channel, true);
	if (ret != 0) {
		return ret;
	}

	ret = k_work_reschedule(&outputs[channel].off_work, K_MSEC(duration_ms));
	if (ret < 0) {
		(void)ejection_output_set(channel, false);
		return ret;
	}
	return 0;
}

int ejection_output_all_off(void)
{
	int result = 0;

	for (size_t i = 0; i < EJECTION_OUTPUT_COUNT; i++) {
		(void)k_work_cancel_delayable(&outputs[i].off_work);
		if (gpio_is_ready_dt(&output_gpios[i])) {
			if (gpio_pin_set_dt(&output_gpios[i], 0) != 0) {
				result = -EIO;
			}
		}
		atomic_clear(&outputs[i].active);
	}
	return result;
}

bool ejection_output_get(size_t channel)
{
	return channel < EJECTION_OUTPUT_COUNT &&
	       atomic_get(&outputs[channel].active) != 0;
}

bool ejection_output_any_active(void)
{
	for (size_t i = 0; i < EJECTION_OUTPUT_COUNT; i++) {
		if (atomic_get(&outputs[i].active) != 0) {
			return true;
		}
	}
	return false;
}

bool ejection_input_get(size_t channel)
{
	bool value = false;

	if (channel >= EJECTION_INPUT_COUNT) {
		return false;
	}
	k_mutex_lock(&input_mutex, K_FOREVER);
	value = input_stable[channel];
	k_mutex_unlock(&input_mutex);
	return value;
}
