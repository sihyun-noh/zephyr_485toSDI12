/* SPDX-License-Identifier: Apache-2.0 */

#include "can_bus.h"

#include <zephyr/sys/printk.h>

static K_THREAD_STACK_DEFINE(can_bus_thread_stack,
			     CONFIG_DAJILOG_CAN_THREAD_STACK_SIZE);
static struct k_thread can_bus_thread_data;
static struct can_bus *can_bus_callback_ctx;

const char *can_bus_state_name(enum can_state state)
{
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		return "error-active";
	case CAN_STATE_ERROR_WARNING:
		return "error-warning";
	case CAN_STATE_ERROR_PASSIVE:
		return "error-passive";
	case CAN_STATE_BUS_OFF:
		return "bus-off";
	case CAN_STATE_STOPPED:
		return "stopped";
	default:
		return "unknown";
	}
}

static void can_rx_callback(const struct device *dev, struct can_frame *frame,
			    void *user_data)
{
	struct can_bus *ctx = user_data;

	ARG_UNUSED(dev);

	if (ctx == NULL) {
		ctx = can_bus_callback_ctx;
	}
	if (ctx == NULL) {
		return;
	}

	ctx->state.rx_count++;
	ctx->state.latest_rx = *frame;
	ctx->state.latest_rx_valid = true;
	if (k_msgq_put(&ctx->rx_msgq, frame, K_NO_WAIT) != 0) {
		ctx->state.rx_drop_count++;
	}

	if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
		printk("CAN RX id=0x%03x dlc=%u flags=0x%02x data:", frame->id,
		       frame->dlc, frame->flags);
		for (uint8_t i = 0; i < frame->dlc; i++) {
			printk(" %02x", frame->data[i]);
		}
		printk("\r\n");
	}
}

static void can_tx_callback(const struct device *dev, int error,
			    void *user_data)
{
	struct can_bus *ctx = user_data;

	ARG_UNUSED(dev);

	if (ctx == NULL) {
		ctx = can_bus_callback_ctx;
	}
	if (ctx == NULL) {
		return;
	}

	if (error == 0) {
		ctx->state.tx_count++;
	} else {
		ctx->state.tx_error_count++;
	}
	ctx->state.last_error = error;

	if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
		printk("CAN TX done error=%d tx_count=%lu tx_error=%lu\r\n",
		       error, (unsigned long)ctx->state.tx_count,
		       (unsigned long)ctx->state.tx_error_count);
	}
}

static void can_bus_thread(void *arg1, void *arg2, void *arg3)
{
	struct can_bus *ctx = arg1;
	struct can_bus_msg msg;
	int ret;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msgq_get(&ctx->msgq, &msg, K_FOREVER);

		if (msg.type != CAN_BUS_MSG_SEND_FRAME) {
			continue;
		}

		ret = can_send(ctx->config.dev, &msg.frame, K_MSEC(100),
			       can_tx_callback, ctx);
		if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
			printk("CAN TX request id=0x%03x ret=%d\r\n", msg.frame.id,
			       ret);
		}
		if (ret != 0) {
			ctx->state.tx_error_count++;
			ctx->state.last_error = ret;
		}
	}
}

int can_bus_init(struct can_bus *ctx)
{
	struct can_filter filter = {
		.id = 0x000,
		.mask = 0x000,
		.flags = 0,
	};
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ctx->state.ready = false;
	ctx->state.thread_started = false;
	ctx->state.last_error = 0;
	ctx->state.rx_filter_id = -ENODEV;
	ctx->state.rx_count = 0;
	ctx->state.rx_drop_count = 0;
	ctx->state.tx_count = 0;
	ctx->state.tx_error_count = 0;
	ctx->state.latest_rx_valid = false;
	k_msgq_init(&ctx->msgq, ctx->msgq_buffer, sizeof(struct can_bus_msg),
		    CONFIG_DAJILOG_CAN_MSGQ_DEPTH);
	k_msgq_init(&ctx->rx_msgq, ctx->rx_msgq_buffer, sizeof(struct can_frame),
		    CONFIG_DAJILOG_CAN_RX_MSGQ_DEPTH);

	if (ctx->config.dev == NULL || !device_is_ready(ctx->config.dev)) {
		ctx->state.last_error = -ENODEV;
		if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
			printk("CAN device is not ready\r\n");
		}
		return -ENODEV;
	}

	ret = can_start(ctx->config.dev);
	if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
		printk("CAN device %s start ret=%d\r\n", ctx->config.dev->name,
		       ret);
	}
	if (ret != 0 && ret != -EALREADY) {
		ctx->state.last_error = ret;
		return ret;
	}

	can_bus_callback_ctx = ctx;
	ctx->state.rx_filter_id =
		can_add_rx_filter(ctx->config.dev, can_rx_callback, ctx, &filter);
	ret = can_get_state(ctx->config.dev, &ctx->state.bus_state,
			    &ctx->state.err_cnt);
	if (ret != 0) {
		ctx->state.last_error = ret;
	}

	ctx->state.ready = true;
	if (IS_ENABLED(CONFIG_DAJILOG_CAN_VERBOSE)) {
		printk("CAN component ready: filter=%d bitrate=%d bps\r\n",
		       ctx->state.rx_filter_id, CONFIG_CAN_DEFAULT_BITRATE);
	}

	return 0;
}

int can_bus_start(struct can_bus *ctx)
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

	k_thread_create(&can_bus_thread_data, can_bus_thread_stack,
			K_THREAD_STACK_SIZEOF(can_bus_thread_stack),
			can_bus_thread, ctx, NULL, NULL,
			CONFIG_DAJILOG_CAN_THREAD_PRIORITY, 0, K_NO_WAIT);
	ctx->state.thread_started = true;

	return 0;
}

int can_bus_send(struct can_bus *ctx, const struct can_frame *frame)
{
	struct can_bus_msg msg = {
		.type = CAN_BUS_MSG_SEND_FRAME,
	};

	if (ctx == NULL || frame == NULL) {
		return -EINVAL;
	}
	if (!ctx->state.thread_started) {
		return -ENODEV;
	}

	msg.frame = *frame;
	return k_msgq_put(&ctx->msgq, &msg, K_NO_WAIT);
}

int can_bus_receive(struct can_bus *ctx, struct can_frame *frame, k_timeout_t timeout)
{
	if (ctx == NULL || frame == NULL) {
		return -EINVAL;
	}
	if (!ctx->state.ready) {
		return -ENODEV;
	}

	return k_msgq_get(&ctx->rx_msgq, frame, timeout);
}

int can_bus_send_test(struct can_bus *ctx)
{
	static uint32_t seq;
	struct can_frame frame = {
		.id = CONFIG_DAJILOG_CAN_TX_TEST_ID,
		.dlc = 8,
		.flags = 0,
	};
	int ret;

	frame.data[0] = (uint8_t)(seq & 0xff);
	frame.data[1] = (uint8_t)((seq >> 8) & 0xff);
	frame.data[2] = (uint8_t)((seq >> 16) & 0xff);
	frame.data[3] = (uint8_t)((seq >> 24) & 0xff);
	frame.data[4] = 0xda;
	frame.data[5] = 0x1a;
	frame.data[6] = 0x47;
	frame.data[7] = 0x01;

	ret = can_bus_send(ctx, &frame);
	if (ret == 0) {
		seq++;
	}

	return ret;
}

void can_bus_state_get(const struct can_bus *ctx, struct can_bus_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
	if (ctx->state.ready && ctx->config.dev != NULL) {
		(void)can_get_state(ctx->config.dev, &out->bus_state,
				    &out->err_cnt);
	}
}
