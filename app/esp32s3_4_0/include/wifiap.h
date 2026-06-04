#ifndef WIFIAP_H
#define WIFIAP_H

#include <zephyr/kernel.h>

/**
 * @brief Wi-Fi AP 모드를 시작합니다.
 *
 * @return 성공 시 0, 실패 시 음수 값
 */
int start_wifi_ap(void);

/**
 * @brief Wi-Fi AP 연결 완료를 기다립니다.
 */
void wait_wifi_ap_ready(void);

/**
 * @brief AP 모드 준비 완료를 기다립니다.
 */
void wait_ap_ready(void);

#endif /* WIFIAP_H */
