/* SPDX-License-Identifier: Apache-2.0 */

#include "tic_input.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "tic_fsm.h"

#define USER_NODE DT_PATH(zephyr_user)
#define INPUT_THREAD_STACK_SIZE 768
#define INPUT_THREAD_PRIORITY 5
#define INPUT_SAMPLE_MS 10
#define INPUT_DEBOUNCE_SAMPLES 3

enum input_id {
	INPUT_DI1_KEY_ON,
	INPUT_DI2_MANUAL_CRANK,
	INPUT_COUNT,
};

static const struct gpio_dt_spec inputs[INPUT_COUNT] = {
	[INPUT_DI1_KEY_ON] = GPIO_DT_SPEC_GET(USER_NODE, di1_gpios),
	[INPUT_DI2_MANUAL_CRANK] = GPIO_DT_SPEC_GET(USER_NODE, di2_gpios),
};

static struct tic_input_snapshot snapshot;
static bool stable[INPUT_COUNT];
static bool candidate[INPUT_COUNT];
static uint8_t candidate_count[INPUT_COUNT];
static bool initialized;
K_MUTEX_DEFINE(input_mutex);
K_THREAD_STACK_DEFINE(input_thread_stack, INPUT_THREAD_STACK_SIZE);
static struct k_thread input_thread_data;

static void snapshot_update_locked(void)
{
	snapshot.di1_key_on = stable[INPUT_DI1_KEY_ON];
	snapshot.di2_manual_crank = stable[INPUT_DI2_MANUAL_CRANK];
}

static int post_input_event(enum input_id id, bool active)
{
	struct tic_event event = { 0 };

	switch (id) {
	case INPUT_DI1_KEY_ON:
		event.type = active ? TIC_EVENT_KEY_ON : TIC_EVENT_KEY_OFF;
		break;
	case INPUT_DI2_MANUAL_CRANK:
		event.type = active ? TIC_EVENT_MANUAL_CRANK_ON :
					    TIC_EVENT_MANUAL_CRANK_OFF;
		break;
	default:
		return -EINVAL;
	}

	return tic_fsm_post(&event, K_MSEC(20));
}

static void input_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	for (size_t i = 0; i < INPUT_COUNT; i++) {
		(void)post_input_event((enum input_id)i, stable[i]);
	}

	while (1) {
		for (size_t i = 0; i < INPUT_COUNT; i++) {
			bool sample = gpio_pin_get_dt(&inputs[i]) > 0;

			if (sample == stable[i]) {
				candidate[i] = sample;
				candidate_count[i] = 0;
				continue;
			}

			if (sample != candidate[i]) {
				candidate[i] = sample;
				candidate_count[i] = 1;
			} else if (candidate_count[i] < UINT8_MAX) {
				candidate_count[i]++;
			}

			if (candidate_count[i] < INPUT_DEBOUNCE_SAMPLES) {
				continue;
			}

			if (post_input_event((enum input_id)i, sample) != 0) {
				continue;
			}

			stable[i] = sample;
			candidate_count[i] = 0;
			k_mutex_lock(&input_mutex, K_FOREVER);
			snapshot_update_locked();
			k_mutex_unlock(&input_mutex);
		}

		k_sleep(K_MSEC(INPUT_SAMPLE_MS));
	}
}

int tic_input_init(void)
{
	for (size_t i = 0; i < INPUT_COUNT; i++) {
		int ret;

		if (!device_is_ready(inputs[i].port)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&inputs[i], GPIO_INPUT | GPIO_PULL_UP);
		if (ret != 0) {
			return ret;
		}

		stable[i] = gpio_pin_get_dt(&inputs[i]) > 0;
		candidate[i] = stable[i];
		candidate_count[i] = 0;
	}

	k_mutex_lock(&input_mutex, K_FOREVER);
	snapshot_update_locked();
	k_mutex_unlock(&input_mutex);
	initialized = true;
	return 0;
}

int tic_input_start(void)
{
	if (!initialized) {
		return -EAGAIN;
	}

	k_thread_create(&input_thread_data, input_thread_stack,
			K_THREAD_STACK_SIZEOF(input_thread_stack), input_thread,
			NULL, NULL, NULL, INPUT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&input_thread_data, "tic_input");
	return 0;
}

void tic_input_get_snapshot(struct tic_input_snapshot *out)
{
	if (out == NULL) {
		return;
	}
	k_mutex_lock(&input_mutex, K_FOREVER);
	*out = snapshot;
	k_mutex_unlock(&input_mutex);
}
