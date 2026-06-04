#ifndef _C_K_PULL_EVENT_H_
#define _C_K_PULL_EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/kernel.h>

typedef enum {
  WIFI_SIGNAL_SUCCESS = 0x01,
  WIFI_SIGNAL_FAILURE,
  DISPLAY_WRITE,

} wifi_signal_result_t;

extern struct k_poll_signal wifi_connected_signal;
extern struct k_poll_event wifi_poll_event;

struct mqtt_data {
  char payload[256]; // 수신된 MQTT 메시지의 데이터를 저장
};

// 공유 데이터와 이벤트를 관리할 구조체
struct mqtt_shared {
  struct mqtt_data data;
  struct k_poll_signal signal;
  struct k_poll_event event;
};

extern struct mqtt_shared shared_mqtt_data;

#ifdef __cplusplus
}
#endif

#endif /*_C_K_PULL_EVENT_H_*/
