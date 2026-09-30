/* SPDX-License-Identifier: Apache-2.0 */

#include "tic_can.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "tic_fsm.h"
#include "tic_input.h"
#include "tic_output.h"

#define TIC_CAN_COMMAND_ID 0x18FF0001U
#define TIC_CAN_STATUS_ID 0x18FF0100U
#define TIC_CAN_PERIOD_MS 200
#define TIC_CAN_TIMEOUT_MS 2000
#define TIC_CAN_THREAD_STACK_SIZE 1536
#define TIC_CAN_THREAD_PRIORITY 4
#define TIC_CAN_RX_QUEUE_DEPTH 8

enum tic_can_command {
	TIC_CMD_AUTO_OFF = 0x00,
	TIC_CMD_AUTO_ON = 0x01,
	TIC_CMD_START = 0x02,
	TIC_CMD_ALL_OFF = 0x03,
	TIC_CMD_ENGINE_STOP = 0x04,
};

static const struct device *const can_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
K_MSGQ_DEFINE(can_rx_queue, sizeof(struct can_frame), TIC_CAN_RX_QUEUE_DEPTH,
	      sizeof(uint32_t));
K_THREAD_STACK_DEFINE(can_thread_stack, TIC_CAN_THREAD_STACK_SIZE);
static struct k_thread can_thread_data;
static struct tic_can_snapshot snapshot;
K_MUTEX_DEFINE(can_snapshot_mutex);
static atomic_t rx_count;
static atomic_t rx_drop_count;
static atomic_t rx_invalid_count;
static atomic_t tx_count;
static atomic_t tx_error_count;
static int rx_filter_id = -ENODEV;
static bool initialized;

const char *tic_can_state_name(enum can_state state)
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
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	atomic_inc(&rx_count);
	if (k_msgq_put(&can_rx_queue, frame, K_NO_WAIT) != 0) {
		atomic_inc(&rx_drop_count);
	}
}

static void post_simple_event(enum tic_event_type type, uint8_t command_code)
{
	const struct tic_event event = {
		.type = type,
		.command_code = command_code,
		.source = TIC_CONTROL_CAN,
	};

	(void)tic_fsm_post(&event, K_MSEC(20));
}

static bool command_frame_format_valid(const struct can_frame *frame)
{
	if (frame->id != TIC_CAN_COMMAND_ID || frame->dlc != 8 ||
	    (frame->flags & CAN_FRAME_IDE) == 0) {
		return false;
	}
	if (frame->data[0] > TIC_CMD_ENGINE_STOP) {
		return false;
	}
	for (size_t i = 2; i < 8; i++) {
		if (frame->data[i] != 0) {
			return false;
		}
	}
	if (frame->data[0] != TIC_CMD_START && frame->data[1] != 0) {
		return false;
	}
	return true;
}

static void process_command_frame(const struct can_frame *frame,
				  int64_t *last_valid_rx_ms)
{
	uint8_t command;
	struct tic_event event;

	if (!command_frame_format_valid(frame)) {
		atomic_inc(&rx_invalid_count);
		return;
	}

	command = frame->data[0];
	*last_valid_rx_ms = k_uptime_get();
	post_simple_event(TIC_EVENT_CAN_VALID, command);

	switch (command) {
	case TIC_CMD_AUTO_OFF:
		post_simple_event(TIC_EVENT_AUTO_DISABLE, command);
		break;
	case TIC_CMD_AUTO_ON:
		post_simple_event(TIC_EVENT_AUTO_ENABLE, command);
		break;
	case TIC_CMD_START:
		/* START never acquires AUTO control; FSM rejects it unless ready. */
		event = (struct tic_event){
			.type = TIC_EVENT_START_REQUEST,
			.crank_time_ms = (uint16_t)frame->data[1] * 100U,
			.command_code = command,
			.source = TIC_CONTROL_CAN,
		};
		(void)tic_fsm_post(&event, K_MSEC(20));
		break;
	case TIC_CMD_ALL_OFF:
		post_simple_event(TIC_EVENT_ALL_OFF, command);
		break;
	case TIC_CMD_ENGINE_STOP:
		post_simple_event(TIC_EVENT_STOP_REQUEST, command);
		break;
	default:
		break;
	}
}

static int send_status_frame(void)
{
	struct tic_fsm_snapshot fsm;
	struct tic_input_snapshot input;
	struct tic_output_snapshot output;
	struct can_frame frame = {
		.id = TIC_CAN_STATUS_ID,
		.dlc = 8,
		.flags = CAN_FRAME_IDE,
	};
	int ret;

	tic_fsm_get_snapshot(&fsm);
	tic_input_get_snapshot(&input);
	tic_output_get_snapshot(&output);

	frame.data[0] = (input.di1_key_on ? BIT(0) : 0U) |
			(input.di2_manual_crank ? BIT(1) : 0U);
	frame.data[1] = (output.r1_key_on ? BIT(0) : 0U) |
			(output.r2_restart ? BIT(1) : 0U) |
			(output.r3_manual_lockout ? BIT(2) : 0U) |
			(output.r4_engine_stop ? BIT(3) : 0U);
	frame.data[2] = (fsm.auto_enabled ? BIT(0) : 0U) |
			(fsm.state == TIC_STATE_AUTO_READY ? BIT(1) : 0U) |
			(fsm.start_accepted ? BIT(2) : 0U) |
			(fsm.state == TIC_STATE_AUTO_CRANKING ? BIT(3) : 0U) |
			(fsm.start_complete ? BIT(4) : 0U) |
			(fsm.start_rejected ? BIT(5) : 0U) |
			(fsm.command_valid ? BIT(6) : 0U) |
			(fsm.can_timeout ? BIT(7) : 0U);
	frame.data[3] = (uint8_t)fsm.state;
	frame.data[4] = fsm.fault != TIC_FAULT_NONE ?
			(uint8_t)(0x80U | (uint8_t)fsm.fault) :
			(uint8_t)fsm.inhibit;
	frame.data[5] = fsm.last_command_code;
	frame.data[6] = (fsm.stop_accepted ? BIT(0) : 0U) |
			(fsm.stop_complete ? BIT(1) : 0U) |
			(fsm.stop_rejected ? BIT(2) : 0U);
	frame.data[7] = (uint8_t)MIN(DIV_ROUND_UP(fsm.crank_remaining_ms, 100U),
				     UINT8_MAX);

	ret = can_send(can_dev, &frame, K_MSEC(50), NULL, NULL);
	if (ret == 0) {
		atomic_inc(&tx_count);
	} else {
		atomic_inc(&tx_error_count);
	}
	return ret;
}

static void update_can_snapshot(bool command_received, bool timeout,
				enum can_state state,
				const struct can_bus_err_cnt *error_count,
				int last_error)
{
	k_mutex_lock(&can_snapshot_mutex, K_FOREVER);
	snapshot.ready = true;
	snapshot.command_received = command_received;
	snapshot.timeout = timeout;
	snapshot.bus_state = state;
	snapshot.error_count = *error_count;
	snapshot.rx_count = (uint32_t)atomic_get(&rx_count);
	snapshot.rx_drop_count = (uint32_t)atomic_get(&rx_drop_count);
	snapshot.rx_invalid_count = (uint32_t)atomic_get(&rx_invalid_count);
	snapshot.tx_count = (uint32_t)atomic_get(&tx_count);
	snapshot.tx_error_count = (uint32_t)atomic_get(&tx_error_count);
	snapshot.last_error = last_error;
	k_mutex_unlock(&can_snapshot_mutex);
}

static void can_thread(void *arg1, void *arg2, void *arg3)
{
	struct can_frame frame;
	struct can_bus_err_cnt error_count = { 0 };
	enum can_state bus_state = CAN_STATE_STOPPED;
	int64_t next_status_ms = k_uptime_get() + TIC_CAN_PERIOD_MS;
	int64_t last_valid_rx_ms = 0;
	bool command_received = false;
	bool timeout_reported = false;
	bool bus_off_reported = false;
	int last_error = 0;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		struct tic_fsm_snapshot fsm;
		int ret = k_msgq_get(&can_rx_queue, &frame, K_MSEC(20));
		int64_t now = k_uptime_get();

		if (ret == 0) {
			int64_t before = last_valid_rx_ms;

			process_command_frame(&frame, &last_valid_rx_ms);
			if (last_valid_rx_ms != before) {
				command_received = true;
				timeout_reported = false;
			}
		}

		if (now >= next_status_ms) {
			last_error = send_status_frame();
			next_status_ms += TIC_CAN_PERIOD_MS;
			if (now >= next_status_ms) {
				next_status_ms = now + TIC_CAN_PERIOD_MS;
			}
		}

		ret = can_get_state(can_dev, &bus_state, &error_count);
		if (ret != 0) {
			last_error = ret;
		}
		if (bus_state == CAN_STATE_BUS_OFF && !bus_off_reported) {
			post_simple_event(TIC_EVENT_CAN_BUS_OFF, 0);
			bus_off_reported = true;
		} else if (bus_state != CAN_STATE_BUS_OFF) {
			bus_off_reported = false;
		}

		tic_fsm_get_snapshot(&fsm);
		if (command_received && fsm.auto_enabled &&
		    fsm.auto_control_source == TIC_CONTROL_CAN &&
		    now - last_valid_rx_ms >= TIC_CAN_TIMEOUT_MS &&
		    !timeout_reported) {
			post_simple_event(TIC_EVENT_CAN_TIMEOUT, 0);
			timeout_reported = true;
		}
		if (!fsm.auto_enabled && !fsm.can_timeout) {
			timeout_reported = false;
		}

		update_can_snapshot(command_received, timeout_reported, bus_state,
				    &error_count, last_error);
	}
}

int tic_can_init(void)
{
	const struct can_filter filter = {
		.id = TIC_CAN_COMMAND_ID,
		.mask = CAN_EXT_ID_MASK,
		.flags = CAN_FILTER_IDE,
	};
	int ret;

	if (!device_is_ready(can_dev)) {
		return -ENODEV;
	}

	ret = can_start(can_dev);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}

	rx_filter_id = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);
	if (rx_filter_id < 0) {
		return rx_filter_id;
	}

	k_mutex_lock(&can_snapshot_mutex, K_FOREVER);
	snapshot = (struct tic_can_snapshot){
		.ready = true,
	};
	k_mutex_unlock(&can_snapshot_mutex);
	initialized = true;
	return 0;
}

int tic_can_start(void)
{
	if (!initialized) {
		return -EAGAIN;
	}

	k_thread_create(&can_thread_data, can_thread_stack,
			K_THREAD_STACK_SIZEOF(can_thread_stack), can_thread,
			NULL, NULL, NULL, TIC_CAN_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&can_thread_data, "tic_can");
	return 0;
}

void tic_can_get_snapshot(struct tic_can_snapshot *out)
{
	if (out == NULL) {
		return;
	}
	k_mutex_lock(&can_snapshot_mutex, K_FOREVER);
	*out = snapshot;
	k_mutex_unlock(&can_snapshot_mutex);
}
