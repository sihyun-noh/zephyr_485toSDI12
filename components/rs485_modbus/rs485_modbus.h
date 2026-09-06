/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_RS485_MODBUS_H_
#define DAJILOG_RS485_MODBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define RS485_MODBUS_MAX_REGS 8

enum rs485_modbus_msg_type {
	RS485_MODBUS_MSG_READ_INPUT_REGS,
};

struct rs485_modbus_msg {
	enum rs485_modbus_msg_type type;
	uint8_t slave_addr;
	uint16_t start_reg;
	uint16_t reg_count;
};

struct rs485_modbus_config {
	const struct device *uart;
	struct gpio_dt_spec de;
	uint8_t default_slave_addr;
	uint16_t default_start_reg;
	uint16_t default_reg_count;
	uint32_t response_timeout_ms;
	uint32_t tx_turnaround_us;
};

struct rs485_modbus_state {
	bool ready;
	bool thread_started;
	bool value_valid;
	int last_error;
	uint8_t latest_slave_addr;
	uint16_t latest_start_reg;
	uint16_t latest_reg_count;
	uint16_t regs[RS485_MODBUS_MAX_REGS];
	uint32_t request_count;
	uint32_t response_count;
	uint32_t timeout_count;
	uint32_t crc_error_count;
	uint32_t protocol_error_count;
};

struct rs485_modbus {
	struct rs485_modbus_config config;
	struct rs485_modbus_state state;
	struct k_msgq msgq;
	char msgq_buffer[CONFIG_DAJILOG_RS485_MODBUS_MSGQ_DEPTH *
			 sizeof(struct rs485_modbus_msg)] __aligned(4);
};

int rs485_modbus_init(struct rs485_modbus *ctx);
int rs485_modbus_start(struct rs485_modbus *ctx);
int rs485_modbus_request_read(struct rs485_modbus *ctx);
int rs485_modbus_request_read_regs(struct rs485_modbus *ctx, uint8_t slave_addr,
				   uint16_t start_reg, uint16_t reg_count);
void rs485_modbus_state_get(const struct rs485_modbus *ctx,
			    struct rs485_modbus_state *out);

#endif /* DAJILOG_RS485_MODBUS_H_ */
