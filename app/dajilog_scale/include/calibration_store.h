/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_CALIBRATION_STORE_H_
#define DAJILOG_CALIBRATION_STORE_H_

#include "scale_types.h"

int calibration_store_load(struct scale_calibration *calibration);
int calibration_store_save(struct scale_calibration *calibration);

#endif /* DAJILOG_CALIBRATION_STORE_H_ */
