/* SPDX-License-Identifier: Apache-2.0 */

#include "can_protocol.h"

#include "controller_state.h"
#include "ejection_io.h"
#include "manual_control.h"
#include "phase3_devices.h"
#include "ejection_scheduler.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define PROTOCOL_THREAD_STACK_SIZE 1280
#define PROTOCOL_THREAD_PRIORITY 5
#define PROTOCOL_RX_QUEUE_DEPTH 64

static const struct device *const can_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

struct protocol_rx {
	struct can_frame frame;
	int64_t rx_at;
	uint32_t epoch;
};
K_MSGQ_DEFINE(protocol_rx_queue, sizeof(struct protocol_rx),
	      PROTOCOL_RX_QUEUE_DEPTH, 4);
/* ALL OFF never competes for the bounded data queue's last slot. */
K_MSGQ_DEFINE(protocol_cancel_queue, sizeof(struct protocol_rx), 1, 4);
K_SEM_DEFINE(protocol_rx_wake, 0, 1);
K_THREAD_STACK_DEFINE(protocol_thread_stack, PROTOCOL_THREAD_STACK_SIZE);
static struct k_thread protocol_thread_data;
static struct k_work_delayable status_work;
static struct k_work_delayable heartbeat_timeout_work;
static struct can_protocol_status protocol_status;
static uint8_t status_sequence;
static int protocol_filter_id = -1;
K_MUTEX_DEFINE(protocol_mutex);

static atomic_t rx_count;
static atomic_t command_count;
static atomic_t heartbeat_count;
static atomic_t echo_count;
static atomic_t status_count;
static atomic_t reject_count;
static atomic_t duplicate_count;
static atomic_t dropped_count;
static atomic_t tx_error_count;

static uint8_t state_wire_value(enum controller_state state)
{
	switch (state) {
	case CONTROLLER_STATE_BOOT: return 0U;
	case CONTROLLER_STATE_SELF_TEST: return 1U;
	case CONTROLLER_STATE_IDLE: return 2U;
	case CONTROLLER_STATE_READY: return 3U;
	case CONTROLLER_STATE_RUNNING: return 4U;
	case CONTROLLER_STATE_FAULT: return 5U;
	default: return 0xffU;
	}
}

static uint8_t fault_wire_value(enum controller_fault fault)
{
	switch (fault) {
	case CONTROLLER_FAULT_NONE: return 0U;
	case CONTROLLER_FAULT_IO_INIT: return 1U;
	case CONTROLLER_FAULT_CAN_INIT: return 2U;
	case CONTROLLER_FAULT_OLED_INIT: return 3U;
	case CONTROLLER_FAULT_EEPROM_INIT: return 4U;
	case CONTROLLER_FAULT_INVALID_CONFIG: return 5U;
	case CONTROLLER_FAULT_CAN_TIMEOUT: return 6U;
	case CONTROLLER_FAULT_CAN_BUS_OFF: return 7U;
	case CONTROLLER_FAULT_EMERGENCY_STOP: return 8U;
	default: return 0xffU;
	}
}

static uint16_t input_mask_get(void)
{
	uint16_t mask = 0U;

	for (size_t i = 0; i < EJECTION_INPUT_COUNT; i++) {
		if (ejection_input_get(i)) {
			mask |= BIT(i);
		}
	}
	return mask;
}

static void protocol_rx_callback(const struct device *dev,
				 struct can_frame *frame, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);
	struct protocol_rx rx = {
		.frame = *frame,
		.rx_at = k_uptime_get(),
		.epoch = ejection_scheduler_epoch(),
	};
	phase3_can_observe_rx(frame);
	atomic_inc(&rx_count);
	bool cancel = frame->id == CAN_PROTOCOL_OUTPUT_ID && frame->flags == 0U &&
		      frame->dlc == 8U;
	for (size_t i = 0; cancel && i < 8; i++) {
		cancel = frame->data[i] == 0;
	}
	if (cancel) {
		/* Invalidate pending registrations immediately, even while Echo TX waits. */
		ejection_scheduler_invalidate();
		struct protocol_rx discarded;
		(void)k_msgq_get(&protocol_cancel_queue, &discarded, K_NO_WAIT);
		(void)k_msgq_put(&protocol_cancel_queue, &rx, K_NO_WAIT);
	} else if (k_msgq_put(&protocol_rx_queue, &rx, K_NO_WAIT) != 0) {
		atomic_inc(&dropped_count);
	}
	k_sem_give(&protocol_rx_wake);
}

static bool frame_is_standard_data(const struct can_frame *frame)
{
	return (frame->flags & (CAN_FRAME_IDE | CAN_FRAME_RTR)) == 0U;
}

static void start_status_publishing(void)
{
	k_mutex_lock(&protocol_mutex, K_FOREVER);
	protocol_status.status_publishing = true;
	k_mutex_unlock(&protocol_mutex);
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
}

static void status_change_callback(void *user_data)
{
	ARG_UNUSED(user_data);
	bool active;

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	active = protocol_status.status_publishing;
	k_mutex_unlock(&protocol_mutex);
	if (active) {
		(void)k_work_reschedule(&status_work, K_NO_WAIT);
	}
}

static void handle_heartbeat(const struct can_frame *frame)
{
	if (!frame_is_standard_data(frame) || can_dlc_to_bytes(frame->dlc) != 2U ||
	    frame->data[0] != CAN_PROTOCOL_VERSION) {
		atomic_inc(&reject_count);
		return;
	}

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	if (protocol_status.link_active &&
	    protocol_status.last_heartbeat_sequence == frame->data[1]) {
		atomic_inc(&duplicate_count);
	}
	protocol_status.link_active = true;
	protocol_status.last_heartbeat_sequence = frame->data[1];
	protocol_status.last_heartbeat_ms = k_uptime_get();
	k_mutex_unlock(&protocol_mutex);
	atomic_inc(&heartbeat_count);
	controller_state_set_run_permission(true);
	(void)k_work_reschedule(&heartbeat_timeout_work,
				K_MSEC(CAN_PROTOCOL_HEARTBEAT_TIMEOUT_MS));
	start_status_publishing();
}

static bool output_frame_valid(const struct can_frame *frame, uint16_t *mask)
{
	if (!frame_is_standard_data(frame) || can_dlc_to_bytes(frame->dlc) != 8U) {
		return false;
	}
	for (size_t i = 2; i < 8; i++) {
		if (frame->data[i] != 0U) {
			return false;
		}
	}
	if ((frame->data[1] & 0xf0U) != 0U) {
		return false;
	}
	*mask = (uint16_t)frame->data[0] | ((uint16_t)frame->data[1] << 8);
	return true;
}

static void handle_output_command(const struct protocol_rx *rx)
{
	const struct can_frame *frame = &rx->frame;
	uint16_t mask;

	if (!output_frame_valid(frame, &mask) || manual_control_is_active()) {
		atomic_inc(&reject_count);
		return;
	}

	int ret = mask == 0U ? ejection_scheduler_cancel_all() :
		ejection_scheduler_submit(mask, rx->rx_at, rx->epoch);
	if (ret != 0) {
		atomic_inc(&reject_count);
		return;
	}

	atomic_inc(&command_count);
	start_status_publishing();

	ret = phase3_can_send(CAN_PROTOCOL_OUTPUT_ID, frame->data, 8U);
	if (ret == 0) {
		atomic_inc(&echo_count);
	} else {
		atomic_inc(&tx_error_count);
	}
	(void)k_work_reschedule(&status_work, K_NO_WAIT);
}

static void protocol_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	struct protocol_rx rx;

	while (true) {
		if (k_msgq_get(&protocol_cancel_queue, &rx, K_NO_WAIT) != 0 &&
		    k_msgq_get(&protocol_rx_queue, &rx, K_NO_WAIT) != 0) {
			(void)k_sem_take(&protocol_rx_wake, K_FOREVER);
			continue;
		}
		if (rx.frame.id == CAN_PROTOCOL_OUTPUT_ID) {
			handle_output_command(&rx);
		} else if (rx.frame.id == CAN_PROTOCOL_HEARTBEAT_ID) {
			handle_heartbeat(&rx.frame);
		}
	}
}

int can_protocol_send_status(void)
{
	struct controller_state_snapshot controller;
	controller_state_get(&controller);
	uint16_t inputs = input_mask_get();
	uint16_t outputs = ejection_output_mask_get();
	uint8_t data[8] = {
		CAN_PROTOCOL_VERSION,
		status_sequence++,
		state_wire_value(controller.state),
		fault_wire_value(controller.fault),
		(uint8_t)inputs,
		(uint8_t)(inputs >> 8),
		(uint8_t)outputs,
		(uint8_t)(outputs >> 8),
	};

	if (controller.prerequisites.settings_ready) {
		data[7] |= BIT(4);
	}
	if (controller.prerequisites.can_ready) {
		data[7] |= BIT(5);
	}
	if (controller.prerequisites.encoder_ready) {
		data[7] |= BIT(6);
	}
	if (controller.prerequisites.oled_ready) {
		data[7] |= BIT(7);
	}

	int ret = phase3_can_send(CAN_PROTOCOL_STATUS_ID, data, sizeof(data));
	if (ret == 0) {
		atomic_inc(&status_count);
	} else {
		atomic_inc(&tx_error_count);
		enum can_state can_state;
		struct can_bus_err_cnt err_count;
		if (can_get_state(can_dev, &can_state, &err_count) == 0 &&
		    can_state == CAN_STATE_BUS_OFF) {
			k_mutex_lock(&protocol_mutex, K_FOREVER);
			protocol_status.status_publishing = false;
			k_mutex_unlock(&protocol_mutex);
			controller_state_set_run_permission(false);
			(void)controller_state_enter_fault(CONTROLLER_FAULT_CAN_BUS_OFF);
		}
	}
	return ret;
}

static void status_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	bool active;

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	active = protocol_status.status_publishing;
	k_mutex_unlock(&protocol_mutex);
	if (!active) {
		return;
	}

	(void)can_protocol_send_status();
	(void)k_work_reschedule(&status_work,
				K_MSEC(CAN_PROTOCOL_STATUS_PERIOD_MS));
}

static void heartbeat_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	bool required;

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	protocol_status.link_active = false;
	required = protocol_status.heartbeat_required;
	k_mutex_unlock(&protocol_mutex);
	if (!required) {
		return;
	}
	controller_state_set_run_permission(false);

	struct controller_state_snapshot controller;
	controller_state_get(&controller);
	if (controller.state == CONTROLLER_STATE_RUNNING) {
		(void)controller_state_enter_fault(CONTROLLER_FAULT_CAN_TIMEOUT);
		(void)can_protocol_send_status();
	}
}

int can_protocol_init(void)
{
	if (!device_is_ready(can_dev)) {
		return -ENODEV;
	}

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	if (protocol_status.initialized) {
		k_mutex_unlock(&protocol_mutex);
		return 0;
	}
	memset(&protocol_status, 0, sizeof(protocol_status));
	k_mutex_unlock(&protocol_mutex);
	/* Heartbeat is optional until its use is agreed with the Vision PC vendor. */
	controller_state_set_run_permission(true);
	k_work_init_delayable(&status_work, status_work_handler);
	k_work_init_delayable(&heartbeat_timeout_work, heartbeat_timeout_handler);

	/* One catch-all hardware filter dispatches frames in software.  Registering
	 * an earlier catch-all monitor and later per-ID filters causes STM32 FDCAN
	 * to deliver a matching frame only to the first filter callback.
	 */
	const struct can_filter protocol_filter = {
		.id = 0U,
		.mask = 0U,
	};
	protocol_filter_id = can_add_rx_filter(can_dev, protocol_rx_callback, NULL,
					       &protocol_filter);
	if (protocol_filter_id < 0) {
		return protocol_filter_id;
	}

	k_thread_create(&protocol_thread_data, protocol_thread_stack,
			K_THREAD_STACK_SIZEOF(protocol_thread_stack), protocol_thread,
			NULL, NULL, NULL, PROTOCOL_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&protocol_thread_data, "can_protocol");
	k_mutex_lock(&protocol_mutex, K_FOREVER);
	protocol_status.initialized = true;
	k_mutex_unlock(&protocol_mutex);
	ejection_io_set_change_callback(status_change_callback, NULL);
	controller_state_set_change_callback(status_change_callback, NULL);
	return 0;
}

void can_protocol_set_heartbeat_required(bool required)
{
	bool link_active;

	k_mutex_lock(&protocol_mutex, K_FOREVER);
	protocol_status.heartbeat_required = required;
	link_active = protocol_status.link_active;
	k_mutex_unlock(&protocol_mutex);
	controller_state_set_run_permission(!required || link_active);
}

void can_protocol_get_status(struct can_protocol_status *status)
{
	if (status == NULL) {
		return;
	}
	k_mutex_lock(&protocol_mutex, K_FOREVER);
	*status = protocol_status;
	k_mutex_unlock(&protocol_mutex);
	status->rx_count = atomic_get(&rx_count);
	status->command_count = atomic_get(&command_count);
	status->heartbeat_count = atomic_get(&heartbeat_count);
	status->echo_count = atomic_get(&echo_count);
	status->status_count = atomic_get(&status_count);
	status->reject_count = atomic_get(&reject_count);
	status->duplicate_count = atomic_get(&duplicate_count);
	status->dropped_count = atomic_get(&dropped_count);
	status->tx_error_count = atomic_get(&tx_error_count);
}
