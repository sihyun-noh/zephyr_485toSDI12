/* SPDX-License-Identifier: Apache-2.0 */

#include "sbus.h"

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define SBUS_FRAME_LEN 25
#define SBUS_START_BYTE 0x0f

static K_THREAD_STACK_DEFINE(sbus_thread_stack,
			     CONFIG_DAJILOG_SBUS_THREAD_STACK_SIZE);
static struct k_thread sbus_thread_data;

static void sbus_decode_channels(const uint8_t frame[SBUS_FRAME_LEN],
				 uint16_t ch[SBUS_CHANNEL_COUNT])
{
	ch[0] = ((uint16_t)frame[1] | ((uint16_t)frame[2] << 8)) & 0x07ff;
	ch[1] = (((uint16_t)frame[2] >> 3) | ((uint16_t)frame[3] << 5)) & 0x07ff;
	ch[2] = (((uint16_t)frame[3] >> 6) | ((uint16_t)frame[4] << 2) |
		 ((uint16_t)frame[5] << 10)) & 0x07ff;
	ch[3] = (((uint16_t)frame[5] >> 1) | ((uint16_t)frame[6] << 7)) & 0x07ff;
	ch[4] = (((uint16_t)frame[6] >> 4) | ((uint16_t)frame[7] << 4)) & 0x07ff;
	ch[5] = (((uint16_t)frame[7] >> 7) | ((uint16_t)frame[8] << 1) |
		 ((uint16_t)frame[9] << 9)) & 0x07ff;
	ch[6] = (((uint16_t)frame[9] >> 2) | ((uint16_t)frame[10] << 6)) & 0x07ff;
	ch[7] = (((uint16_t)frame[10] >> 5) | ((uint16_t)frame[11] << 3)) & 0x07ff;
	ch[8] = ((uint16_t)frame[12] | ((uint16_t)frame[13] << 8)) & 0x07ff;
	ch[9] = (((uint16_t)frame[13] >> 3) | ((uint16_t)frame[14] << 5)) & 0x07ff;
	ch[10] = (((uint16_t)frame[14] >> 6) | ((uint16_t)frame[15] << 2) |
		  ((uint16_t)frame[16] << 10)) & 0x07ff;
	ch[11] = (((uint16_t)frame[16] >> 1) | ((uint16_t)frame[17] << 7)) & 0x07ff;
	ch[12] = (((uint16_t)frame[17] >> 4) | ((uint16_t)frame[18] << 4)) & 0x07ff;
	ch[13] = (((uint16_t)frame[18] >> 7) | ((uint16_t)frame[19] << 1) |
		  ((uint16_t)frame[20] << 9)) & 0x07ff;
	ch[14] = (((uint16_t)frame[20] >> 2) | ((uint16_t)frame[21] << 6)) & 0x07ff;
	ch[15] = (((uint16_t)frame[21] >> 5) | ((uint16_t)frame[22] << 3)) & 0x07ff;
}

static void sbus_process_frame(struct sbus *ctx,
			       const uint8_t frame[SBUS_FRAME_LEN])
{
	if (frame[24] != 0x00) {
		ctx->state.bad_end_count++;
	}

	sbus_decode_channels(frame, ctx->state.channels);
	ctx->state.flags = frame[23];
	ctx->state.frame_valid = true;
	ctx->state.frame_count++;

	if ((ctx->state.frame_count % CONFIG_DAJILOG_SBUS_LOG_PERIOD) != 0) {
		return;
	}

	printk("SBUS frame=%lu ch1=%u ch2=%u ch3=%u ch4=%u ch5=%u ch6=%u "
	       "ch7=%u ch8=%u flags=0x%02x lost=%u failsafe=%u bad_end=%lu "
	       "sync_lost=%lu\r\n",
	       (unsigned long)ctx->state.frame_count, ctx->state.channels[0],
	       ctx->state.channels[1], ctx->state.channels[2],
	       ctx->state.channels[3], ctx->state.channels[4],
	       ctx->state.channels[5], ctx->state.channels[6],
	       ctx->state.channels[7], ctx->state.flags,
	       (ctx->state.flags >> 2) & 1U, (ctx->state.flags >> 3) & 1U,
	       (unsigned long)ctx->state.bad_end_count,
	       (unsigned long)ctx->state.sync_lost_count);
}

static void sbus_thread(void *arg1, void *arg2, void *arg3)
{
	struct sbus *ctx = arg1;
	uint8_t frame[SBUS_FRAME_LEN];
	size_t pos = 0;
	uint8_t byte;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		if (uart_poll_in(ctx->config.uart, &byte) != 0) {
			k_sleep(K_MSEC(1));
			continue;
		}

		if (pos == 0) {
			if (byte != SBUS_START_BYTE) {
				ctx->state.sync_lost_count++;
				continue;
			}
			frame[pos++] = byte;
			continue;
		}

		frame[pos++] = byte;
		if (pos >= SBUS_FRAME_LEN) {
			sbus_process_frame(ctx, frame);
			pos = 0;
		}
	}
}

int sbus_init(struct sbus *ctx)
{
	const struct uart_config cfg = {
		.baudrate = 100000,
		.parity = UART_CFG_PARITY_EVEN,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ctx->state.ready = false;
	ctx->state.thread_started = false;
	ctx->state.frame_valid = false;
	ctx->state.last_error = 0;
	ctx->state.frame_count = 0;
	ctx->state.sync_lost_count = 0;
	ctx->state.bad_end_count = 0;

	if (ctx->config.uart == NULL || !device_is_ready(ctx->config.uart)) {
		ctx->state.last_error = -ENODEV;
		printk("SBUS UART device is not ready\r\n");
		return -ENODEV;
	}

	ret = uart_configure(ctx->config.uart, &cfg);
	if (ret != 0) {
		ctx->state.last_error = ret;
		printk("SBUS UART configure failed ret=%d\r\n", ret);
		return ret;
	}

	ctx->state.ready = true;
	printk("SBUS component ready: baud=100000 8E2 inverted_by_hardware=%d\r\n",
	       ctx->config.inverted_by_hardware);

	return 0;
}

int sbus_start(struct sbus *ctx)
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

	k_thread_create(&sbus_thread_data, sbus_thread_stack,
			K_THREAD_STACK_SIZEOF(sbus_thread_stack), sbus_thread, ctx,
			NULL, NULL, CONFIG_DAJILOG_SBUS_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	ctx->state.thread_started = true;

	return 0;
}

void sbus_state_get(const struct sbus *ctx, struct sbus_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
}
