/* SPDX-License-Identifier: Apache-2.0 */

#include "app_context.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>

#include "monitor_task.h"
#include "rs485_task.h"
#include "sdi12_response.h"
#include "sdi12_task.h"

#define USER_NODE DT_PATH(zephyr_user)
#define RS485_UART_NODE DT_NODELABEL(usart1)
#define I2C1_NODE DT_NODELABEL(i2c1)

struct bridge_gpio_output {
	const char *name;
	struct gpio_dt_spec gpio;
};

static const struct bridge_gpio_output od_outputs[] = {
	{
		.name = "od1_pb3",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, od1_gpios),
	},
	{
		.name = "od2_pb4",
		.gpio = GPIO_DT_SPEC_GET(USER_NODE, od2_gpios),
	},
};

struct app_context app = {
	.rs485 = {
		.config = {
			.uart = DEVICE_DT_GET(RS485_UART_NODE),
			.de = GPIO_DT_SPEC_GET(USER_NODE, rs485_en_gpios),
			.default_slave_addr = CONFIG_BRIDGE_RS485_SLAVE_ADDR,
			.default_start_reg = CONFIG_BRIDGE_RS485_START_REG,
			.default_reg_count = CONFIG_BRIDGE_RS485_REG_COUNT,
			.response_timeout_ms =
				CONFIG_BRIDGE_RS485_RESPONSE_TIMEOUT_MS,
			.tx_turnaround_us = CONFIG_BRIDGE_RS485_TX_TURNAROUND_US,
		},
	},
	.sdi12 = {
		.config = {
			.tx = GPIO_DT_SPEC_GET(USER_NODE, sdi12_tx_gpios),
			.rx = GPIO_DT_SPEC_GET(USER_NODE, sdi12_rx_gpios),
			.oe = GPIO_DT_SPEC_GET(USER_NODE, sdi12_oe_gpios),
			.rx_inverted = IS_ENABLED(CONFIG_DAJILOG_SDI12_RX_INVERTED),
			.response_cb = sdi12_bridge_response_cb,
			.user_data = &app,
		},
	},
	.oled = {
		.config = {
			.display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
			.i2c = DEVICE_DT_GET(I2C1_NODE),
			.i2c_speed_hz = CONFIG_DAJILOG_OLED_I2C_SPEED_HZ,
			.scan_i2c_on_init =
				IS_ENABLED(CONFIG_DAJILOG_OLED_SCAN_I2C_ON_INIT),
		},
	},
};

static uint8_t bridge_uart_parity(void)
{
	if (IS_ENABLED(CONFIG_BRIDGE_RS485_PARITY_EVEN)) {
		return UART_CFG_PARITY_EVEN;
	}
	if (IS_ENABLED(CONFIG_BRIDGE_RS485_PARITY_ODD)) {
		return UART_CFG_PARITY_ODD;
	}

	return UART_CFG_PARITY_NONE;
}

static int configure_rs485_uart(const struct device *uart)
{
	const struct uart_config config = {
		.baudrate = CONFIG_BRIDGE_RS485_BAUDRATE,
		.parity = bridge_uart_parity(),
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};

	return uart_configure(uart, &config);
}

static int configure_od_outputs(bool verbose)
{
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(od_outputs); i++) {
		const struct bridge_gpio_output *output = &od_outputs[i];

		if (!device_is_ready(output->gpio.port)) {
			if (verbose) {
				printk("%s GPIO port is not ready\r\n",
				       output->name);
			}
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&output->gpio,
					    GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			if (verbose) {
				printk("%s configure failed ret=%d\r\n",
				       output->name, ret);
			}
			return ret;
		}

		(void)gpio_pin_set_dt(&output->gpio, 0);

		if (verbose) {
			printk("%s ready: %s pin %u open-drain inactive/released\r\n",
			       output->name, output->gpio.port->name,
			       output->gpio.pin);
		}
	}

	return 0;
}

static int od_outputs_early_safe_state(void)
{
	return configure_od_outputs(false);
}

SYS_INIT(od_outputs_early_safe_state, APPLICATION, 0);

void app_print_profile(void)
{
	printk("Profile: RS485 slave=%d fc=%d start=%d count=%d baud=%d poll=%dms timeout=%dms\r\n",
	       CONFIG_BRIDGE_RS485_SLAVE_ADDR,
	       CONFIG_BRIDGE_RS485_FUNCTION_CODE,
	       CONFIG_BRIDGE_RS485_START_REG, CONFIG_BRIDGE_RS485_REG_COUNT,
	       CONFIG_BRIDGE_RS485_BAUDRATE,
	       CONFIG_BRIDGE_RS485_POLL_PERIOD_MS,
	       CONFIG_BRIDGE_RS485_RESPONSE_TIMEOUT_MS);
	printk("Profile: SDI-12 address=%s vendor=%s model=%s version=%s serial=%s\r\n",
	       CONFIG_BRIDGE_SDI12_ADDRESS, CONFIG_BRIDGE_SDI12_VENDOR,
	       CONFIG_BRIDGE_SDI12_MODEL, CONFIG_BRIDGE_SDI12_VERSION,
	       CONFIG_BRIDGE_SDI12_SERIAL);
}

int app_context_init(struct app_context *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	app_fsm_init(&ctx->fsm);
	bridge_state_init(&ctx->bridge);
	k_msgq_init(&ctx->app_msgq, ctx->app_msgq_buffer,
		    sizeof(struct app_msg), CONFIG_BRIDGE_APP_MSGQ_DEPTH);

	return 0;
}

int app_components_init(struct app_context *ctx)
{
	struct app_msg msg = {
		.type = APP_MSG_HW_READY,
		.timestamp_ms = k_uptime_get(),
	};
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ret = configure_rs485_uart(ctx->rs485.config.uart);
	printk("RS485 UART configure ret=%d\r\n", ret);
	if (ret != 0) {
		return ret;
	}

	ret = configure_od_outputs(true);
	printk("OD outputs configure ret=%d\r\n", ret);
	if (ret != 0) {
		return ret;
	}

	ret = rs485_modbus_init(&ctx->rs485);
	if (ret == 0) {
		ret = rs485_modbus_start(&ctx->rs485);
	}
	printk("RS485 component start ret=%d\r\n", ret);
	if (ret != 0) {
		return ret;
	}

	ret = sdi12_slave_init(&ctx->sdi12);
	if (ret == 0) {
		ret = sdi12_slave_start(&ctx->sdi12);
	}
	printk("SDI-12 component start ret=%d\r\n", ret);
	if (ret != 0) {
		return ret;
	}

	ret = oled_display_init(&ctx->oled);
	printk("OLED component init ret=%d\r\n", ret);
	if (ret != 0) {
		printk("OLED disabled for this run ret=%d\r\n", ret);
	}

	(void)app_msg_publish(&ctx->app_msgq, &msg);

	return 0;
}

int app_tasks_start(struct app_context *ctx)
{
	int ret;

	ret = rs485_task_start(ctx);
	if (ret != 0) {
		return ret;
	}

	ret = sdi12_task_start(ctx);
	if (ret != 0) {
		return ret;
	}

	return monitor_task_start(ctx);
}
