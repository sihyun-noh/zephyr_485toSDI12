/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_DEVICE_PROVISIONING_H_
#define DAJILOG_DEVICE_PROVISIONING_H_

#include <dajilog_device_info/dajilog_device_info.h>

typedef void (*dajilog_device_runtime_get_t)(
	void *user_data, struct dajilog_device_runtime *runtime);

struct dajilog_device_provisioning_config {
	struct dajilog_device_info *device_info;
	dajilog_device_runtime_get_t runtime_get;
	void *user_data;
};

int dajilog_device_provisioning_init(
	const struct dajilog_device_provisioning_config *config);

#endif /* DAJILOG_DEVICE_PROVISIONING_H_ */
