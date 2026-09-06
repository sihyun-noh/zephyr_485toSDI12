/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include <ads1232/ads1232.h>
#include <can_bus/can_bus.h>
#include <dajilog_device_info/dajilog_device_info.h>
#include <dajilog_device_profile/dajilog_device_profile.h>
#include <dajilog_device_provisioning/dajilog_device_provisioning.h>
#include <display_oled/display_oled.h>
#include <eeprom_store/eeprom_store.h>

#include "scale_can_protocol.h"
#include "scale_can_service.h"
#include "scale_service.h"

#define USER_NODE DT_PATH(zephyr_user)
#define I2C1_NODE DT_NODELABEL(i2c1)

static const struct device *const console_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static struct ads1232 loadcell_adc = {
    .config =
        {
            .sclk = GPIO_DT_SPEC_GET(USER_NODE, ads1232_sclk_gpios),
            .dout = GPIO_DT_SPEC_GET(USER_NODE, ads1232_dout_gpios),
            .pwdn = GPIO_DT_SPEC_GET(USER_NODE, ads1232_pwdn_gpios),
            .drdy_timeout_ms = CONFIG_DAJILOG_ADS1232_DRDY_TIMEOUT_MS,
            .startup_delay_ms = CONFIG_DAJILOG_ADS1232_STARTUP_DELAY_MS,
            .sclk_half_period_us = CONFIG_DAJILOG_ADS1232_SCLK_HALF_PERIOD_US,
        },
};

static struct can_bus can1 = {
    .config =
        {
            .dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus)),
            .default_tx_id = SCALE_CAN_ID_MEASUREMENT,
        },
};

static struct oled_display oled = {
    .config =
        {
            .display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
            .i2c = DEVICE_DT_GET(I2C1_NODE),
            .i2c_speed_hz = 400000,
            .scan_i2c_on_init = false,
        },
};

static struct scale_service scale;
static struct scale_can_service scale_can;
static struct dajilog_device_profile device_profile = {
    .config =
        {
            .allow_development_profile =
                IS_ENABLED(CONFIG_SCALE_DEVICE_PROFILE_ALLOW_DEVELOPMENT),
            .development_node_id = CONFIG_SCALE_DEVICE_NODE_ID,
            .development_hardware_revision = CONFIG_SCALE_DEVICE_HW_REVISION,
            .development_serial_number = CONFIG_SCALE_DEVICE_SERIAL_NUMBER,
        },
};
static struct dajilog_device_info device_info;

static void scale_device_runtime_get(void *user_data,
                                     struct dajilog_device_runtime *runtime) {
  struct scale_service *service = user_data;
  struct scale_state state;

  scale_service_state_get(service, &state);
  if (!state.ready || !state.value_valid || !state.calibration.valid ||
      state.last_error != 0) {
    *runtime = (struct dajilog_device_runtime){
        .health = DAJILOG_DEVICE_HEALTH_DEGRADED,
        .last_error = state.last_error,
    };
    return;
  }

  *runtime = (struct dajilog_device_runtime){
      .health = DAJILOG_DEVICE_HEALTH_READY,
      .last_error = 0,
  };
}

static void start_usb_cdc_console(void) {
  uint32_t dtr = 0;

  k_sleep(K_MSEC(2000));
  if (!IS_ENABLED(CONFIG_USB_DEVICE_INITIALIZE_AT_BOOT)) {
    for (int i = 0; i < 3; i++) {
      if (usb_enable(NULL) == 0) {
        break;
      }
      k_sleep(K_MSEC(200));
    }
  }

  for (int elapsed = 0; elapsed < 3000; elapsed += 100) {
    (void)uart_line_ctrl_get(console_dev, UART_LINE_CTRL_DTR, &dtr);
    if (dtr != 0) {
      break;
    }
    k_sleep(K_MSEC(100));
  }
}

int main(void) {
  struct dajilog_device_profile_state profile_state;
  struct dajilog_device_identity identity = {
      .device_type = DAJILOG_DEVICE_TYPE_SCALE,
      .hardware_revision = CONFIG_SCALE_DEVICE_HW_REVISION,
      .firmware_major = CONFIG_SCALE_DEVICE_FW_MAJOR,
      .firmware_minor = CONFIG_SCALE_DEVICE_FW_MINOR,
      .firmware_patch = CONFIG_SCALE_DEVICE_FW_PATCH,
      .firmware_build = CONFIG_SCALE_DEVICE_FW_BUILD,
      .app_protocol_version = SCALE_CAN_PROTOCOL_VERSION,
      .capability =
          DAJILOG_CAPABILITY_MEASUREMENT | DAJILOG_CAPABILITY_CALIBRATION |
          DAJILOG_CAPABILITY_EEPROM_STORE | DAJILOG_CAPABILITY_SERVICE_MODE,
  };
  int ret;

  start_usb_cdc_console();
  printk("Dajilog scale app skeleton\r\n");
  printk("ADS1232 -> scale service -> CAN/OLED\r\n");

  ret = ads1232_init(&loadcell_adc);
  printk("ADS1232 init ret=%d\r\n", ret);
  if (ret != 0) {
    return ret;
  }

  ret = eeprom_store_init();
  printk("EEPROM readiness ret=%d (read/write test disabled by default)\r\n",
         ret);

  ret = dajilog_device_profile_init(&device_profile);
  dajilog_device_profile_get(&device_profile, &profile_state);
  printk("Device profile init ret=%d valid=%d persistent=%d development=%d "
         "node=%u serial=%u hw=%u\r\n",
         ret, profile_state.valid, profile_state.persistent,
         profile_state.development, profile_state.data.node_id,
         (unsigned int)profile_state.data.serial_number,
         profile_state.data.hardware_revision);
  if (ret != 0) {
    return ret;
  }
  identity.hardware_revision = profile_state.data.hardware_revision;
  ret = dajilog_device_info_init(&device_info, &identity, &device_profile);
  printk("Device Info init ret=%d request=0x%03x response=0x%03x\r\n", ret,
         DAJILOG_DEVICE_INFO_REQUEST_BASE_ID + profile_state.data.node_id,
         DAJILOG_DEVICE_INFO_RESPONSE_BASE_ID + profile_state.data.node_id);
  if (ret != 0) {
    return ret;
  }
  ret = dajilog_device_provisioning_init(
      &(struct dajilog_device_provisioning_config){
          .device_info = &device_info,
          .runtime_get = scale_device_runtime_get,
          .user_data = &scale,
      });
  printk("Device provisioning shell init ret=%d\r\n", ret);
  if (ret != 0) {
    return ret;
  }

  if (IS_ENABLED(CONFIG_SCALE_CAN_ENABLE)) {
    ret = can_bus_init(&can1);
    if (ret == 0) {
      ret = can_bus_start(&can1);
    }
    printk("CAN init ret=%d\r\n", ret);
    if (ret != 0) {
      return ret;
    }
  } else {
    printk("CAN disabled for ADC diagnosis\r\n");
  }

  ret = oled_display_init(&oled);
  printk("OLED init ret=%d\r\n", ret);

  ret = scale_service_init(&scale, &loadcell_adc);
  if (ret == 0) {
    ret = scale_service_start(&scale);
  }
  printk("Scale service start ret=%d\r\n", ret);
  if (ret != 0) {
    return ret;
  }

  if (IS_ENABLED(CONFIG_SCALE_CAN_ENABLE)) {
    ret = scale_can_service_init(&scale_can, &can1, &scale, &device_info);
    if (ret == 0) {
      ret = scale_can_service_start(&scale_can);
    }
    printk("Scale CAN service start ret=%d\r\n", ret);
  }

  while (1) {
    struct ads1232_state adc_state;
    struct scale_state state;
    char line0[22];
    char line1[22];

    scale_service_state_get(&scale, &state);
    ads1232_state_get(&loadcell_adc, &adc_state);
    snprintk(line0, sizeof(line0), "RAW %ld", (long)state.latest_raw);
    snprintk(line1, sizeof(line1), "Net %ld mg %s", (long)state.net_weight_mg,
             state.stable ? "ST" : "MV");
    (void)oled_display_show_text(&oled, "Dajilog Scale", line0, line1,
                                 "CAN + calibration");
    printk("SCALE raw=%ld applied=%ld p2p=%ld stable=%d net_mg=%ld cal=%d "
           "scale_err=%d adc_ready=%d adc_valid=%d adc_err=%d adc_n=%llu\r\n",
           (long)state.latest_raw, (long)state.filtered_raw,
           (long)state.p2p_raw, state.stable, (long)state.net_weight_mg,
           state.calibration_state, state.last_error, adc_state.ready,
           adc_state.value_valid, adc_state.last_error,
           (unsigned long long)adc_state.sample_count);
    k_sleep(K_MSEC(200));
  }
}
