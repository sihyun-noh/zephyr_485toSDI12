/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SDI12_SLAVE_H_
#define DAJILOG_SDI12_SLAVE_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define SDI12_CMD_MAX_LEN 16
#define SDI12_RESPONSE_MAX_LEN 80

struct sdi12_slave;

typedef int (*sdi12_slave_response_cb_t)(struct sdi12_slave *ctx,
					 const char *cmd, char *response,
					 size_t response_len, void *user_data);

enum sdi12_slave_msg_type {
	SDI12_SLAVE_MSG_RX_EDGE,
};

struct sdi12_slave_msg {
	enum sdi12_slave_msg_type type;
};

struct sdi12_slave_config {
	struct gpio_dt_spec tx;
	struct gpio_dt_spec rx;
	struct gpio_dt_spec oe;
	bool rx_inverted;
	sdi12_slave_response_cb_t response_cb;
	void *user_data;
};

struct sdi12_slave_state {
	bool ready;
	bool thread_started;
	int last_error;
	int rx_raw;
	int rx_logical;
	uint32_t irq_count;
	uint32_t break_mark_count;
	uint32_t break_fail_count;
	uint32_t break_initial_high_count;
	uint32_t break_short_count;
	uint32_t mark_timeout_count;
	uint32_t timeout_count;
	uint32_t rx_error_count;
	uint32_t command_count;
	uint32_t response_count;
	uint32_t unsupported_count;
	int32_t last_break_low_us;
	int32_t last_mark_high_us;
	char latest_command[SDI12_CMD_MAX_LEN];
	char latest_response[SDI12_RESPONSE_MAX_LEN];
};

struct sdi12_slave {
	struct sdi12_slave_config config;
	struct sdi12_slave_state state;
	struct gpio_callback rx_callback;
	struct k_msgq msgq;
	char msgq_buffer[CONFIG_DAJILOG_SDI12_MSGQ_DEPTH *
			 sizeof(struct sdi12_slave_msg)] __aligned(4);
};

int sdi12_slave_init(struct sdi12_slave *ctx);
int sdi12_slave_start(struct sdi12_slave *ctx);
void sdi12_slave_state_get(struct sdi12_slave *ctx,
			   struct sdi12_slave_state *out);

#endif /* DAJILOG_SDI12_SLAVE_H_ */
