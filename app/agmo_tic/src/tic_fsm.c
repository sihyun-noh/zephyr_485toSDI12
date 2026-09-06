/* SPDX-License-Identifier: Apache-2.0 */

#include "tic_fsm.h"

#include <zephyr/kernel.h>

#include "tic_output.h"

#define FSM_THREAD_STACK_SIZE 1536
#define FSM_THREAD_PRIORITY 3
#define FSM_EVENT_QUEUE_DEPTH 16
#define AUTO_ARM_DELAY_MS 100
#define CRANK_MIN_MS 1000
#define CRANK_MAX_MS 3000
#define ENGINE_STOP_HOLD_MS 5000
#define KEY_RECOVERY_WAIT_MS 200

struct tic_fsm_context {
	struct tic_fsm_snapshot snapshot;
	int64_t crank_deadline_ms;
};

K_MSGQ_DEFINE(fsm_event_queue, sizeof(struct tic_event), FSM_EVENT_QUEUE_DEPTH,
	      sizeof(uint32_t));
K_THREAD_STACK_DEFINE(fsm_thread_stack, FSM_THREAD_STACK_SIZE);
static struct k_thread fsm_thread_data;
static struct tic_fsm_context ctx = {
	.snapshot = {
		.state = TIC_STATE_INIT,
	},
};
static struct tic_fsm_snapshot public_snapshot = {
	.state = TIC_STATE_INIT,
};
static int64_t public_crank_deadline_ms;
K_MUTEX_DEFINE(fsm_snapshot_mutex);

static void arm_delay_handler(struct k_work *work);
static void crank_timeout_handler(struct k_work *work);
static void stop_timeout_handler(struct k_work *work);
static void key_recovery_timeout_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(arm_delay_work, arm_delay_handler);
K_WORK_DELAYABLE_DEFINE(crank_timeout_work, crank_timeout_handler);
K_WORK_DELAYABLE_DEFINE(stop_timeout_work, stop_timeout_handler);
K_WORK_DELAYABLE_DEFINE(key_recovery_timeout_work,
			 key_recovery_timeout_handler);

const char *tic_fsm_state_name(enum tic_state state)
{
	switch (state) {
	case TIC_STATE_INIT:
		return "INIT";
	case TIC_STATE_SAFE_OFF:
		return "SAFE_OFF";
	case TIC_STATE_KEY_DETECTED:
		return "KEY_DETECTED";
	case TIC_STATE_AUTO_ARMING:
		return "AUTO_ARMING";
	case TIC_STATE_AUTO_READY:
		return "AUTO_READY";
	case TIC_STATE_AUTO_CRANKING:
		return "AUTO_CRANKING";
	case TIC_STATE_AUTO_COMPLETE:
		return "AUTO_COMPLETE";
	case TIC_STATE_ENGINE_STOPPING:
		return "ENGINE_STOPPING";
	case TIC_STATE_FAULT:
		return "FAULT";
	default:
		return "UNKNOWN";
	}
}

static void snapshot_publish(void)
{
	if (ctx.snapshot.state == TIC_STATE_AUTO_CRANKING) {
		int64_t remaining = ctx.crank_deadline_ms - k_uptime_get();

		ctx.snapshot.crank_remaining_ms =
			remaining > 0 ? (uint16_t)MIN(remaining, UINT16_MAX) : 0;
	} else {
		ctx.snapshot.crank_remaining_ms = 0;
	}

	k_mutex_lock(&fsm_snapshot_mutex, K_FOREVER);
	public_snapshot = ctx.snapshot;
	public_crank_deadline_ms = ctx.crank_deadline_ms;
	k_mutex_unlock(&fsm_snapshot_mutex);
}

static void set_fault(enum tic_fault_code fault)
{
	(void)k_work_cancel_delayable(&arm_delay_work);
	(void)k_work_cancel_delayable(&crank_timeout_work);
	(void)k_work_cancel_delayable(&stop_timeout_work);
	(void)k_work_cancel_delayable(&key_recovery_timeout_work);
	(void)tic_output_all_off();
	ctx.snapshot.auto_enabled = false;
	ctx.snapshot.auto_control_source = TIC_CONTROL_NONE;
	ctx.snapshot.fault = fault;
	ctx.snapshot.state = TIC_STATE_FAULT;
	ctx.crank_deadline_ms = 0;
	tic_output_sync_auto_leds(ctx.snapshot.key_on);
}

static void release_auto(bool can_timeout)
{
	(void)k_work_cancel_delayable(&arm_delay_work);
	(void)k_work_cancel_delayable(&crank_timeout_work);
	(void)k_work_cancel_delayable(&key_recovery_timeout_work);
	(void)tic_output_all_off();
	ctx.snapshot.auto_enabled = false;
	ctx.snapshot.auto_control_source = TIC_CONTROL_NONE;
	ctx.snapshot.start_accepted = false;
	ctx.snapshot.start_complete = false;
	ctx.snapshot.start_rejected = false;
	ctx.snapshot.stop_accepted = false;
	ctx.snapshot.stop_complete = false;
	ctx.snapshot.stop_rejected = false;
	ctx.snapshot.can_timeout = can_timeout;
	if (can_timeout) {
		ctx.snapshot.command_valid = false;
	}
	ctx.snapshot.fault = can_timeout ? TIC_FAULT_CAN_TIMEOUT : TIC_FAULT_NONE;
	ctx.snapshot.state = ctx.snapshot.key_on ? TIC_STATE_KEY_DETECTED :
						       TIC_STATE_SAFE_OFF;
	ctx.crank_deadline_ms = 0;
	tic_output_sync_auto_leds(ctx.snapshot.key_on);
}

static void key_off(void)
{
	ctx.snapshot.key_on = false;
	ctx.snapshot.manual_crank = false;
	ctx.snapshot.inhibit = TIC_INHIBIT_NONE;
	release_auto(false);
	ctx.snapshot.state = TIC_STATE_SAFE_OFF;
	tic_output_sync_auto_leds(false);
}

static void process_event(const struct tic_event *event)
{
	int ret;

	switch (event->type) {
	case TIC_EVENT_BOOT_COMPLETE:
		(void)tic_output_all_off();
		ctx.snapshot.state = TIC_STATE_SAFE_OFF;
		break;

	case TIC_EVENT_KEY_ON:
		(void)k_work_cancel_delayable(&key_recovery_timeout_work);
		ctx.snapshot.key_on = true;
		ctx.snapshot.can_timeout = false;
		if (ctx.snapshot.state == TIC_STATE_SAFE_OFF ||
		    ctx.snapshot.state == TIC_STATE_INIT ||
		    ctx.snapshot.state == TIC_STATE_FAULT) {
			ctx.snapshot.state = TIC_STATE_KEY_DETECTED;
			ctx.snapshot.fault = TIC_FAULT_NONE;
			ctx.snapshot.inhibit = TIC_INHIBIT_NONE;
		}
		tic_output_sync_auto_leds(true);
		break;

	case TIC_EVENT_KEY_OFF:
		if (ctx.snapshot.state == TIC_STATE_ENGINE_STOPPING) {
			ctx.snapshot.key_on = false;
			ctx.snapshot.manual_crank = false;
			tic_output_sync_auto_leds(false);
		} else {
			key_off();
		}
		break;

	case TIC_EVENT_MANUAL_CRANK_ON:
		ctx.snapshot.manual_crank = true;
		break;

	case TIC_EVENT_MANUAL_CRANK_OFF:
		ctx.snapshot.manual_crank = false;
		break;

	case TIC_EVENT_CAN_VALID:
		ctx.snapshot.command_valid = true;
		ctx.snapshot.can_timeout = false;
		ctx.snapshot.last_command_code = event->command_code;
		if (ctx.snapshot.fault == TIC_FAULT_CAN_TIMEOUT) {
			ctx.snapshot.fault = TIC_FAULT_NONE;
		}
		break;

	case TIC_EVENT_AUTO_ENABLE:
		ctx.snapshot.last_command_code = event->command_code;
		if (ctx.snapshot.state == TIC_STATE_ENGINE_STOPPING) {
			break;
		}
		if (!ctx.snapshot.key_on) {
			ctx.snapshot.inhibit = TIC_INHIBIT_KEY_OFF;
			break;
		}
		if (ctx.snapshot.manual_crank) {
			ctx.snapshot.inhibit = TIC_INHIBIT_MANUAL_CRANK_ACTIVE;
			break;
		}
		if (ctx.snapshot.state == TIC_STATE_KEY_DETECTED) {
			ctx.snapshot.start_accepted = false;
			ctx.snapshot.start_complete = false;
			ctx.snapshot.start_rejected = false;
			ctx.snapshot.stop_accepted = false;
			ctx.snapshot.stop_complete = false;
			ctx.snapshot.stop_rejected = false;
			ret = tic_output_set_r1(true);
			if (ret != 0) {
				set_fault(TIC_FAULT_OUTPUT);
				break;
			}
			ctx.snapshot.auto_enabled = true;
			ctx.snapshot.auto_control_source = event->source;
			ctx.snapshot.inhibit = TIC_INHIBIT_NONE;
			ctx.snapshot.state = TIC_STATE_AUTO_ARMING;
			(void)k_work_reschedule(&arm_delay_work,
						K_MSEC(AUTO_ARM_DELAY_MS));
		}
		break;

	case TIC_EVENT_AUTO_ARM_DELAY_DONE:
		if (ctx.snapshot.state != TIC_STATE_AUTO_ARMING) {
			break;
		}
		if (!ctx.snapshot.key_on || ctx.snapshot.manual_crank) {
			release_auto(false);
			break;
		}
		ret = tic_output_set_r3(true);
		if (ret != 0) {
			set_fault(TIC_FAULT_OUTPUT);
			break;
		}
		ctx.snapshot.state = TIC_STATE_AUTO_READY;
		break;

	case TIC_EVENT_START_REQUEST:
		ctx.snapshot.last_command_code = event->command_code;
		if (ctx.snapshot.start_accepted) {
			/* ACK remains asserted; duplicate retry must not crank again. */
			break;
		}
		if (!ctx.snapshot.key_on || !ctx.snapshot.auto_enabled ||
		    ctx.snapshot.manual_crank ||
		    (ctx.snapshot.state != TIC_STATE_AUTO_READY &&
		     ctx.snapshot.state != TIC_STATE_AUTO_COMPLETE)) {
			ctx.snapshot.inhibit = TIC_INHIBIT_AUTO_NOT_READY;
			ctx.snapshot.start_rejected = true;
			break;
		}
		if (event->crank_time_ms < CRANK_MIN_MS ||
		    event->crank_time_ms > CRANK_MAX_MS) {
			ctx.snapshot.inhibit = TIC_INHIBIT_CRANK_TIME;
			ctx.snapshot.start_rejected = true;
			break;
		}
		ret = tic_output_set_r2(true);
		if (ret != 0) {
			set_fault(TIC_FAULT_OUTPUT);
			break;
		}
		ctx.snapshot.inhibit = TIC_INHIBIT_NONE;
		ctx.snapshot.start_accepted = true;
		ctx.snapshot.start_complete = false;
		ctx.snapshot.start_rejected = false;
		ctx.snapshot.stop_accepted = false;
		ctx.snapshot.stop_complete = false;
		ctx.snapshot.stop_rejected = false;
		ctx.snapshot.state = TIC_STATE_AUTO_CRANKING;
		ctx.crank_deadline_ms = k_uptime_get() + event->crank_time_ms;
		(void)k_work_reschedule(&crank_timeout_work,
					K_MSEC(event->crank_time_ms));
		break;

	case TIC_EVENT_CRANK_TIMEOUT:
		if (ctx.snapshot.state == TIC_STATE_AUTO_CRANKING) {
			ret = tic_output_set_r2(false);
			if (ret != 0) {
				set_fault(TIC_FAULT_OUTPUT);
				break;
			}
			ctx.snapshot.state = TIC_STATE_AUTO_COMPLETE;
			ctx.snapshot.start_complete = true;
			ctx.crank_deadline_ms = 0;
		}
		break;

	case TIC_EVENT_STOP_REQUEST:
		ctx.snapshot.last_command_code = event->command_code;
		if (ctx.snapshot.stop_accepted) {
			/* Duplicate STOP retry: keep the original 5-second timer. */
			break;
		}
		if (!ctx.snapshot.key_on || !ctx.snapshot.auto_enabled ||
		    (ctx.snapshot.state != TIC_STATE_AUTO_READY &&
		     ctx.snapshot.state != TIC_STATE_AUTO_CRANKING &&
		     ctx.snapshot.state != TIC_STATE_AUTO_COMPLETE)) {
			ctx.snapshot.inhibit = TIC_INHIBIT_STOP_NOT_ALLOWED;
			ctx.snapshot.stop_rejected = true;
			break;
		}
		(void)k_work_cancel_delayable(&arm_delay_work);
		(void)k_work_cancel_delayable(&crank_timeout_work);
		ret = tic_output_set_r2(false);
		if (ret == 0) {
			ret = tic_output_set_r4(true);
		}
		if (ret != 0) {
			set_fault(TIC_FAULT_OUTPUT);
			break;
		}
		/* A valid STOP supersedes every previous START result. */
		ctx.snapshot.start_accepted = false;
		ctx.snapshot.start_complete = false;
		ctx.snapshot.start_rejected = false;
		ctx.snapshot.stop_accepted = true;
		ctx.snapshot.stop_complete = false;
		ctx.snapshot.stop_rejected = false;
		ctx.snapshot.inhibit = TIC_INHIBIT_NONE;
		ctx.snapshot.state = TIC_STATE_ENGINE_STOPPING;
		ctx.crank_deadline_ms = 0;
		(void)k_work_reschedule(&stop_timeout_work,
					K_MSEC(ENGINE_STOP_HOLD_MS));
		break;

	case TIC_EVENT_STOP_TIMEOUT:
		if (ctx.snapshot.state == TIC_STATE_ENGINE_STOPPING) {
			ret = tic_output_set_r4(false);
			if (ret != 0) {
				set_fault(TIC_FAULT_OUTPUT);
				break;
			}
			ctx.snapshot.stop_complete = true;
			/* Engine is stopped; allow a new START in the same AUTO session. */
			ctx.snapshot.start_accepted = false;
			ctx.snapshot.start_complete = false;
			ctx.snapshot.start_rejected = false;
			if (ctx.snapshot.can_timeout) {
				(void)tic_output_all_off();
				ctx.snapshot.auto_enabled = false;
				ctx.snapshot.auto_control_source = TIC_CONTROL_NONE;
				ctx.snapshot.state = TIC_STATE_SAFE_OFF;
			} else {
				ctx.snapshot.state = TIC_STATE_AUTO_READY;
				if (!ctx.snapshot.key_on) {
					(void)k_work_reschedule(
						&key_recovery_timeout_work,
						K_MSEC(KEY_RECOVERY_WAIT_MS));
				}
			}
		}
		break;

	case TIC_EVENT_KEY_RECOVERY_TIMEOUT:
		if (ctx.snapshot.auto_enabled && !ctx.snapshot.key_on) {
			key_off();
		}
		break;

	case TIC_EVENT_AUTO_DISABLE:
	case TIC_EVENT_ALL_OFF:
		if (ctx.snapshot.state != TIC_STATE_ENGINE_STOPPING) {
			release_auto(false);
		}
		break;

	case TIC_EVENT_CAN_TIMEOUT:
		if (ctx.snapshot.state == TIC_STATE_ENGINE_STOPPING) {
			ctx.snapshot.can_timeout = true;
			ctx.snapshot.command_valid = false;
			ctx.snapshot.fault = TIC_FAULT_CAN_TIMEOUT;
		} else {
			release_auto(true);
		}
		break;

	case TIC_EVENT_CAN_BUS_OFF:
		if (ctx.snapshot.state == TIC_STATE_ENGINE_STOPPING) {
			ctx.snapshot.fault = TIC_FAULT_CAN_BUS_OFF;
			ctx.snapshot.command_valid = false;
		} else {
			set_fault(TIC_FAULT_CAN_BUS_OFF);
		}
		break;

	case TIC_EVENT_INTERNAL_FAULT:
		set_fault(TIC_FAULT_INTERNAL);
		break;

	default:
		break;
	}

	tic_output_sync_auto_leds(ctx.snapshot.key_on);
	snapshot_publish();
}

static void fsm_thread(void *arg1, void *arg2, void *arg3)
{
	struct tic_event event;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		if (k_msgq_get(&fsm_event_queue, &event, K_FOREVER) == 0) {
			process_event(&event);
		}
	}
}

static void arm_delay_handler(struct k_work *work)
{
	const struct tic_event event = {
		.type = TIC_EVENT_AUTO_ARM_DELAY_DONE,
	};

	ARG_UNUSED(work);
	(void)tic_fsm_post(&event, K_MSEC(20));
}

static void crank_timeout_handler(struct k_work *work)
{
	const struct tic_event event = {
		.type = TIC_EVENT_CRANK_TIMEOUT,
	};

	ARG_UNUSED(work);
	(void)tic_fsm_post(&event, K_MSEC(20));
}

static void stop_timeout_handler(struct k_work *work)
{
	const struct tic_event event = {
		.type = TIC_EVENT_STOP_TIMEOUT,
	};

	ARG_UNUSED(work);
	(void)tic_fsm_post(&event, K_MSEC(20));
}

static void key_recovery_timeout_handler(struct k_work *work)
{
	const struct tic_event event = {
		.type = TIC_EVENT_KEY_RECOVERY_TIMEOUT,
	};

	ARG_UNUSED(work);
	(void)tic_fsm_post(&event, K_MSEC(20));
}

int tic_fsm_start(void)
{
	const struct tic_event boot_event = {
		.type = TIC_EVENT_BOOT_COMPLETE,
	};

	k_thread_create(&fsm_thread_data, fsm_thread_stack,
			K_THREAD_STACK_SIZEOF(fsm_thread_stack), fsm_thread,
			NULL, NULL, NULL, FSM_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&fsm_thread_data, "tic_fsm");
	return tic_fsm_post(&boot_event, K_MSEC(20));
}

int tic_fsm_post(const struct tic_event *event, k_timeout_t timeout)
{
	if (event == NULL) {
		return -EINVAL;
	}
	return k_msgq_put(&fsm_event_queue, event, timeout);
}

void tic_fsm_get_snapshot(struct tic_fsm_snapshot *out)
{
	if (out == NULL) {
		return;
	}

	k_mutex_lock(&fsm_snapshot_mutex, K_FOREVER);
	*out = public_snapshot;
	if (out->state == TIC_STATE_AUTO_CRANKING) {
		int64_t remaining = public_crank_deadline_ms - k_uptime_get();

		out->crank_remaining_ms =
			remaining > 0 ? (uint16_t)MIN(remaining, UINT16_MAX) : 0;
	}
	k_mutex_unlock(&fsm_snapshot_mutex);
}
