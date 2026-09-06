/* SPDX-License-Identifier: Apache-2.0 */

#include "sdi12_slave.h"

#include <string.h>

#include <zephyr/sys/printk.h>

#define SDI12_BIT_US 833
#define SDI12_RX_POLL_US 50
#define SDI12_BREAK_MIN_US 12000
#define SDI12_BREAK_DETECT_MIN_US 11500
#define SDI12_MARK_MIN_US 1500
#define SDI12_RESPONSE_MARK_US 9000
#define SDI12_RESPONSE_MAX_US 15000
#define SDI12_BREAK_FAIL_LOG_PERIOD_MS 1000

static K_THREAD_STACK_DEFINE(sdi12_slave_thread_stack,
			     CONFIG_DAJILOG_SDI12_THREAD_STACK_SIZE);
static struct k_thread sdi12_slave_thread_data;

static int sdi12_rx_get_raw(struct sdi12_slave *ctx)
{
	return gpio_pin_get_dt(&ctx->config.rx);
}

static int sdi12_rx_get(struct sdi12_slave *ctx)
{
	int raw = sdi12_rx_get_raw(ctx);

	return ctx->config.rx_inverted ? !raw : raw;
}

static uint8_t even_parity7(uint8_t value)
{
	uint8_t parity = 0;

	for (int i = 0; i < 7; i++) {
		parity ^= (value >> i) & 1U;
	}

	return parity;
}

static void sdi12_rx_edge_isr(const struct device *port,
			      struct gpio_callback *cb, uint32_t pins)
{
	struct sdi12_slave *ctx =
		CONTAINER_OF(cb, struct sdi12_slave, rx_callback);
	struct sdi12_slave_msg msg = {
		.type = SDI12_SLAVE_MSG_RX_EDGE,
	};

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	ctx->state.irq_count++;
	(void)k_msgq_put(&ctx->msgq, &msg, K_NO_WAIT);
}

static void sdi12_set_tx_bit(struct sdi12_slave *ctx, uint8_t bit)
{
	gpio_pin_set_dt(&ctx->config.tx, bit ? 0 : 1);
}

static void sdi12_send_char(struct sdi12_slave *ctx, char ch)
{
	uint8_t data = (uint8_t)ch & 0x7f;
	uint8_t parity = even_parity7(data);

	sdi12_set_tx_bit(ctx, 0);
	k_busy_wait(SDI12_BIT_US);

	for (int i = 0; i < 7; i++) {
		sdi12_set_tx_bit(ctx, (data >> i) & 1U);
		k_busy_wait(SDI12_BIT_US);
	}

	sdi12_set_tx_bit(ctx, parity);
	k_busy_wait(SDI12_BIT_US);

	sdi12_set_tx_bit(ctx, 1);
	k_busy_wait(SDI12_BIT_US);
}

static void sdi12_send_response(struct sdi12_slave *ctx, const char *response,
				int64_t command_end_us)
{
	k_busy_wait(SDI12_RESPONSE_MARK_US);

	if ((k_cyc_to_us_floor64(k_cycle_get_32()) - command_end_us) >
	    SDI12_RESPONSE_MAX_US) {
		printk("SDI-12 response start exceeded 15ms window\r\n");
	}

	gpio_pin_set_dt(&ctx->config.oe, 1);
	k_busy_wait(SDI12_BIT_US);

	while (*response != '\0') {
		sdi12_send_char(ctx, *response++);
	}

	sdi12_set_tx_bit(ctx, 1);
	k_busy_wait(SDI12_BIT_US);
	gpio_pin_set_dt(&ctx->config.oe, 0);
}

static bool sdi12_wait_rx_level_measured(struct sdi12_slave *ctx, int level,
					 int32_t stable_us, int32_t timeout_us,
					 int32_t *max_stable_seen_us)
{
	int32_t elapsed_us = 0;
	int32_t stable_seen_us = 0;
	int32_t max_seen_us = 0;

	while (elapsed_us < timeout_us) {
		if (sdi12_rx_get(ctx) == level) {
			stable_seen_us += SDI12_RX_POLL_US;
			if (stable_seen_us > max_seen_us) {
				max_seen_us = stable_seen_us;
			}
			if (stable_seen_us >= stable_us) {
				if (max_stable_seen_us != NULL) {
					*max_stable_seen_us = max_seen_us;
				}
				return true;
			}
		} else {
			stable_seen_us = 0;
		}

		k_busy_wait(SDI12_RX_POLL_US);
		elapsed_us += SDI12_RX_POLL_US;
	}

	if (max_stable_seen_us != NULL) {
		*max_stable_seen_us = max_seen_us;
	}

	return false;
}

static void sdi12_log_break_fail(struct sdi12_slave *ctx, const char *reason,
				 int32_t low_us, int32_t high_us)
{
	static int64_t next_log_ms;
	int64_t now_ms = k_uptime_get();

	ctx->state.break_fail_count++;
	if (now_ms < next_log_ms) {
		return;
	}
	next_log_ms = now_ms + SDI12_BREAK_FAIL_LOG_PERIOD_MS;

	printk("SDI-12 break fail reason=%s raw=%d logical=%d low_us=%d "
	       "high_us=%d irq=%lu break_ok=%lu fail=%lu initial_high=%lu "
	       "short=%lu mark_timeout=%lu\r\n",
	       reason, sdi12_rx_get_raw(ctx), sdi12_rx_get(ctx), low_us,
	       high_us, (unsigned long)ctx->state.irq_count,
	       (unsigned long)ctx->state.break_mark_count,
	       (unsigned long)ctx->state.break_fail_count,
	       (unsigned long)ctx->state.break_initial_high_count,
	       (unsigned long)ctx->state.break_short_count,
	       (unsigned long)ctx->state.mark_timeout_count);
}

static bool sdi12_wait_break_and_mark(struct sdi12_slave *ctx)
{
	int32_t low_us = 0;
	int32_t high_us = 0;

	if (sdi12_rx_get(ctx) != 0) {
		ctx->state.break_initial_high_count++;
		sdi12_log_break_fail(ctx, "initial_high", 0, 0);
		return false;
	}

	if (!sdi12_wait_rx_level_measured(ctx, 0, SDI12_BREAK_DETECT_MIN_US, 20000,
					  &low_us)) {
		ctx->state.break_short_count++;
		ctx->state.last_break_low_us = low_us;
		sdi12_log_break_fail(ctx, "break_short", low_us, 0);
		return false;
	}
	ctx->state.last_break_low_us = low_us;

	if (!sdi12_wait_rx_level_measured(ctx, 1, SDI12_MARK_MIN_US, 50000,
					  &high_us)) {
		ctx->state.mark_timeout_count++;
		ctx->state.last_mark_high_us = high_us;
		sdi12_log_break_fail(ctx, "mark_timeout", low_us,
				     high_us);
		return false;
	}
	ctx->state.last_mark_high_us = SDI12_MARK_MIN_US;

	return true;
}

static int sdi12_read_char(struct sdi12_slave *ctx, uint8_t *out,
			   int32_t timeout_us)
{
	int64_t deadline = k_uptime_get() + (timeout_us / 1000);
	uint8_t data = 0;
	uint8_t parity;
	int stop;

	while (sdi12_rx_get(ctx) == 0) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
		k_busy_wait(SDI12_RX_POLL_US);
	}

	while (sdi12_rx_get(ctx) != 0) {
		if (k_uptime_get() > deadline) {
			return -ETIMEDOUT;
		}
		k_busy_wait(SDI12_RX_POLL_US);
	}

	k_busy_wait(SDI12_BIT_US + (SDI12_BIT_US / 2));

	for (int i = 0; i < 7; i++) {
		if (sdi12_rx_get(ctx) != 0) {
			data |= BIT(i);
		}
		k_busy_wait(SDI12_BIT_US);
	}

	parity = sdi12_rx_get(ctx) ? 1U : 0U;
	k_busy_wait(SDI12_BIT_US);
	stop = sdi12_rx_get(ctx);

	if (parity != even_parity7(data) || stop == 0) {
		return -EIO;
	}

	*out = data;
	return 0;
}

static void sdi12_handle_command(struct sdi12_slave *ctx, const char *cmd,
				 int64_t command_end_us)
{
	char response[SDI12_RESPONSE_MAX_LEN] = {0};
	int ret = -ENOTSUP;

	if (ctx->config.response_cb != NULL) {
		ret = ctx->config.response_cb(ctx, cmd, response,
					      sizeof(response),
					      ctx->config.user_data);
	} else if (strcmp(cmd, "?!") == 0) {
		ret = snprintk(response, sizeof(response), "0\r\n");
	} else if (strcmp(cmd, "0!") == 0) {
		ret = snprintk(response, sizeof(response), "0\r\n");
	} else if (strcmp(cmd, "0I!") == 0) {
		ret = snprintk(response, sizeof(response),
			       "013DAJILOGTECHV01\r\n");
	}

	if (ret > 0 && response[0] != '\0') {
		strncpy(ctx->state.latest_response, response,
			sizeof(ctx->state.latest_response) - 1);
		ctx->state.latest_response[sizeof(ctx->state.latest_response) -
					   1] = '\0';
		ctx->state.response_count++;
		sdi12_send_response(ctx, response, command_end_us);
		printk("SDI-12 cmd: %s rsp: %s", cmd, response);
	} else {
		ctx->state.unsupported_count++;
		ctx->state.last_error = ret;
		printk("SDI-12 unsupported cmd: %s\r\n", cmd);
	}
}

static void sdi12_slave_poll(struct sdi12_slave *ctx)
{
	char cmd[SDI12_CMD_MAX_LEN];
	size_t len = 0;
	uint8_t ch;
	int ret;

	if (!sdi12_wait_break_and_mark(ctx)) {
		return;
	}
	ctx->state.break_mark_count++;

	while (len < (sizeof(cmd) - 1)) {
		ret = sdi12_read_char(ctx, &ch, 20000);
		if (ret == -ETIMEDOUT) {
			ctx->state.timeout_count++;
			printk("SDI-12 command timeout after break/mark len=%u\r\n",
			       (unsigned int)len);
			return;
		}
		if (ret != 0) {
			ctx->state.rx_error_count++;
			ctx->state.last_error = ret;
			printk("SDI-12 rx error ret=%d len=%u\r\n", ret,
			       (unsigned int)len);
			return;
		}
		if (ch < 0x20 || ch > 0x7e) {
			len = 0;
			continue;
		}

		cmd[len++] = (char)ch;
		if (ch == '!') {
			cmd[len] = '\0';
			ctx->state.command_count++;
			strncpy(ctx->state.latest_command, cmd,
				sizeof(ctx->state.latest_command) - 1);
			ctx->state.latest_command[sizeof(ctx->state.latest_command) -
						  1] = '\0';
			sdi12_handle_command(
				ctx, cmd, k_cyc_to_us_floor64(k_cycle_get_32()));
			return;
		}
	}

	printk("SDI-12 command too long\r\n");
}

static void sdi12_slave_thread(void *arg1, void *arg2, void *arg3)
{
	struct sdi12_slave *ctx = arg1;
	struct sdi12_slave_msg msg;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msgq_get(&ctx->msgq, &msg, K_FOREVER);
		if (msg.type == SDI12_SLAVE_MSG_RX_EDGE) {
			sdi12_slave_poll(ctx);
		}
	}
}

int sdi12_slave_init(struct sdi12_slave *ctx)
{
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	memset(&ctx->state, 0, sizeof(ctx->state));
	k_msgq_init(&ctx->msgq, ctx->msgq_buffer,
		    sizeof(struct sdi12_slave_msg),
		    CONFIG_DAJILOG_SDI12_MSGQ_DEPTH);

	if (!device_is_ready(ctx->config.tx.port) ||
	    !device_is_ready(ctx->config.rx.port) ||
	    !device_is_ready(ctx->config.oe.port)) {
		ctx->state.last_error = -ENODEV;
		printk("SDI-12 GPIO device is not ready\r\n");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&ctx->config.oe, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}
	ret = gpio_pin_configure_dt(&ctx->config.tx, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}
	ret = gpio_pin_configure_dt(&ctx->config.rx, GPIO_INPUT);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	gpio_init_callback(&ctx->rx_callback, sdi12_rx_edge_isr,
			   BIT(ctx->config.rx.pin));
	ret = gpio_add_callback(ctx->config.rx.port, &ctx->rx_callback);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(
		&ctx->config.rx, ctx->config.rx_inverted ? GPIO_INT_EDGE_RISING :
							   GPIO_INT_EDGE_FALLING);
	if (ret != 0) {
		ctx->state.last_error = ret;
		return ret;
	}

	ctx->state.ready = true;
	printk("SDI-12 component ready: TX=PB10 RX=PB11 OE=PB12 rx_inverted=%d\r\n",
	       ctx->config.rx_inverted);
	printk("SDI-12 response provider: %s\r\n",
	       ctx->config.response_cb != NULL ? "app callback" :
						 "built-in minimal");

	return 0;
}

int sdi12_slave_start(struct sdi12_slave *ctx)
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

	k_thread_create(&sdi12_slave_thread_data, sdi12_slave_thread_stack,
			K_THREAD_STACK_SIZEOF(sdi12_slave_thread_stack),
			sdi12_slave_thread, ctx, NULL, NULL,
			CONFIG_DAJILOG_SDI12_THREAD_PRIORITY, 0, K_NO_WAIT);
	ctx->state.thread_started = true;

	return 0;
}

void sdi12_slave_state_get(struct sdi12_slave *ctx,
			   struct sdi12_slave_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	ctx->state.rx_raw = sdi12_rx_get_raw(ctx);
	ctx->state.rx_logical = sdi12_rx_get(ctx);
	*out = ctx->state;
}
