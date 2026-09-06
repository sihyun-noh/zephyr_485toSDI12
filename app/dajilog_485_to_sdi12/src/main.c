/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

#include "app_context.h"
#include "eeprom_store/eeprom_store.h"

#define USER_NODE DT_PATH(zephyr_user)
#define USB_ENABLE_RETRY_COUNT 3
#define USB_START_DELAY_MS 2000
#define USB_DTR_WAIT_MS 3000
#ifndef CONFIG_BRIDGE_DO_STARTUP_TEST_ON_MS
#define CONFIG_BRIDGE_DO_STARTUP_TEST_ON_MS 300
#endif

static const struct device *const console_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

struct do_output {
  const char *name;
  struct gpio_dt_spec gpio;
};

static const struct do_output do_outputs[] = {
    {
        .name = "do_od1_pb3",
        .gpio = GPIO_DT_SPEC_GET(USER_NODE, od1_gpios),
    },
    {
        .name = "do_od2_pb4",
        .gpio = GPIO_DT_SPEC_GET(USER_NODE, od2_gpios),
    },
};

static void start_usb_cdc_console(void) {
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

static int do_outputs_init(void) {
  for (size_t i = 0; i < ARRAY_SIZE(do_outputs); i++) {
    const struct do_output *output = &do_outputs[i];
    int ret;

    if (!device_is_ready(output->gpio.port)) {
      printk("%s GPIO port is not ready\r\n", output->name);
      return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&output->gpio, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
      printk("%s configure failed ret=%d\r\n", output->name, ret);
      return ret;
    }

    (void)gpio_pin_set_dt(&output->gpio, 0);
    printk("%s init: %s pin %u released\r\n", output->name,
           output->gpio.port->name, output->gpio.pin);
  }

  return 0;
}

static void do_outputs_startup_test(void) {
  if (!IS_ENABLED(CONFIG_BRIDGE_DO_STARTUP_TEST)) {
    return;
  }

  for (size_t i = 0; i < ARRAY_SIZE(do_outputs); i++) {
    const struct do_output *output = &do_outputs[i];

    printk("%s test active\r\n", output->name);
    (void)gpio_pin_set_dt(&output->gpio, 1);
    k_sleep(K_MSEC(CONFIG_BRIDGE_DO_STARTUP_TEST_ON_MS));

    printk("%s test released\r\n", output->name);
    (void)gpio_pin_set_dt(&output->gpio, 0);
    k_sleep(K_MSEC(100));
  }
}

static void pb34_only_test_forever(void) {
  int ret;

  if (!IS_ENABLED(CONFIG_BRIDGE_PB34_ONLY_TEST)) {
    return;
  }

  ret = do_outputs_init();
  if (ret != 0) {
    printk("PB3/PB4 test init failed ret=%d\r\n", ret);
    return;
  }

  printk("PB3/PB4 only test start: period=%dms\r\n",
         CONFIG_BRIDGE_PB34_ONLY_TEST_PERIOD_MS);

  while (1) {
    for (size_t i = 0; i < ARRAY_SIZE(do_outputs); i++) {
      const struct do_output *output = &do_outputs[i];

      printk("%s active\r\n", output->name);
      (void)gpio_pin_set_dt(&output->gpio, 1);
      k_sleep(K_MSEC(CONFIG_BRIDGE_PB34_ONLY_TEST_PERIOD_MS));

      printk("%s released\r\n", output->name);
      (void)gpio_pin_set_dt(&output->gpio, 0);
      k_sleep(K_MSEC(CONFIG_BRIDGE_PB34_ONLY_TEST_PERIOD_MS));
    }
  }
}

int main(void) {
  struct app_msg boot_msg = {
      .type = APP_MSG_BOOT_DONE,
      .timestamp_ms = k_uptime_get(),
  };
  int ret;

  start_usb_cdc_console();

  printk("dajilog_485_totsdi12 app skeleton\r\n");
  app_print_profile();
  (void)eeprom_store_init();
  (void)eeprom_store_bringup_test_once();

  pb34_only_test_forever();

  ret = do_outputs_init();
  if (ret != 0) {
    printk("do_outputs_init failed ret=%d\r\n", ret);
    return 0;
  }
  do_outputs_startup_test();

  ret = app_context_init(&app);
  if (ret != 0) {
    printk("app_context_init failed ret=%d\r\n", ret);
    return 0;
  }

  (void)app_msg_publish(&app.app_msgq, &boot_msg);

  ret = app_components_init(&app);
  if (ret != 0) {
    struct app_msg error_msg = {
        .type = APP_MSG_ERROR,
        .timestamp_ms = k_uptime_get(),
        .data.error =
            {
                .code = ret,
            },
    };

    printk("app_components_init failed ret=%d\r\n", ret);
    (void)app_msg_publish(&app.app_msgq, &error_msg);
  }

  ret = app_tasks_start(&app);
  if (ret != 0) {
    printk("app_tasks_start failed ret=%d\r\n", ret);
  }

  while (1) {
    struct app_msg msg;

    ret = k_msgq_get(&app.app_msgq, &msg,
                     K_MSEC(CONFIG_BRIDGE_FSM_POLL_TIMEOUT_MS));
    if (ret == 0) {
      printk("APP event=%s\r\n", app_msg_type_name(msg.type));
      app_fsm_handle_msg(&app.fsm, &msg);
    }
  }
}
