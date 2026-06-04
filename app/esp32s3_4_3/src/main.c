

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

// strip
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/spi.h>

#include "zephyr/drivers/display.h"
#include <zephyr/display/cfb.h>

#include "adc.h"

#define DISPLAY_WIDTH 128
#define DISPALY_HEIGHT 64
#define MESSAGE "Are you ready?? "

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define STRIP_NODE DT_ALIAS(led_strip)

#if DT_NODE_HAS_PROP(DT_ALIAS(led_strip), chain_length)
#define STRIP_NUM_PIXELS DT_PROP(DT_ALIAS(led_strip), chain_length)
#else
#error Unable to determine length of LED strip
#endif

#define DELAY_TIME K_MSEC(1000)
#define RGB(_r, _g, _b) {.r = (_r), .g = (_g), .b = (_b)}

static const struct led_rgb colors[] = {
    RGB(0x0f, 0x00, 0x00), /* red */
    RGB(0x00, 0x0f, 0x00), /* green */
    RGB(0x00, 0x00, 0x0f), /* blue */
};

// display
static const struct device *display_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

// strip led for ws2812
static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_NUM_PIXELS];

int start_led_strip(void) {
  size_t color = 0;
  int rc;

  if (device_is_ready(strip)) {
    LOG_INF("Found LED strip device %s", strip->name);
  } else {
    LOG_ERR("LED strip device %s is not ready", strip->name);
    return 0;
  }

  LOG_INF("Displaying pattern on strip");
  while (1) {
    for (size_t cursor = 0; cursor < ARRAY_SIZE(pixels); cursor++) {
      memset(&pixels, 0x00, sizeof(pixels));
      memcpy(&pixels[cursor], &colors[color], sizeof(struct led_rgb));

      rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);
      if (rc) {
        LOG_ERR("couldn't update strip: %d", rc);
      }

      k_sleep(DELAY_TIME);
    }

    color = (color + 1) % ARRAY_SIZE(colors);
  }

  return 0;
}

int main(void) {

  LOG_INF("Start system ");
  int ret;

  if (display_dev == NULL) {
    LOG_ERR("디스플레이 디바이스가 NULL 입니다");
    return -1;
  }

  if (!device_is_ready(display_dev)) {
    LOG_ERR("디스플레이 디바이스가 준비되지 않았습니다");
    return -1;
  }

  ret = cfb_framebuffer_init(display_dev);
  if (ret != 0) {
    LOG_INF("could not initialize display");
    return -1;
  }
  // inverted background and font
  ret = cfb_framebuffer_invert(display_dev);

  // MQTT 메시지 표시
  ret = cfb_framebuffer_clear(display_dev, true);
  if (ret) {
    LOG_ERR("디스플레이 클리어 실패");
  }

  ret = cfb_print(display_dev, MESSAGE, 0, 0);
  if (ret) {
    LOG_ERR("MQTT 메시지 출력 실패");
  }

  ret = cfb_framebuffer_finalize(display_dev);
  if (ret) {
    LOG_ERR("디스플레이 업데이트 실패");
  }

  // Initialize and start ADC module
  adc_app_start();

  start_led_strip();
  return 0;
}
