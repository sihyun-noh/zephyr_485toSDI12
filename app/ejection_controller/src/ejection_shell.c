/* SPDX-License-Identifier: Apache-2.0 */

#include "can_protocol.h"
#include "controller_state.h"
#include "ejection_io.h"
#include "ejection_scheduler.h"
#include "ejection_settings.h"
#include "phase3_devices.h"
#include "manual_control.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#if defined(CONFIG_EJECTION_SHELL_MANUAL_OUTPUT)
static int parse_channel(const struct shell *sh, const char *text,
			 size_t limit, size_t *channel)
{
	char *end;
	unsigned long value = strtoul(text, &end, 10);

	if (*text == '\0' || *end != '\0' || value == 0 || value > limit) {
		shell_error(sh, "channel must be 1..%u", (unsigned int)limit);
		return -EINVAL;
	}
	*channel = (size_t)value - 1U;
	return 0;
}
#endif

static int print_transition_result(const struct shell *sh, int ret)
{
	if (ret != 0) {
		shell_error(sh, "result=%d reason=state_transition_rejected", ret);
		return ret;
	}

	struct controller_state_snapshot snapshot;
	controller_state_get(&snapshot);
	phase3_ui_refresh();
	shell_print(sh, "result=0 state=%s sequence=%lu",
		    controller_state_name(snapshot.state),
		    (unsigned long)snapshot.sequence);
	return 0;
}

static int cmd_system_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct controller_state_snapshot snapshot;
	controller_state_get(&snapshot);
	shell_print(sh, "state=%s previous=%s fault=%s sequence=%lu age_ms=%lld",
		    controller_state_name(snapshot.state),
		    controller_state_name(snapshot.previous_state),
		    controller_fault_name(snapshot.fault),
		    (unsigned long)snapshot.sequence,
		    k_uptime_get() - snapshot.entered_at_ms);
	shell_print(sh, "ready.io=%u ready.settings=%u ready.can=%u ready.encoder=%u ready.oled=%u run_ok=%u",
		    snapshot.prerequisites.io_ready ? 1U : 0U,
		    snapshot.prerequisites.settings_ready ? 1U : 0U,
		    snapshot.prerequisites.can_ready ? 1U : 0U,
		    snapshot.prerequisites.encoder_ready ? 1U : 0U,
		    snapshot.prerequisites.oled_ready ? 1U : 0U,
		    snapshot.run_permitted ? 1U : 0U);
	return 0;
}

static int cmd_ready(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (manual_control_is_active()) {
		shell_error(sh, "result=%d reason=manual_mode_active", -EBUSY);
		return -EBUSY;
	}
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_READY));
}

static int cmd_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (manual_control_is_active()) {
		shell_error(sh, "result=%d reason=manual_mode_active", -EBUSY);
		return -EBUSY;
	}
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_RUNNING));
}

static int cmd_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_transition(CONTROLLER_STATE_IDLE));
}

static int cmd_clear_fault(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(sh, controller_state_clear_fault());
}

static int cmd_config_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct ejection_settings_state state;
	ejection_settings_get(&state);
	shell_print(sh, "ready=%u persistent=%u sequence=%lu error=%d",
		    state.ready ? 1U : 0U, state.persistent ? 1U : 0U,
		    (unsigned long)state.sequence, state.last_error);
	shell_print(sh, "can_bitrate=%lu pulse_ms=%u encoder_cps=%u move_ms=%u hold_ms=%u",
		    (unsigned long)state.data.can_bitrate,
		    state.data.default_pulse_ms,
		    state.data.encoder_counts_per_step,
		    state.data.conveyor_delay_ms,
		    state.data.ejection_hold_ms);
	return 0;
}

static int cmd_config_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	struct ejection_settings_state state;
	ejection_settings_get(&state);
	char *end;
	unsigned long value = strtoul(argv[2], &end, 0);

	if (*argv[2] == '\0' || *end != '\0' || argv[2][0] == '-' ||
	    value > 1000000UL) {
		shell_error(sh, "result=%d reason=invalid_value", -EINVAL);
		return -EINVAL;
	}

	if (strcmp(argv[1], "can_bitrate") == 0) {
		state.data.can_bitrate = (uint32_t)value;
	} else if (strcmp(argv[1], "pulse_ms") == 0) {
		if (value > 1000U) { return -ERANGE; }
		state.data.default_pulse_ms = (uint16_t)value;
	} else if (strcmp(argv[1], "encoder_cps") == 0) {
		if (value > 16U) { return -ERANGE; }
		state.data.encoder_counts_per_step = (uint8_t)value;
	} else if (strcmp(argv[1], "move_ms") == 0 ||
		   strcmp(argv[1], "conveyor_delay_ms") == 0) {
		if (value > 10000U) { return -ERANGE; }
		state.data.conveyor_delay_ms = (uint16_t)value;
	} else if (strcmp(argv[1], "hold_ms") == 0 ||
		   strcmp(argv[1], "ejection_hold_ms") == 0) {
		if (value > 1000U) { return -ERANGE; }
		state.data.ejection_hold_ms = (uint16_t)value;
	} else {
		shell_error(sh, "key=can_bitrate|pulse_ms|encoder_cps|move_ms|hold_ms");
		return -EINVAL;
	}

	int ret = ejection_settings_set(&state.data);
	if (ret != 0) {
		shell_error(sh, "result=%d reason=settings_rejected value=%lu", ret, value);
		return ret;
	}
	shell_print(sh, "result=0 persistent=0 action=run_ejection_config_save");
	return 0;
}

static int cmd_config_save(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_settings_save();
	phase3_ui_notify_save(ret);

	if (ret != 0) {
		shell_error(sh, "result=%d reason=eeprom_save_verify_failed", ret);
		return ret;
	}
	shell_print(sh, "result=0 persistent=1 verified=1");
	return 0;
}

static int cmd_config_defaults(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_settings_defaults();

	if (ret != 0) {
		shell_error(sh, "result=%d reason=defaults_rejected", ret);
		return ret;
	}
	shell_print(sh, "result=0 persistent=0 action=run_ejection_config_save");
	return 0;
}

static struct scheduler_event scheduler_snapshot[SCHEDULER_CAPACITY];

static const char *event_state_name(enum scheduler_event_state state)
{
	switch (state) {
	case EVENT_PENDING: return "PENDING";
	case EVENT_ACTIVE: return "ACTIVE";
	case EVENT_COMPLETED: return "DONE";
	case EVENT_CANCELLED: return "CANCELLED";
	case EVENT_MISSED: return "MISSED";
	default: return "UNKNOWN";
	}
}

static int cmd_queue_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct ejection_scheduler_status status;
	struct ejection_settings_state settings;
	ejection_scheduler_get_status(&status);
	ejection_settings_get(&settings);
	shell_print(sh, "pending=%u active=%u used=%u capacity=%u peak=%u mask=0x%03x",
		    status.pending, status.active, status.pending + status.active,
		    SCHEDULER_CAPACITY, status.peak, status.mask);
	shell_print(sh, "move_ms=%u hold_ms=%u next_in_ms=%lld",
		    settings.data.conveyor_delay_ms, settings.data.ejection_hold_ms,
		    status.next_due == INT64_MAX ? -1LL :
		    status.next_due - k_uptime_get());
	shell_print(sh, "accepted=%u completed=%u cancelled=%u missed=%u rejected=%u overflow=%u io_errors=%u",
		    status.accepted, status.completed, status.cancelled, status.missed,
		    status.rejected, status.overflow, status.io_errors);
	shell_print(sh, "max_on_late_ms=%lld max_off_late_ms=%lld",
		    status.max_on_late_ms, status.max_off_late_ms);
	return 0;
}

static void print_history_event(const struct shell *sh,
				const struct scheduler_event *event,
				int64_t previous_rx_at)
{
	int64_t planned_delay = event->on_due - event->rx_at;
	int64_t planned_hold = event->off_due - event->on_due;
	int64_t rx_gap = previous_rx_at < 0 ? -1 : event->rx_at - previous_rx_at;

	if (event->state == EVENT_COMPLETED) {
		shell_print(sh, "seq=%u mask=0x%03x state=%s rx_gap_ms=%lld delay_ms=%lld/%lld hold_ms=%lld/%lld on_late_ms=%+lld off_late_ms=%+lld",
			    event->sequence, event->mask, event_state_name(event->state),
			    rx_gap, event->on_at - event->rx_at, planned_delay,
			    event->off_at - event->on_at, planned_hold,
			    event->on_at - event->on_due,
			    event->off_at - event->off_due);
	} else if (event->state == EVENT_CANCELLED) {
		shell_print(sh, "seq=%u mask=0x%03x state=%s rx_gap_ms=%lld cancel_after_ms=%lld phase=%s",
			    event->sequence, event->mask, event_state_name(event->state),
			    rx_gap, event->cancelled_at - event->rx_at,
			    event->on_at < 0 ? "BEFORE_ON" : "AFTER_ON");
	} else if (event->state == EVENT_MISSED) {
		shell_print(sh, "seq=%u mask=0x%03x state=%s rx_gap_ms=%lld planned_delay_ms=%lld planned_hold_ms=%lld",
			    event->sequence, event->mask, event_state_name(event->state),
			    rx_gap, planned_delay, planned_hold);
	} else {
		shell_print(sh, "seq=%u mask=0x%03x state=%s rx_gap_ms=%lld",
			    event->sequence, event->mask, event_state_name(event->state),
			    rx_gap);
	}
}

static int print_queue_events(const struct shell *sh, bool history)
{
	size_t count = ejection_scheduler_snapshot(scheduler_snapshot,
						 ARRAY_SIZE(scheduler_snapshot), history);

	for (size_t i = 0; i < count; i++) {
		struct scheduler_event *event = &scheduler_snapshot[i];
		if (history) {
			print_history_event(sh, event,
					    i == 0 ? -1 : scheduler_snapshot[i - 1].rx_at);
		} else {
			shell_print(sh, "seq=%u mask=0x%03x state=%s rx_at_ms=%lld on_due_ms=%lld off_due_ms=%lld on_at_ms=%lld off_at_ms=%lld cancel_at_ms=%lld",
				    event->sequence, event->mask,
				    event_state_name(event->state), event->rx_at,
				    event->on_due, event->off_due, event->on_at,
				    event->off_at, event->cancelled_at);
		}
	}
	shell_print(sh, "count=%u", (unsigned int)count);
	return 0;
}

static int cmd_queue_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_queue_events(sh, false);
}

static int cmd_queue_history(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_queue_events(sh, true);
}

static int cmd_queue_cancel(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = ejection_scheduler_cancel_all();

	if (ret != 0) {
		shell_error(sh, "result=%d reason=cancel_or_all_off_failed", ret);
		return ret;
	}
	shell_print(sh, "result=0 pending=0 active=0 mask=0x000");
	return 0;
}

static int cmd_io_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	uint16_t outputs = 0;
	uint16_t inputs = 0;

	for (size_t i = 0; i < EJECTION_OUTPUT_COUNT; i++) {
		if (ejection_output_get(i)) { outputs |= BIT(i); }
	}
	for (size_t i = 0; i < EJECTION_INPUT_COUNT; i++) {
		if (ejection_input_get(i)) { inputs |= BIT(i); }
	}
	shell_print(sh, "outputs=0x%03x inputs=0x%04x", outputs, inputs);
	return 0;
}

static int cmd_manual_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct manual_control_status status;
	struct ejection_settings_state settings;
	manual_control_get_status(&status);
	ejection_settings_get(&settings);
	shell_print(sh, "initialized=%u mode=%s selector=%u armed=%u",
		    status.initialized ? 1U : 0U,
		    status.active ? "MANUAL" : "AUTO",
		    status.active ? 1U : 0U, status.armed ? 1U : 0U);
	shell_print(sh, "buttons=0x%03x outputs=0x%03x hold_ms=%u",
		    status.button_mask, ejection_output_mask_get(),
		    settings.data.ejection_hold_ms);
	shell_print(sh, "triggers=%lu rejected=%lu last_channel=%d last_error=%d",
		    (unsigned long)status.trigger_count,
		    (unsigned long)status.rejected_count,
		    status.last_channel < 0 ? -1 : status.last_channel + 1,
		    status.last_error);
	return 0;
}

static int cmd_can_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct can_protocol_status status;
	can_protocol_get_status(&status);
	int64_t age = status.last_heartbeat_ms == 0 ? -1 :
		      k_uptime_get() - status.last_heartbeat_ms;

	shell_print(sh, "initialized=%u link=%u heartbeat_required=%u status_tx=%u heartbeat_sequence=%u heartbeat_age_ms=%lld",
		    status.initialized ? 1U : 0U, status.link_active ? 1U : 0U,
		    status.heartbeat_required ? 1U : 0U,
		    status.status_publishing ? 1U : 0U,
		    status.last_heartbeat_sequence, age);
	shell_print(sh, "rx=%lu commands=%lu heartbeat=%lu echo=%lu status=%lu reject=%lu duplicate=%lu dropped=%lu tx_errors=%lu",
		    (unsigned long)status.rx_count,
		    (unsigned long)status.command_count,
		    (unsigned long)status.heartbeat_count,
		    (unsigned long)status.echo_count,
		    (unsigned long)status.status_count,
		    (unsigned long)status.reject_count,
		    (unsigned long)status.duplicate_count,
		    (unsigned long)status.dropped_count,
		    (unsigned long)status.tx_error_count);
	return 0;
}

static int cmd_can_heartbeat(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	bool required;

	if (strcmp(argv[1], "on") == 0) {
		required = true;
	} else if (strcmp(argv[1], "off") == 0) {
		required = false;
	} else {
		shell_error(sh, "usage: ejection can heartbeat <on|off>");
		return -EINVAL;
	}
	can_protocol_set_heartbeat_required(required);
	shell_print(sh, "result=0 heartbeat_required=%u", required ? 1U : 0U);
	return 0;
}

#if defined(CONFIG_EJECTION_SHELL_DIAGNOSTICS)
static int cmd_diag_devices(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct phase3_status status;
	phase3_status_get(&status);
	shell_print(sh, "can_ready=%u oled_ready=%u encoder_ready=%u encoder_position=%ld key=%u",
		    status.can_ready ? 1U : 0U, status.oled_ready ? 1U : 0U,
		    status.encoder_ready ? 1U : 0U, (long)status.encoder_position,
		    status.encoder_pressed ? 1U : 0U);
	shell_print(sh, "can_rx=%lu last_id=0x%03x last_dlc=%u",
		    (unsigned long)status.can_rx_count, status.last_rx.id,
		    status.last_rx.dlc);
	return 0;
}

static int cmd_diag_can_send(const struct shell *sh, size_t argc, char **argv)
{
	char *end;
	unsigned long id = strtoul(argv[1], &end, 0);

	if (*argv[1] == '\0' || *end != '\0' || id > CAN_STD_ID_MASK) {
		shell_error(sh, "standard CAN id must be 0..0x7ff");
		return -EINVAL;
	}
	uint8_t data[CAN_MAX_DLEN] = {0};
	size_t length = argc - 2U;
	for (size_t i = 0; i < length; i++) {
		unsigned long value = strtoul(argv[i + 2U], &end, 16);
		if (*argv[i + 2U] == '\0' || *end != '\0' || value > 0xff) {
			shell_error(sh, "data bytes must be hexadecimal 00..ff");
			return -EINVAL;
		}
		data[i] = (uint8_t)value;
	}
	int ret = phase3_can_send((uint32_t)id, data, length);
	if (ret != 0) {
		shell_error(sh, "result=%d reason=can_send_failed", ret);
		return ret;
	}
	shell_print(sh, "result=0 id=0x%03lx dlc=%u", id, (unsigned int)length);
	return 0;
}

static int cmd_diag_oled(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	int ret = phase3_oled_show("12CH EJECTION", argv[1], "OLED TEST", "DIAGNOSTIC");

	if (ret != 0) {
		shell_error(sh, "result=%d reason=oled_write_failed", ret);
		return ret;
	}
	shell_print(sh, "result=0 oled_updated=1");
	return 0;
}

static int cmd_diag_status_send(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int ret = can_protocol_send_status();

	if (ret != 0) {
		shell_error(sh, "result=%d reason=status_send_failed", ret);
		return ret;
	}
	shell_print(sh, "result=0 id=0x%03x", CAN_PROTOCOL_STATUS_ID);
	return 0;
}

#if defined(CONFIG_EJECTION_SHELL_MANUAL_OUTPUT)
static int require_idle(const struct shell *sh)
{
	struct controller_state_snapshot snapshot;
	controller_state_get(&snapshot);
	if (snapshot.state != CONTROLLER_STATE_IDLE) {
		shell_error(sh, "result=%d reason=manual_output_requires_idle state=%s",
			    -EACCES, controller_state_name(snapshot.state));
		return -EACCES;
	}
	return 0;
}

static int cmd_diag_output_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	int ret = require_idle(sh);
	if (ret != 0) { return ret; }
	bool active;
	if (strcmp(argv[2], "on") == 0) {
		active = true;
	} else if (strcmp(argv[2], "off") == 0) {
		active = false;
	} else {
		shell_error(sh, "state must be on or off");
		return -EINVAL;
	}
	size_t channel;
	ret = parse_channel(sh, argv[1], EJECTION_OUTPUT_COUNT, &channel);
	if (ret != 0) { return ret; }
	ret = ejection_scheduler_manual(channel, active, 0);
	if (ret != 0) {
		shell_error(sh, "result=%d reason=manual_output_rejected", ret);
		return ret;
	}
	shell_print(sh, "result=0 channel=%u active=%u",
		    (unsigned int)channel + 1U, active ? 1U : 0U);
	return 0;
}

static int cmd_diag_output_pulse(const struct shell *sh, size_t argc, char **argv)
{
	int ret = require_idle(sh);
	if (ret != 0) { return ret; }
	size_t channel;
	ret = parse_channel(sh, argv[1], EJECTION_OUTPUT_COUNT, &channel);
	if (ret != 0) { return ret; }
	struct ejection_settings_state settings;
	ejection_settings_get(&settings);
	unsigned long duration = settings.data.default_pulse_ms;
	char *end = NULL;
	if (argc >= 3U) { duration = strtoul(argv[2], &end, 10); }
	if ((argc >= 3U && (*argv[2] == '\0' || *end != '\0')) ||
	    duration < EJECTION_PULSE_MIN_MS || duration > EJECTION_PULSE_MAX_MS) {
		shell_error(sh, "duration must be %u..%u ms",
			    EJECTION_PULSE_MIN_MS, EJECTION_PULSE_MAX_MS);
		return -ERANGE;
	}
	ret = ejection_scheduler_manual(channel, true, (uint32_t)duration);
	if (ret != 0) {
		shell_error(sh, "result=%d reason=manual_pulse_rejected", ret);
		return ret;
	}
	shell_print(sh, "result=0 channel=%u pulse_ms=%lu",
		    (unsigned int)channel + 1U, duration);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_diag_output_cmds,
	SHELL_CMD_ARG(set, NULL, "Set output in IDLE: <1-12> <on|off>",
		      cmd_diag_output_set, 3, 0),
	SHELL_CMD_ARG(pulse, NULL, "Pulse output in IDLE: <1-12> [5-1000 ms]",
		      cmd_diag_output_pulse, 2, 1),
	SHELL_SUBCMD_SET_END
);
#endif

#if defined(CONFIG_EJECTION_SHELL_FAULT_INJECTION)
static int cmd_diag_inject_fault(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return print_transition_result(
		sh, controller_state_enter_fault(CONTROLLER_FAULT_EMERGENCY_STOP));
}
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_diag_cmds,
	SHELL_CMD_ARG(devices, NULL, "Show CAN/OLED/encoder diagnostics",
		      cmd_diag_devices, 1, 0),
	SHELL_CMD_ARG(can_send, NULL, "Send raw standard CAN: <id> [00..ff]",
		      cmd_diag_can_send, 2, 8),
	SHELL_CMD_ARG(oled, NULL, "Overwrite OLED with diagnostic text: <text>",
		      cmd_diag_oled, 2, 0),
	SHELL_CMD_ARG(status_send, NULL, "Send one 0x201 status frame",
		      cmd_diag_status_send, 1, 0),
#if defined(CONFIG_EJECTION_SHELL_MANUAL_OUTPUT)
	SHELL_CMD(output, &ejection_diag_output_cmds, "Manual output diagnostics", NULL),
#endif
#if defined(CONFIG_EJECTION_SHELL_FAULT_INJECTION)
	SHELL_CMD_ARG(inject_fault, NULL, "Inject emergency-stop fault",
		      cmd_diag_inject_fault, 1, 0),
#endif
	SHELL_SUBCMD_SET_END
);
#endif

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_config_cmds,
	SHELL_CMD_ARG(show, NULL, "Show persistent configuration", cmd_config_show, 1, 0),
	SHELL_CMD_ARG(set, NULL, "Set RAM value: <key> <value>", cmd_config_set, 3, 0),
	SHELL_CMD_ARG(save, NULL, "Save and verify EEPROM", cmd_config_save, 1, 0),
	SHELL_CMD_ARG(defaults, NULL, "Load defaults into RAM", cmd_config_defaults, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_queue_cmds,
	SHELL_CMD_ARG(status, NULL, "Show queue, timing and counters", cmd_queue_status, 1, 0),
	SHELL_CMD_ARG(list, NULL, "Show pending and active events", cmd_queue_list, 1, 0),
	SHELL_CMD_ARG(history, NULL, "Show last 32 retired events", cmd_queue_history, 1, 0),
	SHELL_CMD_ARG(cancel, NULL, "Cancel all reservations and turn outputs off",
		      cmd_queue_cancel, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_io_cmds,
	SHELL_CMD_ARG(status, NULL, "Show logical input and output masks", cmd_io_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_manual_cmds,
	SHELL_CMD_ARG(status, NULL, "Show selector, arming and trigger state",
		      cmd_manual_status, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_can_cmds,
	SHELL_CMD_ARG(status, NULL, "Show CAN protocol counters", cmd_can_status, 1, 0),
	SHELL_CMD_ARG(heartbeat, NULL, "Set optional heartbeat gate: <on|off>",
		      cmd_can_heartbeat, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(ejection_cmds,
	SHELL_CMD_ARG(status, NULL, "Show controller state and prerequisites",
		      cmd_system_status, 1, 0),
	SHELL_CMD_ARG(ready, NULL, "Transition IDLE to READY", cmd_ready, 1, 0),
	SHELL_CMD_ARG(start, NULL, "Transition READY to RUNNING", cmd_start, 1, 0),
	SHELL_CMD_ARG(stop, NULL, "Transition READY/RUNNING to IDLE", cmd_stop, 1, 0),
	SHELL_CMD_ARG(clear_fault, NULL, "Clear FAULT and return to IDLE",
		      cmd_clear_fault, 1, 0),
	SHELL_CMD(config, &ejection_config_cmds, "Persistent configuration", NULL),
	SHELL_CMD(queue, &ejection_queue_cmds, "Queued output scheduler", NULL),
	SHELL_CMD(io, &ejection_io_cmds, "Logical input/output state", NULL),
	SHELL_CMD(manual, &ejection_manual_cmds, "Physical-button manual operation", NULL),
	SHELL_CMD(can, &ejection_can_cmds, "CAN protocol", NULL),
#if defined(CONFIG_EJECTION_SHELL_DIAGNOSTICS)
	SHELL_CMD(diag, &ejection_diag_cmds, "Development diagnostics", NULL),
#endif
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ejection, &ejection_cmds,
		   "12-channel ejection controller", NULL);
