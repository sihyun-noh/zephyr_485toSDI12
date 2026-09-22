/* SPDX-License-Identifier: Apache-2.0 */

#include "app_context.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/sys/printk.h>

#include <dajilog_device_provisioning/dajilog_device_provisioning.h>

#include "monitor_task.h"
#include "rs485_task.h"
#include "sdi12_response.h"
#include "sdi12_task.h"

#define USER_NODE DT_PATH(zephyr_user)
#define RS485_UART_NODE DT_NODELABEL(usart1)
#define CAN_NODE DT_CHOSEN(zephyr_canbus)

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
			.default_function_code = CONFIG_BRIDGE_RS485_FUNCTION_CODE,
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
	.can = {
		.config = {
			.dev = DEVICE_DT_GET(CAN_NODE),
			.default_tx_id = DAJILOG_DEVICE_INFO_RESPONSE_BASE_ID,
		},
	},
	.device_profile = {
		.config = {
			.allow_development_profile =
				IS_ENABLED(CONFIG_BRIDGE_DEVICE_PROFILE_ALLOW_DEVELOPMENT),
			.development_node_id = CONFIG_BRIDGE_DEVICE_NODE_ID,
			.development_hardware_revision = CONFIG_BRIDGE_DEVICE_HW_REVISION,
			.development_serial_number = CONFIG_BRIDGE_DEVICE_SERIAL_NUMBER,
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

int app_device_services_init(struct app_context *ctx)
{
	struct dajilog_device_profile_state profile;
	struct dajilog_device_identity identity = {
		.device_type = DAJILOG_DEVICE_TYPE_485_TO_SDI12,
		.hardware_revision = CONFIG_BRIDGE_DEVICE_HW_REVISION,
		.firmware_major = CONFIG_BRIDGE_DEVICE_FW_MAJOR,
		.firmware_minor = CONFIG_BRIDGE_DEVICE_FW_MINOR,
		.firmware_patch = CONFIG_BRIDGE_DEVICE_FW_PATCH,
		.firmware_build = CONFIG_BRIDGE_DEVICE_FW_BUILD,
		.app_protocol_version = 1,
		.capability = DAJILOG_CAPABILITY_EEPROM_STORE |
			      DAJILOG_CAPABILITY_RS485_MODBUS |
			      DAJILOG_CAPABILITY_SDI12_SLAVE |
			      DAJILOG_CAPABILITY_SERVICE_MODE,
	};
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ret = dajilog_device_profile_init(&ctx->device_profile);
	dajilog_device_profile_get(&ctx->device_profile, &profile);
	printk("Device profile init ret=%d valid=%d persistent=%d development=%d node=%u serial=%u hw=%u\r\n",
	       ret, profile.valid, profile.persistent, profile.development,
	       profile.data.node_id, (unsigned int)profile.data.serial_number,
	       profile.data.hardware_revision);
	if (ret != 0) {
		return ret;
	}

	identity.hardware_revision = profile.data.hardware_revision;
	ret = dajilog_device_info_init(&ctx->device_info, &identity,
				       &ctx->device_profile);
	if (ret != 0) {
		return ret;
	}

	ret = dajilog_device_provisioning_init(
		&(struct dajilog_device_provisioning_config){
			.device_info = &ctx->device_info,
			.runtime_get = bridge_device_runtime_get,
			.user_data = ctx,
		});
	printk("Device services init ret=%d request=0x%03x response=0x%03x\r\n",
	       ret, DAJILOG_DEVICE_INFO_REQUEST_BASE_ID + profile.data.node_id,
	       DAJILOG_DEVICE_INFO_RESPONSE_BASE_ID + profile.data.node_id);
	return ret;
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

	if (IS_ENABLED(CONFIG_BRIDGE_CAN_ENABLE)) {
		ret = can_bus_init(&ctx->can);
		if (ret == 0) {
			ret = can_bus_start(&ctx->can);
		}
		printk("CAN component start ret=%d\r\n", ret);
		if (ret != 0) {
			return ret;
		}
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

	(void)app_msg_publish(&ctx->app_msgq, &msg);

	return 0;
}

int app_tasks_start(struct app_context *ctx)
{
	int ret;

	if (IS_ENABLED(CONFIG_BRIDGE_CAN_ENABLE)) {
		ret = bridge_can_service_init(&ctx->can_service, &ctx->can, ctx,
					      &ctx->device_info);
		if (ret != 0) {
			return ret;
		}
		printk("Bridge CAN Device Info and telemetry service ready\r\n");
	}

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
