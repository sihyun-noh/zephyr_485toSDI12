/* SPDX-License-Identifier: Apache-2.0 */

#include "phase3_devices.h"
#include "controller_state.h"
#include "ejection_settings.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <display_oled/display_oled.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define IO_NODE DT_NODELABEL(ejection_io)
#define UI_UPDATE_DELAY_MS 25

static const struct device *const can_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static const struct gpio_dt_spec encoder_a =
    GPIO_DT_SPEC_GET(IO_NODE, encoder_a_gpios);
static const struct gpio_dt_spec encoder_b =
    GPIO_DT_SPEC_GET(IO_NODE, encoder_b_gpios);
static const struct gpio_dt_spec encoder_switch =
    GPIO_DT_SPEC_GET(IO_NODE, encoder_switch_gpios);

static struct oled_display oled = {
    .config =
        {
            .display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
            .i2c = DEVICE_DT_GET(DT_NODELABEL(i2c3)),
            .i2c_speed_hz = 400000,
            .scan_i2c_on_init = true,
        },
};

static struct gpio_callback encoder_a_cb;
static struct gpio_callback encoder_b_cb;
static struct gpio_callback encoder_switch_cb;
static struct k_work_delayable ui_update_work;
static atomic_t encoder_position;
static atomic_t encoder_pressed;
static atomic_t can_rx_count;
static uint8_t encoder_state;
static bool can_ready;
static bool oled_ready;
static bool encoder_ready;
static struct can_frame last_rx;
static struct k_spinlock can_rx_lock;

static const int8_t quadrature_table[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0,
};

static void update_oled_work(struct k_work *work) {
  ARG_UNUSED(work);
  char line1[24];
  char line2[24];
  char line3[24];
  struct can_bus_err_cnt err_cnt = {0};
  enum can_state state = CAN_STATE_STOPPED;

  if (!oled_ready) {
    return;
  }

  struct ejection_settings_state settings;
  struct controller_state_snapshot controller;
  ejection_settings_get(&settings);
  controller_state_get(&controller);
  (void)can_get_state(can_dev, &state, &err_cnt);
  snprintk(line1, sizeof(line1), "ENC:%ld KEY:%u",
           (long)atomic_get(&encoder_position) /
               settings.data.encoder_counts_per_step,
           atomic_get(&encoder_pressed) != 0 ? 1U : 0U);
  snprintk(line2, sizeof(line2), "CAN:%d RX:%lu", state,
           (unsigned long)atomic_get(&can_rx_count));
  if (controller.state == CONTROLLER_STATE_FAULT) {
    snprintk(line3, sizeof(line3), "FAULT:%s",
             controller_fault_name(controller.fault));
  } else {
    snprintk(line3, sizeof(line3), "STATE:%s",
             controller_state_name(controller.state));
  }
  (void)oled_display_show_text(&oled, "12CH EJECTION", line1, line2, line3);
}

static void encoder_edge(const struct device *port, struct gpio_callback *cb,
                         gpio_port_pins_t pins) {
  ARG_UNUSED(port);
  ARG_UNUSED(cb);
  ARG_UNUSED(pins);
  int a = gpio_pin_get_dt(&encoder_a);
  int b = gpio_pin_get_dt(&encoder_b);

  if (a < 0 || b < 0) {
    return;
  }

  uint8_t next = ((a != 0) ? 2U : 0U) | ((b != 0) ? 1U : 0U);
  uint8_t transition = (encoder_state << 2) | next;
  int8_t delta = quadrature_table[transition];
  encoder_state = next;
  if (delta != 0) {
    atomic_add(&encoder_position, delta);
    (void)k_work_reschedule(&ui_update_work, K_MSEC(UI_UPDATE_DELAY_MS));
  }
}

static void encoder_switch_edge(const struct device *port,
                                struct gpio_callback *cb,
                                gpio_port_pins_t pins) {
  ARG_UNUSED(port);
  ARG_UNUSED(cb);
  ARG_UNUSED(pins);
  int pressed = gpio_pin_get_dt(&encoder_switch);

  if (pressed >= 0) {
    atomic_set(&encoder_pressed, pressed != 0);
    (void)k_work_reschedule(&ui_update_work, K_MSEC(UI_UPDATE_DELAY_MS));
  }
}

static void can_rx_callback(const struct device *dev, struct can_frame *frame,
                            void *user_data) {
  ARG_UNUSED(dev);
  ARG_UNUSED(user_data);
  k_spinlock_key_t key = k_spin_lock(&can_rx_lock);
  last_rx = *frame;
  k_spin_unlock(&can_rx_lock, key);
  atomic_inc(&can_rx_count);
  (void)k_work_reschedule(&ui_update_work, K_MSEC(UI_UPDATE_DELAY_MS));
}

static int encoder_init(void) {
  if (!gpio_is_ready_dt(&encoder_a) || !gpio_is_ready_dt(&encoder_b) ||
      !gpio_is_ready_dt(&encoder_switch)) {
    return -ENODEV;
  }

  int ret = gpio_pin_configure_dt(&encoder_a, GPIO_INPUT | GPIO_PULL_UP);
  if (ret != 0) {
    return ret;
  }
  ret = gpio_pin_configure_dt(&encoder_b, GPIO_INPUT | GPIO_PULL_UP);
  if (ret != 0) {
    return ret;
  }
  ret = gpio_pin_configure_dt(&encoder_switch, GPIO_INPUT | GPIO_PULL_UP);
  if (ret != 0) {
    return ret;
  }

  int a = gpio_pin_get_dt(&encoder_a);
  int b = gpio_pin_get_dt(&encoder_b);
  encoder_state = ((a > 0) ? 2U : 0U) | ((b > 0) ? 1U : 0U);
  atomic_set(&encoder_pressed, gpio_pin_get_dt(&encoder_switch) > 0);

  gpio_init_callback(&encoder_a_cb, encoder_edge, BIT(encoder_a.pin));
  gpio_init_callback(&encoder_b_cb, encoder_edge, BIT(encoder_b.pin));
  gpio_init_callback(&encoder_switch_cb, encoder_switch_edge,
                     BIT(encoder_switch.pin));
  ret = gpio_add_callback(encoder_a.port, &encoder_a_cb);
  ret |= gpio_add_callback(encoder_b.port, &encoder_b_cb);
  ret |= gpio_add_callback(encoder_switch.port, &encoder_switch_cb);
  ret |= gpio_pin_interrupt_configure_dt(&encoder_a, GPIO_INT_EDGE_BOTH);
  ret |= gpio_pin_interrupt_configure_dt(&encoder_b, GPIO_INT_EDGE_BOTH);
  ret |= gpio_pin_interrupt_configure_dt(&encoder_switch, GPIO_INT_EDGE_BOTH);
  return ret;
}

static int can_init(void) {
  static const struct can_filter filter = {
      .id = 0,
      .mask = 0,
      .flags = 0,
  };

  if (!device_is_ready(can_dev)) {
    return -ENODEV;
  }

  struct ejection_settings_state settings;
  ejection_settings_get(&settings);
  int ret = can_set_bitrate(can_dev, settings.data.can_bitrate);
  if (ret != 0) {
    return ret;
  }
  ret = can_start(can_dev);
  if (ret != 0 && ret != -EALREADY) {
    return ret;
  }

  ret = can_add_rx_filter(can_dev, can_rx_callback, NULL, &filter);
  return ret < 0 ? ret : 0;
}

int phase3_devices_init(void) {
  k_work_init_delayable(&ui_update_work, update_oled_work);
  int first_error = 0;
  int ret = can_init();
  can_ready = ret == 0;
  if (ret != 0) {
    first_error = ret;
  }

  ret = encoder_init();
  encoder_ready = ret == 0;
  if (ret != 0 && first_error == 0) {
    first_error = ret;
  }

  ret = oled_display_init(&oled);
  oled_ready = ret == 0;
  if (ret != 0 && first_error == 0) {
    first_error = ret;
  }
  (void)k_work_reschedule(&ui_update_work, K_MSEC(UI_UPDATE_DELAY_MS));
  return first_error;
}

void phase3_status_get(struct phase3_status *status) {
  if (status == NULL) {
    return;
  }
  memset(status, 0, sizeof(*status));
  status->can_ready = can_ready;
  status->oled_ready = oled_ready;
  status->encoder_ready = encoder_ready;
  struct ejection_settings_state settings;
  ejection_settings_get(&settings);
  status->encoder_position =
      atomic_get(&encoder_position) / settings.data.encoder_counts_per_step;
  status->encoder_pressed = atomic_get(&encoder_pressed) != 0;
  status->can_rx_count = atomic_get(&can_rx_count);
  k_spinlock_key_t key = k_spin_lock(&can_rx_lock);
  status->last_rx = last_rx;
  k_spin_unlock(&can_rx_lock, key);
}

int phase3_can_send(uint32_t id, const uint8_t *data, size_t length) {
  if (!can_ready) {
    return -ENODEV;
  }
  if (id > CAN_STD_ID_MASK || length > CAN_MAX_DLEN) {
    return -EINVAL;
  }
  struct can_frame frame = {
      .id = id,
      .dlc = (uint8_t)length,
  };
  memcpy(frame.data, data, length);
  return can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
}

int phase3_oled_show(const char *line0, const char *line1, const char *line2,
                     const char *line3) {
  return oled_ready ? oled_display_show_text(&oled, line0, line1, line2, line3)
                    : -ENODEV;
}

void phase3_ui_refresh(void) {
  (void)k_work_reschedule(&ui_update_work, K_NO_WAIT);
}
