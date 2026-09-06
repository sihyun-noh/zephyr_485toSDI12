/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eeprom_store.h"

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define EEPROM_NODE DT_NODELABEL(eeprom0)

static struct eeprom_store_state store_state;

#if DT_NODE_HAS_STATUS(EEPROM_NODE, okay)
static const struct device *const eeprom_dev = DEVICE_DT_GET(EEPROM_NODE);
#endif

static int eeprom_store_require_ready(void)
{
	return eeprom_store_init();
}

#if IS_ENABLED(CONFIG_DAJILOG_EEPROM_BRINGUP_TEST)
static void print_bytes(const char *prefix, const uint8_t *data, size_t len)
{
	printk("%s", prefix);
	for (size_t i = 0; i < len; i++) {
		printk(" %02x", data[i]);
	}
	printk("\r\n");
}
#endif

int eeprom_store_init(void)
{
#if !DT_NODE_HAS_STATUS(EEPROM_NODE, okay)
	store_state.ready = false;
	store_state.last_error = -ENODEV;
	printk("EEPROM: eeprom0 devicetree node is not okay\r\n");
	return store_state.last_error;
#else
	if (!device_is_ready(eeprom_dev)) {
		store_state.ready = false;
		store_state.last_error = -ENODEV;
		printk("EEPROM: %s is not ready\r\n", eeprom_dev->name);
		return store_state.last_error;
	}

	store_state.ready = true;
	store_state.last_error = 0;
	return 0;
#endif
}

int eeprom_store_read(off_t offset, void *buffer, size_t length)
{
	int ret = eeprom_store_require_ready();

	if (ret == 0) {
#if DT_NODE_HAS_STATUS(EEPROM_NODE, okay)
		ret = eeprom_read(eeprom_dev, offset, buffer, length);
#endif
	}

	store_state.last_error = ret;
	return ret;
}

int eeprom_store_write(off_t offset, const void *buffer, size_t length)
{
	int ret = eeprom_store_require_ready();

	if (ret == 0) {
#if DT_NODE_HAS_STATUS(EEPROM_NODE, okay)
		ret = eeprom_write(eeprom_dev, offset, buffer, length);
#endif
	}

	store_state.last_error = ret;
	return ret;
}

int eeprom_store_state_get(struct eeprom_store_state *state)
{
	if (state == NULL) {
		return -EINVAL;
	}

	*state = store_state;
	return 0;
}

int eeprom_store_bringup_test_once(void)
{
#if IS_ENABLED(CONFIG_DAJILOG_EEPROM_BRINGUP_TEST)
	static const uint8_t test_pattern[] = {
		'D', 'A', 'J', 'I', 'E', 'E', 'P', 0x01,
	};
	uint8_t readback[sizeof(test_pattern)] = {0};
	off_t offset = CONFIG_DAJILOG_EEPROM_TEST_OFFSET;
	int ret;

	ret = eeprom_store_require_ready();
	if (ret != 0) {
		return ret;
	}

#if DT_NODE_HAS_STATUS(EEPROM_NODE, okay)
	printk("EEPROM bring-up: dev=%s offset=0x%04lx len=%u\r\n",
	       eeprom_dev->name, (unsigned long)offset,
	       (unsigned int)sizeof(test_pattern));
#endif

	ret = eeprom_store_write(offset, test_pattern, sizeof(test_pattern));
	if (ret != 0) {
		printk("EEPROM bring-up: write failed ret=%d\r\n", ret);
		return ret;
	}

	k_sleep(K_MSEC(10));

	ret = eeprom_store_read(offset, readback, sizeof(readback));
	if (ret != 0) {
		printk("EEPROM bring-up: read failed ret=%d\r\n", ret);
		return ret;
	}

	print_bytes("EEPROM bring-up: read", readback, sizeof(readback));

	if (memcmp(test_pattern, readback, sizeof(test_pattern)) != 0) {
		store_state.last_error = -EIO;
		printk("EEPROM bring-up: verify failed\r\n");
		return store_state.last_error;
	}

	printk("EEPROM bring-up: verify ok\r\n");
	return 0;
#else
	return 0;
#endif
}
