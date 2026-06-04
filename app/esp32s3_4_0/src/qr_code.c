#include "qr_code.h"
#include <qrencode.h>
#include <zephyr/display/cfb.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(qr_code, CONFIG_LOG_DEFAULT_LEVEL);

#define QR_SIZE 2      // QR 코드 확대 배율
#define MAX_QR_SIZE 29 // QR 코드 최대 크기

int display_qr_code(const struct device *display_dev, const char *text) {
  QRcode *qrcode;
  int ret;

  // QR 코드 생성
  qrcode = QRcode_encodeString(text, 0, QR_ECLEVEL_L, QR_MODE_8, 1);
  if (qrcode == NULL) {
    LOG_ERR("QR 코드 생성 실패");
    return -EINVAL;
  }

  k_mutex_lock(&display_mutex, K_FOREVER);

  ret = cfb_framebuffer_init(display_dev);
  if (ret) {
    LOG_ERR("디스플레이 초기화 실패");
    QRcode_free(qrcode);
    return ret;
  }

  // 디스플레이 크기를 직접 지정 (SSD1306 OLED 기준)
  const int DISPLAY_WIDTH = 128;
  const int DISPLAY_HEIGHT = 64;

  LOG_INF("디스플레이 크기: %d x %d", DISPLAY_WIDTH, DISPLAY_HEIGHT);
  LOG_INF("QR 코드 크기: %d x %d", qrcode->width, qrcode->width);
  LOG_INF("확대 배율: %d", QR_SIZE);

  // QR 코드 중앙 정렬을 위한 오프셋 계산 (0,0부터 시작)
  uint8_t offset_x = (DISPLAY_WIDTH - (qrcode->width * QR_SIZE)) / 2;
  uint8_t offset_y = (DISPLAY_HEIGHT - (qrcode->width * QR_SIZE)) / 2;

  // 오프셋이 음수가 되지 않도록 보정
  offset_x = (offset_x > DISPLAY_WIDTH) ? 0 : offset_x;
  offset_y = (offset_y > DISPLAY_HEIGHT) ? 0 : offset_y;

  LOG_INF("계산된 오프셋 - X: %d, Y: %d", offset_x, offset_y);
  LOG_INF("QR 코드 시작 위치: (%d, %d)", offset_x, offset_y);
  LOG_INF("QR 코드 끝 위치: (%d, %d)", offset_x + (qrcode->width * QR_SIZE),
          offset_y + (qrcode->width * QR_SIZE));

  // QR 코드 그리기
  for (int y = 0; y < qrcode->width; y++) {
    for (int x = 0; x < qrcode->width; x++) {
      int pixel = qrcode->data[y * qrcode->width + x] & 1;
      if (pixel) {
        for (int dy = 0; dy < QR_SIZE; dy++) {
          for (int dx = 0; dx < QR_SIZE; dx++) {
            int px = offset_x + (x * QR_SIZE) + dx;
            int py = offset_y + (y * QR_SIZE) + dy;
            if (px < DISPLAY_WIDTH &&
                py < DISPLAY_HEIGHT) { // 디스플레이 범위 체크
              struct cfb_position pos = {.x = px, .y = py};
              ret = cfb_draw_point(display_dev, &pos);
              if (ret) {
                LOG_ERR("픽셀 설정 실패: (%d, %d)", px, py);
              }
            }
          }
        }
      }
    }
  }

  ret = cfb_framebuffer_finalize(display_dev);
  if (ret) {
    LOG_ERR("디스플레이 업데이트 실패");
    QRcode_free(qrcode);
    k_mutex_unlock(&display_mutex);
    return ret;
  }

  k_mutex_unlock(&display_mutex);
  QRcode_free(qrcode);
  return 0;
}
