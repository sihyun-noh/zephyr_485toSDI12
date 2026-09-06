/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include <ads1232/ads1232.h>
#include <can_bus/can_bus.h>
#include <display_oled/display_oled.h>
#include <rs485_modbus/rs485_modbus.h>
#include <sbus/sbus.h>
#include <sdi12_slave/sdi12_slave.h>

#define USER_NODE DT_PATH(zephyr_user)
#define I2C1_NODE DT_NODELABEL(i2c1)
#define RS485_UART_NODE DT_NODELABEL(usart1)
#define SBUS_UART_NODE DT_NODELABEL(usart2)

#define ADS1232_READ_PERIOD_MS CONFIG_DAJILOG_ADS1232_READ_PERIOD_MS
#define USB_ENABLE_RETRY_COUNT 3
#define USB_START_DELAY_MS 2000
#define USB_DTR_WAIT_MS 3000

static const struct device *const console_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static struct ads1232 loadcell_adc = {
	.config = {
		.sclk = GPIO_DT_SPEC_GET(USER_NODE, ads1232_sclk_gpios),
		.dout = GPIO_DT_SPEC_GET(USER_NODE, ads1232_dout_gpios),
		.pwdn = GPIO_DT_SPEC_GET(USER_NODE, ads1232_pwdn_gpios),
		.drdy_timeout_ms = CONFIG_DAJILOG_ADS1232_DRDY_TIMEOUT_MS,
		.startup_delay_ms = CONFIG_DAJILOG_ADS1232_STARTUP_DELAY_MS,
		.sclk_half_period_us = CONFIG_DAJILOG_ADS1232_SCLK_HALF_PERIOD_US,
	},
};

static struct oled_display oled = {
	.config = {
		.display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
		.i2c = DEVICE_DT_GET(I2C1_NODE),
		.i2c_speed_hz = CONFIG_DAJILOG_OLED_I2C_SPEED_HZ,
		.scan_i2c_on_init = IS_ENABLED(CONFIG_DAJILOG_OLED_SCAN_I2C_ON_INIT),
	},
};

static struct rs485_modbus rs485_sensor = {
	.config = {
		.uart = DEVICE_DT_GET(RS485_UART_NODE),
		.de = GPIO_DT_SPEC_GET(USER_NODE, rs485_en_gpios),
		.default_slave_addr = CONFIG_DAJILOG_RS485_MODBUS_SENSOR_ADDR,
		.default_start_reg = CONFIG_DAJILOG_RS485_MODBUS_START_REG,
		.default_reg_count = CONFIG_DAJILOG_RS485_MODBUS_REG_COUNT,
		.response_timeout_ms = CONFIG_DAJILOG_RS485_MODBUS_RESPONSE_TIMEOUT_MS,
		.tx_turnaround_us = CONFIG_DAJILOG_RS485_MODBUS_TX_TURNAROUND_US,
	},
};

static struct can_bus can1 = {
	.config = {
		.dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)),
		.default_tx_id = CONFIG_DAJILOG_CAN_TX_TEST_ID,
	},
};

static struct sbus sbus_rx = {
	.config = {
		.uart = DEVICE_DT_GET(SBUS_UART_NODE),
		.baudrate = 100000,
		.inverted_by_hardware = true,
	},
};

static struct sdi12_slave sdi12 = {
	.config = {
		.tx = GPIO_DT_SPEC_GET(USER_NODE, sdi12_tx_gpios),
		.rx = GPIO_DT_SPEC_GET(USER_NODE, sdi12_rx_gpios),
		.oe = GPIO_DT_SPEC_GET(USER_NODE, sdi12_oe_gpios),
		.rx_inverted = IS_ENABLED(CONFIG_DAJILOG_SDI12_RX_INVERTED),
	},
};

static void start_usb_cdc_console(void)
{
	uint32_t dtr = 0;
	int ret = -EIO;

	k_sleep(K_MSEC(USB_START_DELAY_MS));

	if (!IS_ENABLED(CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT)) {
		for (int i = 0; i < USB_ENABLE_RETRY_COUNT; i++) {
			ret = usb_enable(NULL);
			if (ret == 0) {
				break;
			}
			k_sleep(K_MSEC(200));
		}

		if (ret != 0) {
			return;
		}
	}

	for (int elapsed_ms = 0; elapsed_ms < USB_DTR_WAIT_MS; elapsed_ms += 100) {
		(void)uart_line_ctrl_get(console_dev, UART_LINE_CTRL_DTR, &dtr);
		if (dtr != 0) {
			break;
		}
		k_sleep(K_MSEC(100));
	}
}

static void oled_show_ads1232_values(void)
{
	struct ads1232_state ads_state;
	struct oled_ads1232_view view = {0};

	ads1232_state_get(&loadcell_adc, &ads_state);

	view.value_valid = ads_state.value_valid;
	view.latest_raw = ads_state.latest_raw;
	view.sample_count = ads_state.sample_count;
	if (ads_state.value_valid && ads_state.sample_count > 0) {
		view.avg_raw = (int32_t)(ads_state.sum_raw / ads_state.sample_count);
		view.p2p_raw = ads_state.max_raw - ads_state.min_raw;
	}

	int ret = oled_display_show_ads1232(&oled, &view);

	if (ret != 0) {
		printk("ADS1232 OLED update failed ret=%d\r\n", ret);
	}
}

static void init_component_threads(void)
{
	int ret;

	ret = rs485_modbus_init(&rs485_sensor);
	if (ret == 0) {
		ret = rs485_modbus_start(&rs485_sensor);
	}
	printk("RS-485 Modbus component start ret=%d\r\n", ret);

	ret = can_bus_init(&can1);
	if (ret == 0) {
		ret = can_bus_start(&can1);
	}
	printk("CAN component start ret=%d\r\n", ret);

	ret = sbus_init(&sbus_rx);
	if (ret == 0) {
		ret = sbus_start(&sbus_rx);
	}
	printk("SBUS component start ret=%d\r\n", ret);

	ret = sdi12_slave_init(&sdi12);
	if (ret == 0) {
		ret = sdi12_slave_start(&sdi12);
	}
	printk("SDI-12 component start ret=%d\r\n", ret);
}

static void log_component_states(void)
{
	struct rs485_modbus_state rs485_state;
	struct can_bus_state can_state;
	struct sbus_state sbus_state;
	struct sdi12_slave_state sdi12_state;

	rs485_modbus_state_get(&rs485_sensor, &rs485_state);
	can_bus_state_get(&can1, &can_state);
	sbus_state_get(&sbus_rx, &sbus_state);
	sdi12_slave_state_get(&sdi12, &sdi12_state);

	if (rs485_state.value_valid) {
		printk("RS485 req=%lu rsp=%lu reg0=%u reg1=%u err=%d\r\n",
		       (unsigned long)rs485_state.request_count,
		       (unsigned long)rs485_state.response_count,
		       rs485_state.regs[0], rs485_state.regs[1],
		       rs485_state.last_error);
	}

	printk("CAN state=%s rx=%lu tx=%lu txerr=%lu filter=%d\r\n",
	       can_bus_state_name(can_state.bus_state),
	       (unsigned long)can_state.rx_count,
	       (unsigned long)can_state.tx_count,
	       (unsigned long)can_state.tx_error_count, can_state.rx_filter_id);

	if (sbus_state.frame_valid) {
		printk("SBUS frames=%lu ch1=%u ch2=%u ch3=%u ch4=%u flags=0x%02x\r\n",
		       (unsigned long)sbus_state.frame_count, sbus_state.channels[0],
		       sbus_state.channels[1], sbus_state.channels[2],
		       sbus_state.channels[3], sbus_state.flags);
	}

	printk("SDI12 rx=%d irq=%lu break=%lu timeout=%lu rxerr=%lu cmd=%lu last=%s\r\n",
	       sdi12_state.rx_logical, (unsigned long)sdi12_state.irq_count,
	       (unsigned long)sdi12_state.break_mark_count,
	       (unsigned long)sdi12_state.timeout_count,
	       (unsigned long)sdi12_state.rx_error_count,
	       (unsigned long)sdi12_state.command_count,
	       sdi12_state.latest_command[0] != '\0' ? sdi12_state.latest_command :
						       "-");
}

int main(void)
{
	struct ads1232_state ads_state;
	int64_t next_rs485_poll_ms = 0;
	int64_t next_can_tx_ms = 0;
	int64_t next_i2c_scan_ms = 0;
	int32_t raw;
	int ret;

	start_usb_cdc_console();

	printk("dajilog_tech_V0.1 component bring-up\r\n");
	printk("USB CDC console is ready\r\n");
	printk("Components: ADS1232, OLED, RS485, CAN, SBUS, SDI-12\r\n");

	k_sleep(K_MSEC(5));
	ret = ads1232_init(&loadcell_adc);
	if (ret != 0) {
		printk("ADS1232 init failed ret=%d\r\n", ret);
	} else {
		printk("ADS1232 ready: 6-wire loadcell ch=1 gain=128 rate=10SPS "
		       "SCLK=PA5 DOUT/DRDY=PA6 PWDN=PA7\r\n");
	}

	ret = oled_display_init(&oled);
	printk("ADS1232 OLED init ret=%d\r\n", ret);
	if (ret == 0) {
		oled_show_ads1232_values();
	}
	init_component_threads();

	next_rs485_poll_ms = k_uptime_get();
	next_can_tx_ms = k_uptime_get() + CONFIG_DAJILOG_CAN_TX_PERIOD_MS;
	if (CONFIG_DAJILOG_OLED_SCAN_PERIOD_MS > 0) {
		next_i2c_scan_ms = k_uptime_get() + CONFIG_DAJILOG_OLED_SCAN_PERIOD_MS;
	}

	while (1) {
		if (next_i2c_scan_ms > 0 && k_uptime_get() >= next_i2c_scan_ms) {
			ret = oled_display_scan_i2c(&oled);
			if (ret != 0) {
				printk("OLED I2C periodic scan failed ret=%d\r\n", ret);
			}
			next_i2c_scan_ms += CONFIG_DAJILOG_OLED_SCAN_PERIOD_MS;
		}

		if (k_uptime_get() >= next_rs485_poll_ms) {
			ret = rs485_modbus_request_read(&rs485_sensor);
			if (ret != 0) {
				printk("RS-485 Modbus request enqueue failed ret=%d\r\n",
				       ret);
			}
			next_rs485_poll_ms += CONFIG_DAJILOG_RS485_MODBUS_POLL_PERIOD_MS;
		}

		if (k_uptime_get() >= next_can_tx_ms) {
			ret = can_bus_send_test(&can1);
			if (ret != 0) {
				printk("CAN TX enqueue failed ret=%d\r\n", ret);
			}
			next_can_tx_ms += CONFIG_DAJILOG_CAN_TX_PERIOD_MS;
		}

		ret = ads1232_sample(&loadcell_adc, &raw);
		if (ret == 0) {
			ads1232_state_get(&loadcell_adc, &ads_state);
			printk("ADS1232 raw=%ld hex=0x%06lx\r\n", (long)raw,
			       (unsigned long)(raw & 0x00ffffff));
			printk("ADS1232 stats samples=%lu min=%ld max=%ld p2p=%ld avg=%ld\r\n",
			       (unsigned long)ads_state.sample_count,
			       (long)ads_state.min_raw, (long)ads_state.max_raw,
			       (long)(ads_state.max_raw - ads_state.min_raw),
			       (long)(ads_state.sum_raw / ads_state.sample_count));
			if (oled.state.ready) {
				oled_show_ads1232_values();
			}
			log_component_states();
		} else {
			printk("ADS1232 read failed ret=%d\r\n", ret);
		}

		k_sleep(K_MSEC(ADS1232_READ_PERIOD_MS));
	}
}
