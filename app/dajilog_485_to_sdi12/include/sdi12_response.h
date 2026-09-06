/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_485_TO_SDI12_SDI12_RESPONSE_H_
#define DAJILOG_485_TO_SDI12_SDI12_RESPONSE_H_

#include <sdi12_slave/sdi12_slave.h>

int sdi12_bridge_response_cb(struct sdi12_slave *sdi12, const char *cmd,
			     char *response, size_t response_len,
			     void *user_data);

#endif /* DAJILOG_485_TO_SDI12_SDI12_RESPONSE_H_ */
