/* SPDX-License-Identifier: Apache-2.0 */

#include "tic_output.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#define USER_NODE DT_PATH(zephyr_user)

struct tic_gpio_signal {
	const char *name;
	struct gpio_dt_spec gpio;
};

enum relay_id {
	RELAY_R1_KEY_ON,
	RELAY_R2_RESTART,
	RELAY_R3_MANUAL_LOCKOUT,
	RELAY_R4_ENGINE_STOP,
	RELAY_COUNT,
};

static const struct tic_gpio_signal relays[RELAY_COUNT] = {
	[RELAY_R1_KEY_ON] = {
		.name = "r1_key_on",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, do_relay1_gpios),
	},
	[RELAY_R2_RESTART] = {
		.name = "r2_restart",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, do_relay2_gpios),
	},
	[RELAY_R3_MANUAL_LOCKOUT] = {
		.name = "r3_manual_lockout",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, do_relay3_gpios),
	},
	[RELAY_R4_ENGINE_STOP] = {
		.name = "r4_engine_stop",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, do_relay4_gpios),
	},
};

static const struct tic_gpio_signal leds[TIC_LED_COUNT] = {
	[TIC_LED_RESTART] = {
		.name = "restart",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, restart_led_gpios),
	},
	[TIC_LED_ACC] = {
		.name = "acc",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, acc_led_gpios),
	},
	[TIC_LED_MANUAL] = {
		.name = "manual",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, manual_led_gpios),
	},
	[TIC_LED_KEY] = {
		.name = "key",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, key_led_gpios),
	},
};

static struct tic_output_snapshot snapshot;
static bool ready;
K_MUTEX_DEFINE(output_mutex);

static int configure_safe(const struct tic_gpio_signal *signals, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		int ret;

		if (!device_is_ready(signals[i].gpio.port)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&signals[i].gpio,
					    GPIO_OUTPUT_INACTIVE | GPIO_PULL_DOWN);
		if (ret != 0) {
			return ret;
		}
		(void)gpio_pin_set_dt(&signals[i].gpio, 0);
	}

	return 0;
}

static int relays_early_safe_state(void)
{
	return configure_safe(relays, ARRAY_SIZE(relays));
}

SYS_INIT(relays_early_safe_state, APPLICATION, 0);

static int relay_set_locked(enum relay_id id, bool on)
{
	int ret;

	if (!ready) {
		return -EAGAIN;
	}

	if (on && id != RELAY_R1_KEY_ON && id != RELAY_R4_ENGINE_STOP &&
	    !snapshot.r1_key_on) {
		return -EACCES;
	}

	ret = gpio_pin_set_dt(&relays[id].gpio, on ? 1 : 0);
	if (ret != 0) {
		return ret;
	}

	switch (id) {
	case RELAY_R1_KEY_ON:
		snapshot.r1_key_on = on;
		break;
	case RELAY_R2_RESTART:
		snapshot.r2_restart = on;
		break;
	case RELAY_R3_MANUAL_LOCKOUT:
		snapshot.r3_manual_lockout = on;
		break;
	case RELAY_R4_ENGINE_STOP:
		snapshot.r4_engine_stop = on;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int led_set_locked(enum tic_led_id id, bool on)
{
	int ret = gpio_pin_set_dt(&leds[id].gpio, on ? 1 : 0);

	if (ret != 0) {
		return ret;
	}

	switch (id) {
	case TIC_LED_RESTART:
		snapshot.led_restart = on;
		break;
	case TIC_LED_ACC:
		snapshot.led_acc = on;
		break;
	case TIC_LED_MANUAL:
		snapshot.led_manual = on;
		break;
	case TIC_LED_KEY:
		snapshot.led_key = on;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void sync_auto_leds_locked(bool key_input_on)
{
	if (!snapshot.led_auto) {
		return;
	}

	(void)led_set_locked(TIC_LED_KEY, key_input_on);
	(void)led_set_locked(TIC_LED_RESTART, snapshot.r2_restart);
	(void)led_set_locked(TIC_LED_MANUAL, snapshot.r3_manual_lockout);
	(void)led_set_locked(TIC_LED_ACC, snapshot.r4_engine_stop);
}

int tic_output_init(void)
{
	int ret;

	ret = configure_safe(relays, ARRAY_SIZE(relays));
	if (ret != 0) {
		return ret;
	}
	ret = configure_safe(leds, ARRAY_SIZE(leds));
	if (ret != 0) {
		return ret;
	}

	k_mutex_lock(&output_mutex, K_FOREVER);
	snapshot = (struct tic_output_snapshot){
		.led_auto = true,
	};
	ready = true;
	k_mutex_unlock(&output_mutex);

	return 0;
}

int tic_output_set_r1(bool on)
{
	int ret;

	k_mutex_lock(&output_mutex, K_FOREVER);
	if (!on) {
		(void)relay_set_locked(RELAY_R2_RESTART, false);
		(void)relay_set_locked(RELAY_R3_MANUAL_LOCKOUT, false);
	}
	ret = relay_set_locked(RELAY_R1_KEY_ON, on);
	sync_auto_leds_locked(snapshot.led_key);
	k_mutex_unlock(&output_mutex);
	return ret;
}

int tic_output_set_r2(bool on)
{
	int ret;

	k_mutex_lock(&output_mutex, K_FOREVER);
	ret = relay_set_locked(RELAY_R2_RESTART, on);
	sync_auto_leds_locked(snapshot.led_key);
	k_mutex_unlock(&output_mutex);
	return ret;
}

int tic_output_set_r3(bool on)
{
	int ret;

	k_mutex_lock(&output_mutex, K_FOREVER);
	ret = relay_set_locked(RELAY_R3_MANUAL_LOCKOUT, on);
	sync_auto_leds_locked(snapshot.led_key);
	k_mutex_unlock(&output_mutex);
	return ret;
}

int tic_output_set_r4(bool on)
{
	int ret;

	k_mutex_lock(&output_mutex, K_FOREVER);
	ret = relay_set_locked(RELAY_R4_ENGINE_STOP, on);
	sync_auto_leds_locked(snapshot.led_key);
	k_mutex_unlock(&output_mutex);
	return ret;
}

int tic_output_all_off(void)
{
	int ret = 0;

	k_mutex_lock(&output_mutex, K_FOREVER);
	if (relay_set_locked(RELAY_R2_RESTART, false) != 0) {
		ret = -EIO;
	}
	if (relay_set_locked(RELAY_R3_MANUAL_LOCKOUT, false) != 0) {
		ret = -EIO;
	}
	if (relay_set_locked(RELAY_R1_KEY_ON, false) != 0) {
		ret = -EIO;
	}
	if (relay_set_locked(RELAY_R4_ENGINE_STOP, false) != 0) {
		ret = -EIO;
	}
	sync_auto_leds_locked(snapshot.led_key);
	k_mutex_unlock(&output_mutex);
	return ret;
}

void tic_output_sync_auto_leds(bool key_input_on)
{
	k_mutex_lock(&output_mutex, K_FOREVER);
	sync_auto_leds_locked(key_input_on);
	k_mutex_unlock(&output_mutex);
}

int tic_output_led_set(enum tic_led_id id, bool on)
{
	int ret;

	if (id >= TIC_LED_COUNT) {
		return -EINVAL;
	}
	k_mutex_lock(&output_mutex, K_FOREVER);
	snapshot.led_auto = false;
	ret = led_set_locked(id, on);
	k_mutex_unlock(&output_mutex);
	return ret;
}

int tic_output_led_set_all(bool on)
{
	int ret = 0;

	k_mutex_lock(&output_mutex, K_FOREVER);
	snapshot.led_auto = false;
	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (led_set_locked((enum tic_led_id)i, on) != 0) {
			ret = -EIO;
		}
	}
	k_mutex_unlock(&output_mutex);
	return ret;
}

void tic_output_led_auto_enable(bool enable, bool key_input_on)
{
	k_mutex_lock(&output_mutex, K_FOREVER);
	snapshot.led_auto = enable;
	sync_auto_leds_locked(key_input_on);
	k_mutex_unlock(&output_mutex);
}

void tic_output_get_snapshot(struct tic_output_snapshot *out)
{
	if (out == NULL) {
		return;
	}
	k_mutex_lock(&output_mutex, K_FOREVER);
	*out = snapshot;
	k_mutex_unlock(&output_mutex);
}
