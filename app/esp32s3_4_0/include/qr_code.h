#ifndef QR_CODE_H
#define QR_CODE_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>

// mutex를 extern으로 선언
extern struct k_mutex display_mutex;

/**
 * @brief QR 코드를 디스플레이에 표시
 *
 * @param display_dev 디스플레이 디바이스
 * @param text QR 코드로 변환할 텍스트
 * @return int 성공 시 0, 실패 시 음수 값
 */
int display_qr_code(const struct device *display_dev, const char *text);
int display_qr_code_display_api(const struct device *display_dev,
                                const char *text);
#endif /* QR_CODE_H */
