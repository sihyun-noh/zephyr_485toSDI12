/* SPDX-License-Identifier: Apache-2.0 */

#include "rs485_modbus.h"

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#define MODBUS_FC_READ_INPUT_REGS 4

static K_THREAD_STACK_DEFINE(rs485_modbus_thread_stack,
			     CONFIG_DAJILOG_RS485_MODBUS_THREAD_STACK_SIZE);
static struct k_thread rs485_modbus_thread_data;

static uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xffff;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x0001) != 0) {
				crc = (crc >> 1) ^ 0xa001;
			} else {
				crc >>= 1;
			}
		}
	}

	return crc;
}

static void rs485_drain_rx(const struct device *uart)
{
	uint8_t ch;

	while (uart_poll_in(uart, &ch) == 0) {
	}
}

static int rs485_read_response(const struct device *uart, uint8_t *rx,
			       size_t rx_len, int32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;
	size_t len = 0;

	while (len < rx_len && k_uptime_get() <= deadline) {
		if (uart_poll_in(uart, &rx[len]) == 0) {
			len++;
			continue;
		}

		k_sleep(K_MSEC(1));
	}

	return (int)len;
}

static int rs485_modbus_read_input_regs(struct rs485_modbus *ctx,
					const struct rs485_modbus_msg *msg)
{
	uint8_t req[8] = {
		msg->slave_addr,
		MODBUS_FC_READ_INPUT_REGS,
		(uint8_t)((msg->start_reg >> 8) & 0xff),
		(uint8_t)(msg->start_reg & 0xff),
		(uint8_t)((msg->reg_count >> 8) & 0xff),
		(uint8_t)(msg->reg_count & 0xff),
		0,
		0,
	};
	uint8_t rx[5 + (RS485_MODBUS_MAX_REGS * 2)];
	size_t expected_len = 5 + ((size_t)msg->reg_count * 2);
	uint16_t crc;
	uint16_t rx_crc;
	uint16_t calc_crc;
	int len;

	crc = modbus_crc16(req, 6);
	req[6] = (uint8_t)(crc & 0xff);
	req[7] = (uint8_t)((crc >> 8) & 0xff);

	ctx->state.request_count++;
	rs485_drain_rx(ctx->config.uart);

	printk("Modbus TX:");
	for (size_t i = 0; i < sizeof(req); i++) {
		printk(" %02x", req[i]);
	}
	printk("\r\n");

	gpio_pin_set_dt(&ctx->config.de, 1);
	k_busy_wait(100);
	for (size_t i = 0; i < sizeof(req); i++) {
		uart_poll_out(ctx->config.uart, req[i]);
	}
	k_busy_wait(ctx->config.tx_turnaround_us);
	gpio_pin_set_dt(&ctx->config.de, 0);

	len = rs485_read_response(ctx->config.uart, rx, expected_len,
				  ctx->config.response_timeout_ms);
	if (len <= 0) {
		ctx->state.timeout_count++;
		ctx->state.last_error = -ETIMEDOUT;
		printk("Modbus RX timeout\r\n");
		return -ETIMEDOUT;
	}

	printk("Modbus RX:");
	for (int i = 0; i < len; i++) {
		printk(" %02x", rx[i]);
	}
	printk("\r\n");

	if ((size_t)len != expected_len) {
		ctx->state.protocol_error_count++;
		ctx->state.last_error = -EMSGSIZE;
		printk("Modbus RX length mismatch len=%d expected=%u\r\n", len,
		       (unsigned int)expected_len);
		return -EMSGSIZE;
	}

	rx_crc = (uint16_t)rx[expected_len - 2] |
		 ((uint16_t)rx[expected_len - 1] << 8);
	calc_crc = modbus_crc16(rx, expected_len - 2);
	if (rx_crc != calc_crc) {
		ctx->state.crc_error_count++;
		ctx->state.last_error = -EIO;
		printk("Modbus CRC mismatch rx=0x%04x calc=0x%04x\r\n", rx_crc,
		       calc_crc);
		return -EIO;
	}

	if (rx[0] != msg->slave_addr || rx[1] != MODBUS_FC_READ_INPUT_REGS ||
	    rx[2] != (msg->reg_count * 2)) {
		ctx->state.protocol_error_count++;
		ctx->state.last_error = -EPROTO;
		printk("Modbus unexpected header addr=%u fc=%u byte_count=%u\r\n",
		       rx[0], rx[1], rx[2]);
		return -EPROTO;
	}

	for (uint16_t i = 0; i < msg->reg_count; i++) {
		size_t offset = 3 + ((size_t)i * 2);

		ctx->state.regs[i] = ((uint16_t)rx[offset] << 8) |
				     rx[offset + 1];
	}

	ctx->state.latest_slave_addr = msg->slave_addr;
	ctx->state.latest_start_reg = msg->start_reg;
	ctx->state.latest_reg_count = msg->reg_count;
	ctx->state.value_valid = true;
	ctx->state.response_count++;
	ctx->state.last_error = 0;

	printk("Modbus FC04 addr=%u", msg->slave_addr);
	for (uint16_t i = 0; i < msg->reg_count; i++) {
		uint16_t reg = ctx->state.regs[i];

		printk(" reg[%u]=%u (%u.%02u)", msg->start_reg + i, reg,
		       reg / 100, reg % 100);
	}
	printk("\r\n");

	return 0;
}

static void rs485_modbus_thread(void *arg1, void *arg2, void *arg3)
{
	struct rs485_modbus *ctx = arg1;
	struct rs485_modbus_msg msg;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msgq_get(&ctx->msgq, &msg, K_FOREVER);

		if (msg.type == RS485_MODBUS_MSG_READ_INPUT_REGS) {
			(void)rs485_modbus_read_input_regs(ctx, &msg);
		}
	}
}

int rs485_modbus_init(struct rs485_modbus *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ctx->state.ready = false;
	ctx->state.thread_started = false;
	ctx->state.value_valid = false;
	ctx->state.last_error = 0;
	ctx->state.request_count = 0;
	ctx->state.response_count = 0;
	ctx->state.timeout_count = 0;
	ctx->state.crc_error_count = 0;
	ctx->state.protocol_error_count = 0;

	k_msgq_init(&ctx->msgq, ctx->msgq_buffer,
		    sizeof(struct rs485_modbus_msg),
		    CONFIG_DAJILOG_RS485_MODBUS_MSGQ_DEPTH);

	if (ctx->config.uart == NULL || !device_is_ready(ctx->config.uart)) {
		ctx->state.last_error = -ENODEV;
		printk("RS-485 UART device is not ready\r\n");
		return -ENODEV;
	}

	if (!device_is_ready(ctx->config.de.port)) {
		ctx->state.last_error = -ENODEV;
		printk("RS-485 DE GPIO device is not ready\r\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&ctx->config.de, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		ctx->state.last_error = ret;
		printk("RS-485 DE configure failed ret=%d\r\n", ret);
		return ret;
	}

	ctx->state.ready = true;

	printk("RS-485 Modbus component ready: slave=%u fc04 start=%u count=%u\r\n",
	       ctx->config.default_slave_addr, ctx->config.default_start_reg,
	       ctx->config.default_reg_count);
	printk("RS-485 default state: DE=0 receive mode\r\n");

	return 0;
}

int rs485_modbus_start(struct rs485_modbus *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	if (!ctx->state.ready) {
		return -ENODEV;
	}

	if (ctx->state.thread_started) {
		return -EALREADY;
	}

	k_thread_create(&rs485_modbus_thread_data, rs485_modbus_thread_stack,
			K_THREAD_STACK_SIZEOF(rs485_modbus_thread_stack),
			rs485_modbus_thread, ctx, NULL, NULL,
			CONFIG_DAJILOG_RS485_MODBUS_THREAD_PRIORITY, 0,
			K_NO_WAIT);

	ctx->state.thread_started = true;
	return 0;
}

int rs485_modbus_request_read_regs(struct rs485_modbus *ctx, uint8_t slave_addr,
				   uint16_t start_reg, uint16_t reg_count)
{
	struct rs485_modbus_msg msg = {
		.type = RS485_MODBUS_MSG_READ_INPUT_REGS,
		.slave_addr = slave_addr,
		.start_reg = start_reg,
		.reg_count = reg_count,
	};

	if (ctx == NULL) {
		return -EINVAL;
	}

	if (!ctx->state.thread_started) {
		return -ENODEV;
	}

	if (reg_count == 0 || reg_count > RS485_MODBUS_MAX_REGS) {
		return -EINVAL;
	}

	return k_msgq_put(&ctx->msgq, &msg, K_NO_WAIT);
}

int rs485_modbus_request_read(struct rs485_modbus *ctx)
{
	if (ctx == NULL) {
		return -EINVAL;
	}

	return rs485_modbus_request_read_regs(
		ctx, ctx->config.default_slave_addr, ctx->config.default_start_reg,
		ctx->config.default_reg_count);
}

void rs485_modbus_state_get(const struct rs485_modbus *ctx,
			    struct rs485_modbus_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
}
