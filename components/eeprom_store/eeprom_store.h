/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DAJILOG_EEPROM_STORE_H_
#define DAJILOG_EEPROM_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct eeprom_store_state {
	bool ready;
	int last_error;
};

int eeprom_store_init(void);
int eeprom_store_read(off_t offset, void *buffer, size_t length);
int eeprom_store_write(off_t offset, const void *buffer, size_t length);
int eeprom_store_state_get(struct eeprom_store_state *state);

/* Hardware bring-up helper. Enabled only by CONFIG_DAJILOG_EEPROM_BRINGUP_TEST. */
int eeprom_store_bringup_test_once(void);

#endif /* DAJILOG_EEPROM_STORE_H_ */
