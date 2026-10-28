/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include "ejection_io.h"
#include "ejection_settings.h"
#include "phase3_devices.h"
#include "controller_state.h"
#include "can_protocol.h"
#include "ejection_scheduler.h"

#define USB_DTR_WAIT_MS 3000

static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct device *const can_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct device *const eeprom_dev = DEVICE_DT_GET(DT_NODELABEL(eeprom0));

static void start_usb_console(void)
{
	uint32_t dtr = 0;
	int ret = usb_enable(NULL);

	if (ret != 0 && ret != -EALREADY) {
		return;
	}

	for (int elapsed = 0; elapsed < USB_DTR_WAIT_MS; elapsed += 100) {
		(void)uart_line_ctrl_get(console_dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr != 0U) {
			break;
		}
		k_sleep(K_MSEC(100));
	}
}

int main(void)
{
	controller_state_init();
	int ret = ejection_io_init();

	start_usb_console();
	int settings_ret = ejection_settings_init();
	ejection_scheduler_init();
	int phase3_ret = phase3_devices_init();
	struct phase3_status phase3_status;
	phase3_status_get(&phase3_status);
	struct controller_prerequisites prerequisites = {
		.io_ready = ret == 0,
		.settings_ready = settings_ret == 0,
		.can_ready = phase3_status.can_ready,
		.encoder_ready = phase3_status.encoder_ready,
		.oled_ready = phase3_status.oled_ready,
	};
	controller_state_set_prerequisites(&prerequisites);
	(void)controller_state_transition(CONTROLLER_STATE_SELF_TEST);
	if (ret != 0) {
		(void)controller_state_enter_fault(CONTROLLER_FAULT_IO_INIT);
	} else if (settings_ret != 0) {
		(void)controller_state_enter_fault(CONTROLLER_FAULT_EEPROM_INIT);
	} else if (!phase3_status.can_ready) {
		(void)controller_state_enter_fault(CONTROLLER_FAULT_CAN_INIT);
	} else if (!phase3_status.encoder_ready) {
		(void)controller_state_enter_fault(CONTROLLER_FAULT_IO_INIT);
	} else {
		(void)controller_state_transition(CONTROLLER_STATE_IDLE);
	}
	int protocol_ret = phase3_status.can_ready ? can_protocol_init() : -ENODEV;
	if (protocol_ret != 0) {
		prerequisites.can_ready = false;
		controller_state_set_prerequisites(&prerequisites);
		(void)controller_state_enter_fault(CONTROLLER_FAULT_CAN_INIT);
	}
	phase3_ui_refresh();
	struct controller_state_snapshot controller;
	controller_state_get(&controller);
	printk("12ch-ejection-controller bring-up\r\n");
	printk("GPIO: %s, CAN: %s, EEPROM: %s\r\n",
	       ret == 0 ? "ready" : "error",
	       device_is_ready(can_dev) ? "ready" : "error",
	       device_is_ready(eeprom_dev) ? "ready" : "error");
	printk("Phase 3 (CAN/OLED/encoder): %s (%d)\r\n",
	       phase3_ret == 0 ? "ready" : "partial/error", phase3_ret);
	printk("EEPROM settings: %s (%d)\r\n",
	       settings_ret == 0 ? "ready" : "error", settings_ret);
	printk("Controller: %s, fault: %s\r\n",
	       controller_state_name(controller.state),
	       controller_fault_name(controller.fault));
	printk("CAN protocol: %s (%d)\r\n",
	       protocol_ret == 0 ? "ready" : "error", protocol_ret);
	printk("Type 'ejection status'. Outputs remain off until commanded.\r\n");
	printk("Type 'phase3 status' for CAN/OLED/encoder state.\r\n");
	printk("Type 'settings status' for persistent configuration.\r\n");
	printk("Type 'controller status' for the system state machine.\r\n");
	printk("Type 'canproto status' for protocol and heartbeat counters.\r\n");
	printk("Phase 4: 0x101 queues events; use 'scheduler status/history'.\r\n");

	return 0;
}
