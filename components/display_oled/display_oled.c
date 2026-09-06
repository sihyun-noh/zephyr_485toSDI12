/* SPDX-License-Identifier: Apache-2.0 */

#include "display_oled.h"

#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int oled_display_scan_i2c(struct oled_display *ctx)
{
	uint8_t found_count = 0;

	if (ctx == NULL) {
		return -EINVAL;
	}

	if (ctx->config.i2c == NULL || !device_is_ready(ctx->config.i2c)) {
		printk("OLED I2C device is not ready\r\n");
		return -ENODEV;
	}

	printk("OLED I2C ready: speed=%luHz\r\n",
	       (unsigned long)ctx->config.i2c_speed_hz);
	printk("OLED I2C scan start\r\n");

	for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
		if (i2c_write(ctx->config.i2c, NULL, 0, addr) == 0) {
			printk("OLED I2C scan found: 0x%02x\r\n", addr);
			found_count++;
		}
	}

	ctx->state.i2c_found_count = found_count;
	printk("OLED I2C scan done: found=%u\r\n", found_count);

	return 0;
}

int oled_display_init(struct oled_display *ctx)
{
	char line[32];
	int ret;

	if (ctx == NULL) {
		return -EINVAL;
	}

	ctx->state.ready = false;
	ctx->state.last_error = 0;
	ctx->state.i2c_found_count = 0;

	if (ctx->config.scan_i2c_on_init) {
		ret = oled_display_scan_i2c(ctx);
		if (ret != 0) {
			ctx->state.last_error = ret;
			return ret;
		}
	}

	if (ctx->config.display == NULL ||
	    !device_is_ready(ctx->config.display)) {
		printk("SSD1309 display device is not ready\r\n");
		ctx->state.last_error = -ENODEV;
		return -ENODEV;
	}

	ret = display_set_pixel_format(ctx->config.display, PIXEL_FORMAT_MONO10);
	if (ret != 0) {
		ret = display_set_pixel_format(ctx->config.display,
					       PIXEL_FORMAT_MONO01);
		if (ret != 0) {
			printk("SSD1309 pixel format configure failed ret=%d\r\n",
			       ret);
			ctx->state.last_error = ret;
			return ret;
		}
	}

	ret = cfb_framebuffer_init(ctx->config.display);
	if (ret != 0) {
		printk("SSD1309 CFB init failed ret=%d\r\n", ret);
		ctx->state.last_error = ret;
		return ret;
	}

	(void)cfb_framebuffer_set_font(ctx->config.display, 0);
	(void)display_blanking_off(ctx->config.display);

	ctx->state.width = cfb_get_display_parameter(ctx->config.display,
						     CFB_DISPLAY_WIDTH);
	ctx->state.height = cfb_get_display_parameter(ctx->config.display,
						      CFB_DISPLAY_HEIGH);
	if (cfb_get_font_size(ctx->config.display, 0, &ctx->state.font_width,
			      &ctx->state.font_height) != 0) {
		ctx->state.font_width = 0;
		ctx->state.font_height = 0;
	}

	ctx->state.ready = true;
	ctx->state.last_error = 0;

	snprintk(line, sizeof(line), "%ux%u f%ux%u", ctx->state.width,
		 ctx->state.height, ctx->state.font_width,
		 ctx->state.font_height);
	(void)oled_display_show_text(ctx, "DAJILOG", "SSD1309 128x64",
				     "I2C1 0x3C", line);

	printk("SSD1309 display ready: %ux%u font=%ux%u\r\n",
	       ctx->state.width, ctx->state.height, ctx->state.font_width,
	       ctx->state.font_height);

	return 0;
}

int oled_display_show_text(struct oled_display *ctx, const char *line0,
			   const char *line1, const char *line2,
			   const char *line3)
{
	if (ctx == NULL || !ctx->state.ready) {
		return -ENODEV;
	}

	cfb_framebuffer_clear(ctx->config.display, false);

	if (line0 != NULL) {
		cfb_print(ctx->config.display, line0, 0, 0);
	}
	if (line1 != NULL) {
		cfb_print(ctx->config.display, line1, 0, 16);
	}
	if (line2 != NULL) {
		cfb_print(ctx->config.display, line2, 0, 32);
	}
	if (line3 != NULL) {
		cfb_print(ctx->config.display, line3, 0, 48);
	}

	cfb_framebuffer_finalize(ctx->config.display);
	return 0;
}

int oled_display_show_ads1232(struct oled_display *ctx,
			      const struct oled_ads1232_view *view)
{
	char line1[32];
	char line2[32];
	char line3[32];

	if (ctx == NULL || view == NULL) {
		return -EINVAL;
	}

	if (!view->value_valid || view->sample_count == 0) {
		return oled_display_show_text(ctx, "ADS1232 10SPS", "RAW: WAIT",
					      "CH1 GAIN128", "PA5 PA6 PA7");
	}

	snprintk(line1, sizeof(line1), "RAW:%ld", (long)view->latest_raw);
	snprintk(line2, sizeof(line2), "AVG:%ld", (long)view->avg_raw);
	snprintk(line3, sizeof(line3), "P2P:%ld N:%lu",
		 (long)view->p2p_raw, (unsigned long)view->sample_count);

	return oled_display_show_text(ctx, "ADS1232 10SPS", line1, line2, line3);
}

void oled_display_state_get(const struct oled_display *ctx,
			    struct oled_display_state *out)
{
	if (ctx == NULL || out == NULL) {
		return;
	}

	*out = ctx->state;
}
