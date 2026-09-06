/* SPDX-License-Identifier: Apache-2.0 */

#ifndef DAJILOG_DISPLAY_OLED_H_
#define DAJILOG_DISPLAY_OLED_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

struct oled_display_config {
	const struct device *display;
	const struct device *i2c;
	uint32_t i2c_speed_hz;
	bool scan_i2c_on_init;
};

struct oled_display_state {
	bool ready;
	int last_error;
	uint16_t width;
	uint16_t height;
	uint8_t font_width;
	uint8_t font_height;
	uint8_t i2c_found_count;
};

struct oled_ads1232_view {
	bool value_valid;
	int32_t latest_raw;
	int32_t avg_raw;
	int32_t p2p_raw;
	uint32_t sample_count;
};

struct oled_display {
	struct oled_display_config config;
	struct oled_display_state state;
};

int oled_display_init(struct oled_display *ctx);
int oled_display_scan_i2c(struct oled_display *ctx);
int oled_display_show_text(struct oled_display *ctx, const char *line0,
			   const char *line1, const char *line2,
			   const char *line3);
int oled_display_show_ads1232(struct oled_display *ctx,
			      const struct oled_ads1232_view *view);
void oled_display_state_get(const struct oled_display *ctx,
			    struct oled_display_state *out);

#endif /* DAJILOG_DISPLAY_OLED_H_ */
