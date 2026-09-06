/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_SCALE_TYPES_H_
#define DAJILOG_SCALE_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

enum scale_calibration_state {
	SCALE_CAL_IDLE = 0,
	SCALE_CAL_ZERO_WAIT_STABLE,
	SCALE_CAL_ZERO_CAPTURED,
	SCALE_CAL_SPAN_WAIT_STABLE,
	SCALE_CAL_REVIEW_READY,
	SCALE_CAL_SAVE_PENDING,
	SCALE_CAL_FAILED,
};

enum scale_command {
	SCALE_CMD_GET_STATUS = 0,
	SCALE_CMD_CAL_START,
	SCALE_CMD_CAL_SET_REFERENCE_MG,
	SCALE_CMD_CAL_ZERO_CAPTURE,
	SCALE_CMD_CAL_SPAN_CAPTURE,
	SCALE_CMD_CAL_SAVE,
	SCALE_CMD_CAL_CANCEL,
	SCALE_CMD_TARE_SET,
	SCALE_CMD_TARE_CLEAR,
};

struct scale_calibration {
	bool valid;
	int32_t zero_raw;
	int32_t span_raw;
	int32_t reference_weight_mg;
	uint32_t sequence;
};

struct scale_state {
	bool ready;
	bool value_valid;
	bool stable;
	bool tare_active;
	int last_error;
	int32_t latest_raw;
	int32_t filtered_raw;
	int32_t p2p_raw;
	int32_t gross_weight_mg;
	int32_t tare_weight_mg;
	int32_t net_weight_mg;
	uint32_t sample_count;
	enum scale_calibration_state calibration_state;
	struct scale_calibration calibration;
};

struct scale_command_msg {
	enum scale_command command;
	uint8_t sequence;
	int32_t value_mg;
};

#endif /* DAJILOG_SCALE_TYPES_H_ */
