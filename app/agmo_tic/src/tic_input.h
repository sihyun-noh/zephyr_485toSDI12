/* SPDX-License-Identifier: Apache-2.0 */

#ifndef AGMO_TIC_INPUT_H_
#define AGMO_TIC_INPUT_H_

#include "tic_types.h"

int tic_input_init(void);
int tic_input_start(void);
void tic_input_get_snapshot(struct tic_input_snapshot *out);

#endif /* AGMO_TIC_INPUT_H_ */
