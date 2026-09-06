/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include "tic_can.h"
#include "tic_fsm.h"
#include "tic_input.h"
#include "tic_output.h"

#define USB_DTR_WAIT_MS 3000

static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static void start_usb_console(void)
{
	uint32_t dtr = 0;
	int ret;

	ret = usb_enable(NULL);
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
	int ret;

	ret = tic_output_init();
	if (ret != 0) {
		return 0;
	}
	ret = tic_fsm_start();
	if (ret != 0) {
		(void)tic_output_all_off();
		return 0;
	}
	ret = tic_input_init();
	if (ret == 0) {
		ret = tic_input_start();
	}
	if (ret != 0) {
		const struct tic_event fault = { .type = TIC_EVENT_INTERNAL_FAULT };
		(void)tic_fsm_post(&fault, K_MSEC(20));
	}

	ret = tic_can_init();
	if (ret == 0) {
		ret = tic_can_start();
	}
	if (ret != 0) {
		const struct tic_event fault = { .type = TIC_EVENT_INTERNAL_FAULT };
		(void)tic_fsm_post(&fault, K_MSEC(20));
	}

	start_usb_console();
	printk("AGMO TIC ready. Type 'help'.\r\n");
	return 0;
}
