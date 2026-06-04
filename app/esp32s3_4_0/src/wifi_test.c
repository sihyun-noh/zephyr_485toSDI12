/*
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

#include <zephyr/display/cfb.h>

#include "led_strip.h"
#include "rtc.h"
#include "state_machine.h"
#include "usb_cdc.h"
#include "wifi_state.h"

#include "demo.h"
#include "http_server.h"
#include "k_poll_event.h"
#include "mqtt.h"
#include "qr_code.h"
#include "wifiap.h"
#include "zephyr/drivers/display.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static const uint8_t wifi_icon[16] = {0x00, 0x00, 0x7E, 0x81, 0xA5, 0xA5,
                                      0x99, 0x81, 0x7E, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00};

int mqtt_flag;
static const struct device *display_dev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

#if 0
void wifi_event_listener_thread(void) {
  while (1) {
    k_poll(&wifi_poll_event, 1, K_FOREVER); // Wi-Fi 연결 이벤트를 기다림
    if (wifi_poll_event.signal->result == WIFI_SIGNAL_SUCCESS) {
      LOG_INF("Wi-Fi Connected Event Received");
      LOG_INF("start mqtt app!");
      // mqtt_main();

      // 여기에서 연결 성공 후의 로직을 처리
    } else if (wifi_poll_event.signal->result == WIFI_SIGNAL_FAILURE) {
      LOG_INF("Wi-Fi Connection Failed Event Received");
      // 여기에서 연결 실패 후의 로직을 처리
    }

    k_poll_signal_reset(&wifi_connected_signal); // 이벤트 재설정
  }
}

K_THREAD_DEFINE(wifi_event_listener, 1024, wifi_event_listener_thread, NULL,
                NULL, NULL, 7, 0, 0);
#endif

// 뮤텍스 선언 및 초기화
K_MUTEX_DEFINE(display_mutex);

#define DISPLAY_WIDTH 128
#define DISPALY_HEIGHT 64
#define MESSAGE "Are you ready?? "

// Frame Buffer 구조체 정의
struct display_buffer_descriptor fb_desc = {
    .width = 16,                   // 가로 크기 (16px)
    .height = 16,                  // 세로 크기 (16px)
    .buf_size = sizeof(wifi_icon), // buffer 크기
    .pitch = 2                     // Frame Buffer (wifi_icon 데이터)
};

// void display_event_thread(void) {
//   int ret;
//   int x_position = DISPLAY_WIDTH;

//   LOG_INF("QR 코드 표시 성공!");
//   LOG_INF("QR 코드 표시 성공!");
//   LOG_INF("QR 코드 표시 성공!");
//   if (display_dev == NULL) {
//     LOG_ERR("device pointer is NULL");
//     return;
//   }

//   if (!device_is_ready(display_dev)) {
//     LOG_ERR("display device is not ready");
//     return;
//   }

//   // QR 코드 생성 및 표시
//   ret = display_qr_code_display_api(display_dev,
//   "https://zephyrproject.org");

//   if (ret) {
//     LOG_ERR("QR 코드 표시 실패! 오류 코드: %d", ret);
//   } else {
//     LOG_INF("QR 코드 표시 성공!");
//   }

//   while (1) {
//     k_sleep(K_SECONDS(5)); // 시스템 유지
//   }

//   // 디스플레이 설정 (1비트 흑백)
//   // if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01) < 0) {
//   //   LOG_ERR("Failed to set pixel format");
//   //   return;
//   // }

//   // Frame Buffer 데이터를 디스플레이에 출력
//   ret = display_write(display_dev, 0, 0, &fb_desc, wifi_icon);
//   if (ret) {
//     LOG_ERR("Failed to write to display: %d", ret);
//   } else {
//     LOG_INF("Wi-Fi icon displayed successfully!");
//   }

//   // 5초 후 화면에서 아이콘을 제거
//   k_sleep(K_SECONDS(10));

//   ret = cfb_framebuffer_init(display_dev);

//   if (ret != 0) {
//     LOG_INF("could not initialize display");
//     return;
//   }

//   while (1) {

//     ret = cfb_print(display_dev, MESSAGE, x_position, 0);
//     if (ret != 0) {
//       LOG_INF("Could not print to display");
//       break;
//     }
//     // Finalize the display
//     ret = cfb_framebuffer_finalize(display_dev);
//     if (ret != 0) {
//       LOG_INF("Could not finalize display");
//     }

//     // Update the text position (move it to the left)
//     x_position--;
//     LOG_INF("x_position: %d", x_position);
//     LOG_INF("strlen message : %d", strlen(MESSAGE) * -1);

//     // When the message moves out of the screen, reset position
//     if (x_position > (strlen(MESSAGE) * -1) * 6) {
//       // Clear the screen and print the text
//       x_position = DISPLAY_WIDTH;
//       ret = cfb_framebuffer_clear(display_dev, true);
//       if (ret != 0) {
//         LOG_INF("Could not clear display");
//         break;
//       }
//     }

//     k_msleep(10); // Move the text every 100ms
//   }

//   /* basic code*/
//   // ret = cfb_framebuffer_init(display_dev);
//   //
//   // if (ret != 0) {
//   //   LOG_INF("could not initialize display");
//   //   return;
//   // }
//   // ret = cfb_print(display_dev, "Are you OK?", 0, 0);
//   // if (ret != 0) {
//   //   LOG_INF("could not print to display");
//   // }
//   //
//   // ret = cfb_framebuffer_finalize(display_dev);
//   // if (ret != 0) {
//   //   LOG_INF("could not finalize to display");
//   // }
//   //
//   while (1) {
//     k_poll(&shared_mqtt_data.event, 1, K_FOREVER); // 이벤트를 기다림
//     if (shared_mqtt_data.event.signal->result == DISPLAY_WRITE) {
//       /*ssd 1306 display*/

//       LOG_INF("display print");

//       k_mutex_lock(&display_mutex, K_FOREVER);
//       ret = cfb_framebuffer_clear(display_dev, true);
//       if (ret != 0) {
//         LOG_INF("could not clear framebuffer to display");
//       }

//       ret = cfb_print(display_dev, shared_mqtt_data.data.payload, 0, 0);
//       if (ret != 0) {
//         LOG_INF("could not print to display");
//       }
//       ret = cfb_framebuffer_finalize(display_dev);
//       if (ret != 0) {
//         LOG_INF("could not finalize to display");
//       }
//       k_mutex_unlock(&display_mutex);
//     }
//     k_poll_signal_reset(&shared_mqtt_data.signal); // 이벤트 재설정
//   }
// }

void display_event_thread(void) {
  struct display_capabilities cap;
  int ret;
  int x_position = DISPLAY_WIDTH;

  LOG_INF("QR 코드 표시 스레드 시작!");

  if (display_dev == NULL) {
    LOG_ERR("디스플레이 디바이스가 NULL 입니다");
    return;
  }

  if (!device_is_ready(display_dev)) {
    LOG_ERR("디스플레이 디바이스가 준비되지 않았습니다");
    return;
  }

  ret = display_qr_code(display_dev, "https://zephyrproject.org");

  k_msleep(10000);

  while (1) {
    k_poll(&shared_mqtt_data.event, 1, K_FOREVER); // MQTT 이벤트 대기

    if (shared_mqtt_data.event.signal->result == DISPLAY_WRITE) {
      LOG_INF("MQTT 메시지를 디스플레이에 출력!");

      k_mutex_lock(&display_mutex, K_FOREVER);

      ret = display_write(display_dev, 0, 0, &fb_desc, wifi_icon);
      if (ret) {
        LOG_ERR("디스플레이 쓰기 실패: %d", ret);
      } else {
        LOG_INF("Wi-Fi 아이콘 표시 완료!");
      }

      // MQTT 메시지 표시
      ret = cfb_framebuffer_clear(display_dev, true);
      if (ret) {
        LOG_ERR("디스플레이 클리어 실패");
      }

      ret = cfb_print(display_dev, shared_mqtt_data.data.payload, 0, 0);
      if (ret) {
        LOG_ERR("MQTT 메시지 출력 실패");
      }

      ret = cfb_framebuffer_finalize(display_dev);
      if (ret) {
        LOG_ERR("디스플레이 업데이트 실패");
      }

      k_mutex_unlock(&display_mutex);
    }

    k_poll_signal_reset(&shared_mqtt_data.signal); // 이벤트 재설정
  }
}

K_THREAD_DEFINE(display_event_handler, 2048, display_event_thread, NULL, NULL,
                NULL, 8, 0, 0);

int main(void) {

  test_main();

  // Wi-Fi AP 모드 시작
  if (start_wifi_ap() == 0) {
    // Wi-Fi AP 활성화 대기
    wait_ap_ready();

    // HTTP 서버 시작
    start_http_server();
  }

#if 0
  LOG_INF("wifi_state");
  reg_cb_wifi_state();
#endif
  //   start_smf();
  // ds3231_rtc_init();
  // start_led_strip();
  // mqtt_main();
  // usb_cdc_start();
  return 0;
}
